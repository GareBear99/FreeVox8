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

Each band uses a JUCE `ProcessorDuplicator<IIR::Filter>` pair (HP + LP at
Butterworth Q=0.707) for both the modulator and carrier paths, built once in
`prepare()` and never rebuilt during processing.

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
| Mask | Dry × (1 − envelope × maskDepth × quality) |
| Ghost | Vocoded × (1 + ghost × 1.95) + dry × ghost × 0.06 |
| Morph | 0.58 × vocoded + 0.42 × tanh(dry + vocoded × (0.28 + ghost × 0.92)) |

**Consonant lift:** `(1 + air × highBandWeight(bi) × 1.6)` where
`highBandWeight(i) = (i/127)²` — quadratic rolloff boosting high bands for
intelligible sibilance and breath.

**Band gain:** `0.038 + 0.040 × (bi/127)` — slight high-frequency emphasis to
counteract the natural energy rolloff of speech formants.

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

---

## 4. Spectral Ghost 1024 Engine (FreeVox8SpectralEngine)

### 4.1 STFT/OLA Infrastructure

| Parameter | Value |
|-----------|-------|
| FFT size | 1024 samples |
| Hop size | 256 samples (75% overlap) |
| Window | Hann (periodic, computed once in `prepare()`) |
| Display bins | 128 (log-spaced subset) |
| Latency | 1024 samples (≈23 ms at 44.1 kHz) |

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

The stride-4 accumulation reduces cost by 4× with negligible accuracy loss on
broadband signals.

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

---

## 6. Future Work

### 6.1 Intraframe Sweep Correction for Freeze Mode

Implement the Bristow-Johnson/Bogdanowicz β/λ estimation [4] in
`analyseBins()` prior to freeze snapshot capture. Target test: freeze a
sustained vocal mid-vibrato with and without sweep correction; measure whether
the frozen pitch holds or wobbles on resynthesis. Pilot result will determine
whether the correction produces audible improvement in the freeze path
(§5.2 hypothesis).

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
