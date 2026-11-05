# Dual-Engine Spectral Vocoder Architecture: Real-Time Ghost Resynthesis and Dynamic Spectral Masking in JUCE/C++

**Gary Doman** (GareBear99 / TizWildin)  
FreeVox8 Open-Source DSP Project  
https://github.com/GareBear99/FreeVox8

---

## Abstract

This paper presents the architecture of FreeVox8, a production-grade spectral
vocoder, ghost-freeze resynthesizer, and dynamic spectral masking engine. The
system employs a dual-engine design: a conservative **128-band log-spaced
filterbank vocoder** for low-latency musical response, and a **Spectral Ghost
1024** STFT/OLA reference backend for high-resolution ghost, freeze, and morph
resynthesis. Both engines enforce allocation-free, lock-free audio processing
per the FreeEQ8 real-time safety doctrine. The architecture targets a zero-cost
open-source alternative to commercial spectral processors, offering Vocoder,
Mask, Ghost, and Morph modes, MIDI carrier control, formant shift via RT-safe
envelope remapping, transient protection, and atomic spectral display taps — all
from a single codebase producing both FreeVox8 (free, GPL-3.0) and future
ProVox8 tiers.

A known limitation of the current Spectral Ghost 1024 backend is that
`analyseBins()` assumes constant-frequency sinusoids within each 1024-sample
frame. For pitched material with vibrato or rapid intraframe frequency movement
captured by the freeze path, spectral energy distributes across 2–3 adjacent
log bands rather than resolving to a single bin — producing a blurred frozen
snapshot. The intraframe sweep-rate estimation technique of Bristow-Johnson and
Bogdanowicz [4] is identified as the correct mathematical treatment and is
designated as primary future work for the Spectral Ghost 1024 backend.

---

## 1. Introduction

Commercial spectral vocoders (iZotope VocalSynth, Antares AVOX, Roland VP series)
are closed-source and expensive. Open-source alternatives either lack real-time
safety, restrict mode depth, or are not production-ready for DAW deployment.

FreeVox8 targets the gap: a fully open-source (GPL-3.0) spectral vocoder with
the same real-time safety guarantees, allocation-free hot path, and build
infrastructure established in FreeEQ8 [1], extended to spectral resynthesis.

### 1.1 Product Architecture

FreeVox8 is a single-codebase JUCE/C++ plugin producing VST3 and AU targets.

**FreeVox8 (Free, GPL-3.0):** Full dual-engine spectral vocoder. 128-band
filterbank core (Low Latency 128) plus Spectral Ghost 1024 STFT/OLA backend.
Vocoder, Mask, Ghost, and Morph modes. MIDI carrier, sidechain bus, formant
shift, freeze, transient protect, stereo width, air excitation. 24 factory
presets. No audio restrictions during real-time playback. Offline export
certification pending v1.0.0 release gate.

**ProVox8 (roadmap):** Extended band count, advanced spectral morph, intraframe
sweep correction (§6.1), and additional carrier synthesis modes. Same
compile-time `#if PROVOX8` pattern as FreeEQ8/ProEQ8 [1].

The restriction logic pattern follows `LicenseValidator.h` from FreeEQ8 [1]:
real-time playback is never gated; only offline export and extended features
are tier-differentiated.

---

## 2. Dual-Engine Architecture

### 2.1 Design Rationale

A production-grade spectral vocoder requires two distinct computational modes:

- **Low-latency musical response**: the filterbank path must track envelope
  changes at sub-millisecond cadence for intelligible consonants and punchy
  transients. STFT-based methods introduce frame-length latency incompatible
  with live performance.

- **High-resolution spectral resynthesis**: ghost/freeze and morph modes require
  spectral resolution finer than a 128-band filterbank can provide. A 1024-point
  STFT resolves ~43 Hz bins at 44.1 kHz, enabling precise spectral texture
  capture and reconstruction.

Neither alone is sufficient. The dual-engine selector (`engineMode` parameter)
routes the signal to either backend without changing the parameter contract or
session state.

### 2.2 Engine Selector

```cpp
if (params.engineMode > 0)
{
    // Spectral Ghost 1024 path
    spectral.setMidiNote(currentMidiNote);
    spectral.setParameters(sp);
    spectral.process(buffer, sidechain);
    return;
}
// Low Latency 128 filterbank path (default)
```

Default is Low Latency 128 (`engineMode == 0`). The Spectral Ghost 1024 is
engaged via parameter automation. Session recall restores the correct engine
on reload.

---

## 3. Low Latency 128 Engine (FreeVox8DSP)

### 3.1 Band Layout

128 log-spaced macro bands from 55 Hz to 18,500 Hz (0.47 × Nyquist safety
margin). Band edges computed by geometric interpolation:

```
f_lo(i) = 55 × (18500/55)^(i/128)
f_hi(i) = max(f_lo(i) + 18 Hz, f_lo(i+1))
```

Each band uses a pair of mono JUCE `IIR::Filter<float>` objects (HP + LP at
Butterworth Q=0.707) per channel for both the modulator and carrier paths,
built once in `prepare()` and never rebuilt during processing. The filters
are deliberately plain per-channel mono filters, not
`ProcessorDuplicator<IIR::Filter>`: the duplicator has no `processSample()`
in JUCE 7 or 8, and this engine processes sample-by-sample, so the
duplicator form as originally written did not compile. (Fixed 2026-11-05;
the instance count is unchanged — 128 bands × 2 paths × 2 filters = 512
filter instances per channel.)

**Measured — intelligibility saturates by 16 bands (STOI, 2026-11-05).**
The band count is a compile-time constant (`FREEVOX8_NUM_BANDS`, default
128); rebuilding with 32 or 16 re-spaces the same log filterbank over the
same 55 Hz–18.5 kHz range, a true fewer-band vocoder, not an envelope
grouping of the 128-band one. Band gains scale by 128/N so the drive
stage sees the same level at every count (at N = 128 the factor is
exactly 1, and a 128-band re-render of the corpus is byte-identical to
the original). On the fixed 10-sentence CMU ARCTIC corpus (bdl male +
slt female, fixed carrier MIDI 60, Bench STOI, envelope-lag aligned,
median lag 20 ms at all three counts): **128 bands 0.5817, 32 bands
0.5816, 16 bands 0.5807** (bdl 0.6136/0.6132/0.6122; slt 0.5499/0.5499/
0.5492; per-band means agree to ~0.01). The spread over 128 → 16 is
0.0010 — flat. 128 bands buys Ghost/Morph resolution, not vocoder
intelligibility; the plain vocoder is equally intelligible at 16.
Renderer: `Tests/RenderCorpus.cpp`, driver `Tests/render_corpus.sh`.

### 3.2 Carrier Synthesis

Internal MIDI-controlled carrier with four waveform components, blended by
the `carrierBlend` parameter:

```cpp
float saw   = (ph / π) - 1.0f;
float sine  = sin(ph + ch * 0.17);
float pulse = sine >= 0 ? 0.55f : -0.55f;
float sub   = sin(ph * 0.5 + ch * 0.11) * 0.22f;
float internal = 0.44*saw + 0.26*sine + 0.18*pulse + sub;
return internal * blend + dry * (1.0f - blend);
```

The `ch * 0.17` / `ch * 0.11` offsets introduce slight inter-channel phase
divergence for natural stereo spread without a dedicated width stage at the
carrier.

MIDI note tracking: `extractLatestMidiNote()` scans the JUCE `MidiBuffer` per
block and updates `currentMidiNote`. Only the latest NoteOn in the block takes
effect. The carrier frequency converts via standard A4=440 Hz equal temperament:
`hz = 440 × 2^((note-69)/12)`.

The carrier above is the Low Latency 128 carrier. The Spectral Ghost 1024
engine uses a different, fixed carrier (§4.5): `0.46*saw + 0.32*sine +
0.18*pulse`, ×0.72, + 28% dry — no sub oscillator, no `carrierBlend`
parameter. The two engines do not share a carrier implementation, and the
`carrierBlend` control only affects the filterbank engine. This section
documents each engine's carrier as shipped; unifying them is a v1.0 decision,
not a silent edit.

### 3.3 Envelope Analysis

Per-sample, per-band envelope following via asymmetric IIR:

```
mag = sqrt(bandMod² + 1e-12)
if mag > env:   env = α_attack  * env + (1 - α_attack)  * mag
else:           env = α_release * env + (1 - α_release) * mag
```

Attack/release time constants are derived from `clarity` parameter:

```
attackMs  = 0.75  + (1 - clarity) × 9.0    // 0.75–9.75 ms
releaseMs = 22.0  + (1 - clarity) × 220.0  // 22–242 ms
```

High clarity → fast attack (≈0.75 ms), short release (≈22 ms) → crisp
consonants. Low clarity → slow attack, long release → smooth vowel-like smear.

### 3.4 Freeze Mode

When `freeze > 0.5`, `analyseEnvelopeSample()` stops writing to `frozenEnv`
and the vocoder reads the frozen snapshot indefinitely:

```cpp
if (!freezeOn)
    b.frozenEnv[ch] = 0.9992f * b.frozenEnv[ch] + 0.0008f * env;

const float visible = freezeOn ? b.frozenEnv[ch] : env;
```

The 0.0008 leak factor ensures the frozen snapshot is a slowly-converging
running average of recent envelope rather than a single-frame capture. This
gives a musically stable freeze texture that represents the sustained spectral
character of the input rather than an instantaneous noise-floor snapshot.

**Filterbank freeze is not subject to the intraframe smear problem** (§5)
because envelope followers track RMS per sample continuously — there is no
frame-assumption.

### 3.5 Mode Processing

| Mode | Processing |
|------|-----------|
| Vocoder | Carrier × shifted envelope × consonant lift × band gain |
| Mask | Dry − Σ bandDry × min(0.96, e × maskDepth × 3.2) × maskK — per-band, subtractive (see below) |
| Ghost | Vocoded × (1 + ghost × 1.95) + dry × ghost × 0.06 |
| Morph | Two-envelope morph: `e = (1−α)·e₁ + α·e₂` per band (e₁ main input, e₂ sidechain, α = ghost parameter), imposed on one carrier. With no sidechain, e₂ is the frozen snapshot. The old blend formula was removed 2026-11-05 (Step 3). |

**Consonant lift:** `(1 + air × highBandWeight(bi) × 1.6)` where
`highBandWeight(i) = (i/127)²` — quadratic rolloff boosting high bands for
intelligible sibilance and breath.

**Band gain:** `0.038 + 0.040 × (bi/127)` — slight high-frequency emphasis to
counteract the natural energy rolloff of speech formants.

**Mask is per-band in this engine (since 2026-11-05, Step 7).** Each
band's dry slice is ducked by its own envelope, same shape as the Ghost
Mask: `reduction = min(0.96, e × maskDepth × 3.2)` per band, where `e` is
the band's (formant-shifted) modulator envelope. The ducked slices are
SUBTRACTED from dry (`wet = dry − Σ bandDry × reduction × maskK +
transientReturn`) instead of summing the kept slices: the filterbank sum
is not a reconstruction -- its transfer is +11..+19 dB with ±66° phase
(measured numerically from the real 2nd-order Butterworth coefficients),
so a summed kept-signal would be loud and ripply at depth 0, while the
subtractive form is bit-exact dry at depth 0 and never boosts. `maskK`
is the no-boost bound of the bank's sum transfer `H(f)`: `k ≤
2·Re(H)/|H|²` at every frequency, solved from the real coefficients in
`prepare()` (0.9× the minimum over a log probe grid; ~0.088 at 128
bands / 48 kHz, so the mean full-duck pocket is ~−5 dB with the worst
frequency at 0 dB). The pocket depth is therefore bounded by the bank's
sum ripple, not by the depth knob: a deeper pocket needs a
reconstruction-flat bank (future work). In Mask mode the band loop runs
on dry, not the internal carrier -- the old broadband Mask never let
the internal carrier reach the output either, so that contract is
unchanged (MIDI note is inert in Mask mode, as before). The old
broadband form (one reduction from the mean envelope applied to the
whole dry signal -- the pumping failure mode by construction) is gone;
the pumping test (`Tests/MaskPumping.cpp`) measures the usable depth
range on a shouted vocal and a drum modulator. The per-band mask
reduction is exposed to the UI through atomic taps (`getMaskBin()` /
`getMaskMeanReduction()`, same discipline as the spectrum display) so
the mode is legible; the UI rendering itself is a later step.

**Pumping test, measured 2026-11-05 (`Tests/MaskPumping.cpp`, VM, 48 kHz,
saw-chord carrier 110 + 164.81 + 220 Hz, Mask mode, mix 1, air 0,
transientProtect 0, outputGain −12 dB):** maskK solves to 0.0876
(predicted ~0.088); depth 0 is bit-exact dry (max|out − tanh(dry·gain)|
= 0); the display reads the pocket (depth 0.5 shout: near band mean
0.164 max 0.591, far band mean 0.064 max 0.174; depth 0: 0.0000). The
pumping metric is the paired 10 ms windowed-RMS ratio against the
depth-0 render (the carrier's own 10 ms RMS beats by 4.14 dB p-p, so an
absolute p-p measures the chord, not the Mask; the paired ratio cancels
it, floor 0 dB by construction). Shouted vocal (CMU ARCTIC bdl
arctic_a0108, ×3 tanh-clipped, looped): pocket −2.22 / −5.03 / −7.58 /
−9.73 dB at 120 Hz and −0.34 / −1.75 / −3.66 / −6.13 dB at 500 Hz, far
band (8000 Hz) 0.03–0.10 dB untouched, pumping 0.96 / 1.94 / 2.91 /
3.81 dB p-p (old broadband 1.09 / 2.35 / 3.83 / 5.65 dB -- the fix buys
up to 1.84 dB). The pocket crosses 3 dB at depth ≈0.32, pumping crosses
the 1 dB bar at depth ≈0.26: **the usable depth range is EMPTY for the
shouted vocal** -- a near miss, not a blowout. Drum (120 BPM kick
160→48 Hz + click): the pocket is INVERTED at the kick fundamental
(+0.29 / +0.56 / +0.80 / +1.03 dB at 60 Hz) and pumping is slightly
WORSE than broadband at every depth (1.02 / 2.02 / 2.90 / 3.49 dB vs
0.71 / 1.49 / 2.35 / 3.31 dB) -- when the reduction concentrates in a
few low bands, the ducked slices are phase-misaligned with the dry sum
and subtracting them adds energy at those frequencies. The vowel pocket
test (`Tests/DSPVerify.cpp` testMaskPerBand) agrees: depth 0.8 near
gain 0.814×, far gain 1.834×, pocket ratio 0.444 (< 0.7, passes) -- the
far band got louder, the same phase effect. So the per-band Mask is a
real, selective pocket-carver for broadband modulators (far bands
untouched where broadband ducked them 2.48 dB at depth 1.0), but it does
NOT meet the golden test ("audible pocket without broadband pumping",
1 dB bar) for either test modulator as built: the pocket depth is
bounded by the bank's sum ripple, and a deeper pocket per unit pumping
needs a reconstruction-flat bank (future work, §6) or a Mask design
that does not subtract phase-misaligned slices.

### 3.6 Formant Shift

Formant shift remaps the envelope readout position without modifying any
filter coefficients:

```cpp
const float formantBins = params.formantShift / 12.0f * 8.0f;
const float shiftedEnv = readShiftedEnvelope(ch, (float)bi - formantBins, freezeOn);
```

`readShiftedEnvelope()` performs linear interpolation between adjacent bands.
The shift is RT-safe: no coefficient rebuilds, no heap, executed entirely
within the per-sample loop.

The scale above — **8 bins per octave** — is the filterbank engine's scale.
The Spectral Ghost 1024 backend shifts by **10 bins per octave** (§4.5), so
the same `formantShift` value in semitones moves the formants by a different
amount in each engine. Neither scale matches the band layout: 128 log-spaced
bands over 55 Hz–18,500 Hz span log₂(18500/55) ≈ 8.39 octaves, i.e. ≈15.25
bands per octave, so a +12 semitone setting moves the formants only ≈+6.3
semitones in the filterbank and ≈+7.9 semitones in Ghost. Both engines
under-shift relative to the labelled value. **Measured 2026-11-05
(`Tests/ArtefactTaxonomy.cpp`, single-formant probes, shifted-envelope
readout via the new `getShiftedEnvelope()` accessor): +12 st moves the
formant +6.3 st (730 Hz probe) / +5.6 st (2290 Hz probe); −12 st moves it
−5.6 st on both probes; the 2290/730 ratio is preserved (3.14 → 3.00 at
+12, 3.14 at −12), so vowel identity holds — the remap moves formants
without moving pitch, as designed.** This paper states one number per
engine as shipped; unifying both engines at the geometric ≈15.25
bins/octave is a v1.0 decision (it changes the audible shift depth of existing
presets), not a silent edit. (Method note: the formant must be read from
the shifted envelope, not the output spectrum — a single output frequency
is dominated by carrier-harmonic skirt leakage, and an output-spectrum
readout returned the same carrier harmonic, 1175 Hz, for every probe and
shift.)

### 3.7 Transient Protect — Traced Mechanism

Transient Protect is a manual macro over a first-difference transient gate —
not a transient detector in the spectral-peak sense, and it must not be cited
as one (Röbel's per-peak transient detection [11] is a different mechanism;
FreeVox8 does no per-peak detection anywhere).

In the Low Latency 128 engine, per sample and per channel:

```cpp
const float transient = jlimit(0.0f, 1.0f,
    std::abs(dry - previousInput[ch]) * 12.0f);
previousInput[ch] = dry;
// ...
const float transientReturn = dry * transientProtect * transient * 0.12f;
vocoded = vocoded * drive + breath + transientReturn;
```

The gate watches the dry (main input) signal: a large sample-to-sample jump
(an onset) opens the gate, and the macro scales how much of that dry onset
is mixed back into the vocoded signal. It does **not** modulate the envelope
follower attack/release time constants, and there is no transient bypass
path — the return is an additive dry-transient feed, present in Vocoder,
Ghost and Morph modes, and added to the Mask wet signal as well. With a
sidechain connected, the gate still watches the main input, not the sidechain
modulator.

In the Spectral Ghost 1024 engine the parameter was accepted into
`Parameters` and forwarded from the filterbank, but as shipped it was never
read in `process()` / `renderFrame()` / `analyseBins()` — the control was a
no-op in the Ghost engine. That is fixed in this revision: the Ghost engine
now applies the same first-difference gate and dry-transient return per
sample in `process()`, before the wet/dry mix, so the control works in both
engines and the transient passes through un-smeared by the OLA path.

**Measured 2026-11-05 (`Tests/ArtefactTaxonomy.cpp`):** drum modulator
(120 BPM kick + click), onset rise time 10–90% — dry 0.0 ms, filterbank
13.0 ms at clarity 0.55, 6.0 ms at clarity 1.0, Ghost 23.9 ms
(report-only: the Ghost frame delay dominates, and clarity does not
change it — the Ghost envelope follower has fixed attack); pre-echo
(raw-waveform energy 20 ms before vs after the onset) — dry −204.8 dB,
filterbank −27.8 dB at clarity 0.55, −87.9 dB at clarity 1.0, Ghost
≈0.0 dB (the carrier-bleed floor, not pre-echo — the Ghost output is
never silent); doubling — the dry drum itself scores a median 1 on the
detector (its own low-frequency ripple), the vocoder introduces none
above that floor (median 0 both engines). Plosives smear far more than
kicks: a /t/ burst loses 25.1 dB burst-to-vowel ratio at clarity 0.55
(17.5 dB at clarity 1.0) — the envelope followers, not the gate, set
the plosive audibility.

### 3.8 Artefact Taxonomy — Objective Measurements (2026-11-05)

One named objective test per artefact (`Tests/ArtefactTaxonomy.cpp`,
VM, 44.1 kHz; the subjective halves need human ears — listening
materials in `~/workspace/vox8-bench/listening/`, see the workspace
paper §7):

- **Phasiness** — 4 s frozen hold, /a/ F0 220. Filterbank: spectral
  centroid 429 ± 1 Hz (static) / 287 ± 1 Hz (vibrato), frame-to-frame
  spectral flux 0.030 / 0.028 per frame, RMS drift +0.72 / +0.43 dB —
  the hold is spectrally still. Ghost: flux 0.090 / 0.119 per frame
  (3–4× the filterbank), centroid std ±10 / ±17 Hz — report-only, the
  wander is the envelope sweep, not a working vocoder's phasiness.
  The frozen vibrato vowel's spectrum differs from the frozen static
  vowel's (centroid 287 vs 429 Hz): the freeze captures the leaky
  average over vibrato cycles, not one instantaneous spectrum.
- **Musical noise** — Mask mode on sparse material (200 ms /a/
  bursts every 800 ms ducking a saw chord). Per-band reduction
  flicker in quiet passages: std 0.0033 at depth 0.8 (0.0012 at 0.3,
  0.0000 at 0) on the 0–0.96 scale; output spectral flicker equals
  the depth-0 floor (3.45 dB) at every depth — the Mask adds no
  measurable flicker above the chord's own variation.
- **Consonant loss** — synthetic fricatives through the vocoder.
  /s/ loses 14.4 dB (2–4 kHz) / 20.0 dB (4–8 kHz) at Air 0,
  improving to 8.8 / 13.5 dB at Air 1.0; /f/ 15.7 / 18.8 → 10.7 /
  12.8 dB; /sh/ 16.7 / 19.5 → 11.4 / 13.3 dB. Air recovers ~5–6 dB
  but a 9–14 dB loss remains even at Air 1.0 — the STOI per-band
  scores say the same thing (2400 Hz 0.430, 3024 Hz 0.495).
- **Freeze blur / wobble** — not re-run: pilot + Bench cross-check
  (correction gain 0.03 dB, smear 4.8–6.2 dB), §5.
- **Pumping** — not re-run: Step 7 (`Tests/MaskPumping.cpp`),
  usable depth range EMPTY at the 1 dB bar, §3.5.

---

## 4. Spectral Ghost 1024 Engine (FreeVox8SpectralEngine)

### 4.1 STFT/OLA Infrastructure

| Parameter | Value |
|-----------|-------|
| FFT size | 1024 samples |
| Hop size | 256 samples (75% overlap) |
| Window | Hann (periodic, computed once in `prepare()`) |
| Display bins | 128 (log-spaced subset) |
| Latency | 1024 samples nominal (≈23 ms at 44.1 kHz); **measured 990 samples (22.45 ms)** by noise-burst onset test (`Tests/DSPVerify.cpp`, VM) |

The ring buffer (`inputRing`, `modRing`, `olaRing`) is a circular array of
1024 floats per channel, pre-allocated in `prepare()`. `writeIndex` advances
sample-by-sample; `hopCountdown` triggers `renderFrame()` every 256 samples.
No heap allocation in `process()`.

### 4.2 Spectral Analysis — analyseBins()

For each of 128 log-spaced analysis bands, magnitude is computed via a
Goertzel-style DFT accumulation:

```cpp
double w = 2π * bandFreq(b) / sr;
double re = 0, im = 0;
for (int i = 0; i < kFftSize; i += 4) {
    float s = modRing[ch][idx] * window[i];
    re += s * cos(w * i);
    im -= s * sin(w * i);
}
float mag = sqrt(re*re + im*im) * 0.0018f;
```

The accumulation steps the ring by a per-band stride (4 where alias-free,
finer above): a stride-s Goertzel sum is alias-free only below sr/(2s), so
a fixed stride of 4 is exact only below sr/8 (~5512 Hz at 44.1 kHz). As
shipped, the stride was fixed at 4 for all bands, and every band above
sr/8 was a mirror of low-frequency content — a pure 676 Hz sine lit band
117 (11701 Hz) *stronger* than its true band 55. The claim that stride-4
has "negligible accuracy loss" was wrong for tonal signals. Fixed
2026-11-05: stride 4 for bands below sr/8, 3 below sr/6, 2 below sr/4,
1 above (101/6/9/12 bands at 44.1 kHz), with the magnitude normalised by
stride/4 against the 0.0018 scale. Cost rises from ~11.3M to ~15.4M
transcendental calls/sec/channel at 44.1 kHz — the price of correctness,
and the reason the §6.3 SIMD work matters.

**Known limitation:** The fixed `w = 2π*bandFreq/sr` assumes constant-frequency
sinusoids within the 1024-sample frame. See §5 for the intraframe smear problem
and the Bristow-Johnson/Bogdanowicz correction.

### 4.3 Envelope Tracking and Freeze

Asymmetric envelope follower with fast attack (0.24/0.76 split) and slow
release (0.92/0.08 split):

```cpp
E = mag > E ? 0.24*E + 0.76*mag   // attack
            : 0.92*E + 0.08*mag;  // release
if (!fr) frozen[ch][b] = 0.998*frozen[ch][b] + 0.002*E;
```

When freeze is active (`fr == true`), `frozen[]` stops updating and
`renderFrame()` reads from the frozen snapshot for every subsequent OLA frame.
Unlike time-scaling — where advancing through frames averages out intraframe
errors — freeze loops the same snapshot indefinitely. Any smear baked into the
snapshot at capture time is permanent.

### 4.4 OLA Resynthesis

```cpp
float e   = readEnv(env[ch], frozen[ch], t*(kDisplayBins-1) - shift, fr);
float car = inputRing[ch][idx] * window[i];
float y   = car * (0.72 + e*(2.8 + clarity*4.0)) + air*t²*whiteNoise()*0.018;
olaRing[ch][idx] += y * window[i] * 0.18f;
```

The `0.18f` OLA scale factor normalises the Hann window overlap-add sum to
unity gain for 75% overlap (4× overlap → scale = 1/(4 × 0.25) ≈ 0.25; the
0.18 value is tuned empirically against the window energy integral).

**Known gap — carrier bleed on silence.** The vocoder formula passes the
carrier at 0.72 gain even with a zero envelope (`y = car × (0.72 + e ×
...)`), so silence in produces a carrier drone out: measured max|out| =
0.083 (−21.6 dBFS) with silence in, mix = 1, air = 0
(`Tests/DSPVerify.cpp`). That fails the golden test "silence in → silence
out. The fix is to remove the 0.72 floor (and recalibrate the level) or
gate it on the envelope — a v1.0 decision, because it changes Ghost's
level structure, not a silent edit.

**Known gap — resynthesis does not follow the modulator (verified
2026-11-05, `Tests/DSPVerify.cpp` testGhostResynthesis, report-only
until the redesign lands).** The resynthesis sweeps one band's envelope
across each frame (`e = readEnv(env[ch], frozen[ch], t*(kDisplayBins-1)
- shift, fr)`), so the output does not follow the modulator; analysis is
healthy, resynthesis is broken. Measured in this engine at HEAD
`1ab2608`, 2026-11-05 (filterbank controls in parentheses): formant-shift
output centroid −12/0/+12 st = 392/390/387 Hz, static (filterbank
169/250/573 Hz, moves); burst transfer envelope ratio 4.70× vs output
ratio 1.01×, needs >5× (filterbank control 4.00× — below the 5× bar
itself; the bar is a rough bar on a skirt-dominated output probe, not
a sharp gate); Ghost morph spectral balance moves 1.42 dB, needs >6 dB;
Ghost Mask depth 0.8 pocket ratio 4.342, needs <0.7 (the near band got
louder, 4.255× — AM sidebands from the envelope sweep, not the
envelope). These figures supersede the ones first recorded for this
finding (centroid 253/254/253 Hz, burst 5.63× vs 2.20×, control 5.66×,
morph 3.07 dB, Mask 1.369 / near 1.533×): the envelope-ratio and
control drift is the Step 3 test-fixture retune (the /a/ floor
0.15 → 0.05 and the /i/ F2 width 150 → 250 Hz in `morphVowel`) —
re-running with the old fixture reproduces 5.63× and 5.66× exactly —
and the Ghost output-side drift is not explained by the Step 1
`analyseBins` stride fix (reverting it changes nothing measurable),
so the earlier output-side values are not reproducible from the
committed code and are superseded. The verdicts are unchanged on all
four sub-tests: the Ghost engine fails every one, and the filterbank
control still moves/follows. Track B (iPlug2) confirmed the same
finding independently (burst 41.5× vs 1.32×; centroid 1144/1144/1142
Hz; Mask 0.993×/0.993×). **Do not ship the Ghost engine as a vocoder
on this resynthesis.** The redesign decision (FFT cross-synthesis vs
per-band carrier filtering vs park Ghost as analysis-only) is owed —
see §6.6. The Ghost engine's mode 3 (Morph) still uses the old blend
formula and cannot work as a morph on this resynthesis.

### 4.5 Carrier and Formant Scale (Ghost Engine)

The Ghost engine does not share the filterbank carrier (§3.2). Its carrier
is fixed — no sub oscillator, no `carrierBlend` parameter, 28% dry always:

```cpp
float sine  = sin(ph + 0.13*ch);
float saw   = ph/π - 1.0f;
float pulse = sine >= 0 ? 0.52f : -0.52f;
return (0.46f*saw + 0.32f*sine + 0.18f*pulse) * 0.72f + dry * 0.28f;
```

Its formant shift is likewise its own scale — **10 bins per octave**,
against the filterbank's 8 (§3.6):

```cpp
float shift = params.formantShift / 12.0f * 10.0f;
float e = readEnv(env[ch], frozen[ch], t*(kDisplayBins-1) - shift, fr);
```

Both numbers are stated here as shipped, one per engine. See §3.6 for the
geometric note (≈15.25 bands per octave) and the open unification decision.

Transient Protect in this engine is the same first-difference gate and
dry-transient return as the filterbank (§3.7), applied per sample in
`process()` before the wet/dry mix.

Mask in this engine is per-band, unlike the filterbank's broadband Mask
(§3.5): each bin's carrier is ducked by its own envelope,
`y = car × (1 − min(0.96, e × maskDepth × 3.2))`, up to 96% suppression.

---

## 5. Intraframe Smear: The Known Limitation and Its Correction

### 5.1 Problem Statement

The Spectral Ghost 1024 `analyseBins()` function evaluates the DFT at a fixed
analysis frequency `w = 2π*bandFreq/sr` across the entire 1024-sample window.
This is mathematically correct for stationary sinusoids but introduces spectral
smear when the input contains sinusoids whose instantaneous frequency varies
within the frame — a condition common in:

- Sustained vocals with vibrato (typical rate 5–7 Hz, deviation ±0.5 semitone)
- Pitch-bent synthesizer notes
- Gliding/portamento melodic lines

For a sinusoidal component with instantaneous frequency `ω(t) = ω₀ + β·t`
(linear intraframe sweep), the Goertzel accumulation at fixed `w = ω₀`
distributes energy across adjacent bins proportional to the sweep rate β.
At a vibrato deviation of ±0.5 semitone at fc = 1 kHz, energy leaks into 2–3
adjacent log bands in the 128-bin layout.

**In freeze mode this smear is permanent.** The frozen snapshot captures the
blurred spectral envelope; every OLA frame resynthesises that blur indefinitely.
The audible result is a frozen pitch that wobbles or smears rather than holding
the captured spectral character cleanly.

In normal (non-freeze) vocoder operation the smear is less critical because
successive frames re-estimate the envelope continuously and the perceptual
integration window partially masks single-frame errors.

### 5.2 The Bristow-Johnson/Bogdanowicz Correction

Bristow-Johnson and Bogdanowicz [4] address exactly this problem in the context
of phase-vocoder time-scaling. Their method estimates the intraframe sweep rate
β and amplitude ramp rate λ per spectral peak by fitting a quadratic to the
complex log spectrum, differencing to linearise, and applying a least-squares
line fit.

The estimated β and λ parameters are then used to design a transfer function
that corrects the DFT accumulation for the instantaneous frequency slope before
the magnitude snapshot is taken.

**Direct applicability to analyseBins():** The correction would be inserted
before the magnitude calculation in `analyseBins()`:

1. Compute `re`/`im` accumulation as currently implemented.
2. Estimate β per band from the log-spectrum finite difference across the
   `modRing` history (the ring buffer infrastructure is already in place).
3. Apply the sweep correction to the `re`/`im` accumulators per eq. 9–11 of [4].
4. Compute corrected magnitude for the freeze snapshot.

No architectural changes are required — the modification is localised to
`analyseBins()`. The `modRing` ring buffer and hop infrastructure are already
sufficient.

**Note from correspondence with Bristow-Johnson (June 2026):** In their SPEED
vocoder implementation, the intraframe correction did not produce audible
improvement in time-scaling tests. However, Bristow-Johnson noted that the
freeze use case is structurally different: time-scaling advances through frames
(errors average out across the temporal sequence), whereas freeze loops a single
frame indefinitely (errors accumulate). This asymmetry makes the correction more
likely to have audible value in the freeze path than in time-scaling.
Empirical verification is designated as primary future work (§6.1).

**Pilot result (2026-11-05, `Tests/FreezePilot.cpp`): the correction was
built and measured, and it FAILED the pre-written decision rule, so it is NOT
implemented in `analyseBins()`.** The pilot implements the full method
(Gaussian-window complex spectrum, eq. 9–11 least-squares fit per peak,
eq. 15 de-sweep transfer with βy = 0). The estimator was verified first on
synthetic swept sinusoids: β recovered as +0.0 (stationary), +401/−401 (true
±400), +5036 (true +5000), +443 with λ = +3.01 (true +400/+3.0), +253 (true
+241, vibrato-scale at 220 Hz); a pure sinusoidally frequency-modulated 220 Hz
tone recovered +248/−258 against a true ±240 Hz/s. Two implementation
pitfalls were found and fixed in the pilot: a Gaussian with σt = 256 samples
has 13.5% edge truncation, whose sinc sidelobes corrupt the fit (a stationary
tone read β = +2355 Hz/s) — σt = 170 samples (α = 10740, ~1% edge value)
recovers cleanly; and Δf must be the FFT bin spacing (43 Hz), not the local
log-band spacing (~10 Hz at 220 Hz), or the fit is ill-conditioned. On the
pilot signal (sustained /a/ vowel, 6 Hz vibrato, 44.1 kHz) the corrected
frozen snapshot improved R by −0.02 dB (±0.5 st, F0 = 220), +0.02 dB (±0.5 st,
F0 = 250), +0.47 dB (±1.0 st, F0 = 220) and +0.45 dB (±1.0 st, F0 = 250),
against the ≥ 3 dB required; the stationary control was unchanged (0.00 dB).
Per-frame R (isolating the intraframe effect from the frozen average)
improved by at most +1.27 dB. The mechanism is quantitative: at vibrato
sweep rates β/α ≈ 0.02, so the intraframe sweep (≈0.4 st within one 23 ms
frame) is negligible next to the window's own spectral width, and the frozen
snapshot's smear is dominated by the leaky average over many vibrato cycles
(±0.5 st extent) — which no per-frame correction can remove. One further
limitation: on multi-harmonic vowel frames the per-peak fit is unreliable
(mean |β| of the fundamental peak 6650–9780 Hz/s against a true maximum of
~240 Hz/s), because the truncated window's sidelobes couple neighbouring
harmonics, violating the paper's isolated-peak assumption; on isolated tones
and pure FM the same estimator is accurate to a few percent. The freeze blur
therefore stays a known, bounded limitation: a frozen vibrato vowel holds its
pitch to within the vibrato extent, smeared by ~5 dB of peak concentration,
and the correction is not worth its cost in the real-time path.

**Independent cross-check, ARC-AudioBench freeze-blur test (2026-11-05,
Bench HEAD `a4fdd5a`).** The pilot's frozen snapshots were rendered to
audio as banks of sinusoids at the 128 log-band frequencies
(`Tests/FreezePilot.cpp` renderFreezeWavs — a measurement instrument,
not the Ghost resynthesis) and run through the Bench's `freeze-blur`
tool (per-harmonic peak-vs-adjacent-bin blur, magnitude-weighted mean).
Mean blur: static −3.31 dB (Gaussian, corrected identical), vibrato
−2.69 dB (Gaussian uncorrected) vs −2.72 dB (corrected) — a 0.03 dB
correction gain (Bench `--compare` reports mean_blur_difference_db
0.029), against the ≥ 3 dB rule. The two measurements agree: the
correction changes the frozen blur by nothing measurable. Track B
independently reproduced the pilot baseline exactly (stationary 220
−0.90 dB, vibrato ±0.5 st −5.73 dB; stationary 250 +0.50 dB, vibrato
−5.69 dB).

---

## 6. Future Work

### 6.1 Intraframe Sweep Correction for Freeze Mode

**Pilot run 2026-11-05 — correction NOT implemented (§5.2).** The
Bristow-Johnson/Bogdanowicz β/λ estimation was built in the pilot harness
(`Tests/FreezePilot.cpp`), verified on synthetic chirps and pure FM, and
measured against the pre-written ≥ 3 dB rule on the frozen snapshot. Best
frozen-snapshot improvement was +0.47 dB; the stationary control was
unchanged. The freeze blur stays a known, bounded limitation (see §5.2 for
the numbers and the mechanism). No further work is planned on this
correction for the freeze path; the sinusoidal-model backend (§6.2, and the
ProVox8 tier) is the remaining route to a sharper frozen pitch.

### 6.2 Full Phase Vocoder Backend

The DSP comments in `FreeVox8SpectralEngine.h` reference a planned phase-vocoder
backend. This would replace the Goertzel-style band analysis with proper phase
accumulation and phase-difference frequency estimation, enabling pitch-shifting
and time-stretching in addition to freeze resynthesis. The intraframe correction
of [4] applies directly to this path as well.

### 6.3 SIMD Optimisation

The `analyseBins()` inner loop (`re += s*cos(w*i); im -= s*sin(w*i)`) is the
dominant cost in Spectral Ghost 1024. AVX2 vectorisation of the cos/sin
evaluation (via polynomial approximation, following the SvfBandArray pattern
in FreeEQ8 [1]) is the primary performance target for v0.6.0.

### 6.4 ProVox8 Tier

Extended band count (256 bands), intraframe sweep correction, additional carrier
synthesis modes (wavetable, noise-shaped), and session-safe linear-phase
resynthesis. Same `#if PROVOX8` compile-time pattern as FreeEQ8/ProEQ8 [1].

### 6.5 Spectral Dynamics Mode

Per-bin FFT threshold clamping (Soothe2-style transient spectral limiting) using
the existing OLA infrastructure. Logically adjacent to the Mask mode already
implemented.

### 6.6 Ghost Resynthesis Redesign (decision owed)

The Ghost engine's OLA resynthesis is broken (§4.4): it sweeps one band's
envelope across each frame, so the output does not follow the modulator.
Three options, in order of increasing scope: (1) **per-band carrier
filtering** — the smallest correct fix, reusing the filterbank machinery
(one carrier filtered per band, imposed with that band's envelope);
(2) **FFT cross-synthesis** — the fuller Ghost-backend answer, and the
direction §6.2 already points (a real STFT resynthesis with the
modulator's spectrum imposed on the carrier); (3) **park Ghost as
analysis-only** — keep the backend for freeze/morph analysis and ship
only the filterbank as the vocoder. The decision is Gary's; it changes
the §6 order and the CPU story, and Track B will not start the redesign
until it lands. Until then the Ghost resynthesis checks in
`Tests/DSPVerify.cpp` are report-only, not asserting, and the Ghost
engine must not ship as a vocoder.

---

## 7. Real-Time Safety Architecture

FreeVox8 inherits the allocation-free, lock-free audio doctrine from FreeEQ8 [1].

### 7.1 Allocation-Free Hot Path

All band filter objects (`FreeVox8DSP::bands[]`), ring buffers
(`FreeVox8SpectralEngine::inputRing/modRing/olaRing`), envelope arrays
(`env[]`, `frozen[]`), and the Hann window are pre-allocated in `prepare()`.
`process()` and `renderFrame()` contain zero heap allocation.

### 7.2 No Locks, No File I/O, No Background Threads

Both engines operate entirely on the audio thread. There is no background
thread, no mutex, no file I/O, and no UI ownership in the DSP path. Atomic
display taps (`std::atomic<float> display[]`) are the sole cross-thread
communication mechanism, using `memory_order_relaxed` stores from the audio
thread and `memory_order_relaxed` loads from the UI timer.

### 7.3 Denormal Safety

The `1e-12f` addend in the filterbank magnitude calculation
(`sqrt(bandMod² + 1e-12f)`) prevents denormal conditions in the IIR envelope
follower when the modulator signal is near zero. The `tanh()` saturation in
`fastSaturate()` provides implicit soft-clipping on the output.

### 7.4 Parameter Contract

`setParameters()` is the only parameter path from the JUCE `AudioProcessorValueTreeState` to the DSP. The `Parameters` struct is copied by value into the DSP engine on each `processBlock()` call — no shared mutable state, no lock required.

---

## 8. Production Gate

Public binary release of FreeVox8 v1.0.0 requires:

1. Release build on macOS (Universal, arm64 + x86_64), Linux (x86_64), and
   Windows (x64).
2. pluginval strictness-level-10 pass for VST3 and AU.
3. DAW smoke tests: Ableton Live, REAPER, Logic Pro (AU).
4. Golden test suite pass (`docs/DSP_GOLDEN_TESTS.md`):
   - Silence in → silence out (Air = 0)
   - No NaN/Inf at parameter extremes
   - Freeze toggle: no clicks above threshold
   - Host bypass: bit-stable dry signal
   - MIDI note changes: no denormal spikes
   - Sample rates: 44.1, 48, 88.2, 96, 192 kHz
   - Buffer sizes: 1, 16, 32, 64, 128, 512, 1024, 2048 samples
5. Factory preset/session restore verification (24 presets).
6. Signed/notarized installer or clearly labeled unsigned build.
7. Release evidence pack (`scripts/make_evidence_pack.py`).

---

## 9. Acknowledgments

The dual-engine architecture pattern and real-time safety doctrine are derived
from FreeEQ8 [1], whose SPSC triple-buffer, allocation-free hot path, and
pluginval CI infrastructure are reused directly.

Correspondence with Robert Bristow-Johnson (June 2026) clarified the structural
difference between time-scaling and freeze-loop paths relevant to the intraframe
smear problem (§5.2), and confirmed the applicability of [4] to the freeze
resynthesis use case. His observation that the correction had no audible effect
in their time-scaling tests (SPEED vocoder) but may behave differently in
indefinite freeze loops directly motivates the empirical test designated in §6.1.

The WASPAA 2001 paper [4] is cited both here and in FreeEQ8 [1] (ref [15],
§9.1), where it is identified for future application to the Match EQ non-stationary
analysis path.

---

## References

[1] G. Doman, "Lock-Free Dynamic EQ Architecture: A Production-Grade SVF
    Implementation in JUCE/C++," FreeEQ8 / ProEQ8 Open-Source DSP Project,
    2026. https://github.com/GareBear99/FreeEQ8

[2] A. Simper, "Solving the continuous SVF equations using trapezoidal integration
    and equivalent currents," Cytomic, 2013.
    https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf

[3] R. Bristow-Johnson, "Audio EQ Cookbook," musicdsp.org, 1994.
    https://www.musicdsp.org/files/Audio-EQ-Cookbook.txt

[4] R. Bristow-Johnson and K. Bogdanowicz, "Intraframe Time-Scaling of
    Nonstationary Sinusoids Within the Phase Vocoder," in Proc. IEEE Workshop
    on Applications of Signal Processing to Audio and Acoustics (WASPAA),
    New Paltz, NY, Oct. 2001, pp. W2001-1–W2001-4.

[5] W. Pirkle, "Designing Audio Effect Plugins in C++," Focal Press, 2019.

[6] J. Reiss and A. McPherson, "Audio Effects: Theory, Implementation and
    Application," CRC Press, 2014.

[7] V. Zavalishin, "The Art of VA Filter Design," Urs Heckmann Audio, 2018.
    https://www.native-instruments.com/fileadmin/ni_media/downloads/pdf/VAFilterDesign_2.1.0.pdf

[8] U. Zölzer (Ed.), "DAFX: Digital Audio Effects," 2nd ed., Wiley, 2011.

[9] B. C. J. Moore, "An Introduction to the Psychology of Hearing,"
    6th ed., Brill, 2012.

[10] R. Bristow-Johnson, personal correspondence, June 2026.
     (Context: intraframe sweep correction applicability to freeze-loop
     resynthesis vs. time-scaling in the SPEED vocoder implementation.)

[11] A. Röbel, "A New Approach to Transient Processing in the Phase
     Vocoder," in Proc. 6th Int. Conf. on Digital Audio Effects (DAFx-03),
     London, Sep. 2003, pp. 344–349. — Cited only to state what Transient
     Protect is not: per-peak transient detection. FreeVox8 implements no
     such detection (§3.7).
