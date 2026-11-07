# FreeVox8 DSP Architecture

Current package: **0.5.0**

FreeVox8 now uses a **dual-engine spectral architecture**:

1. **Low Latency 128** — the default realtime-safe 128-band log-spaced vocoder engine.
2. **Spectral Ghost 1024** — a high-resolution STFT/OLA reference backend for ghost, morph, and future SIMD FFT certification.

The low-latency engine is the default public-production path because it is deterministic, simple to audit, and safe for live sessions. The spectral backend is included behind the same parameter contract so higher-resolution resynthesis can evolve without breaking sessions.

## Audio path

Main input / optional sidechain -> parameter snapshot -> engine selector -> Low Latency 128 or Spectral Ghost 1024 -> vocoder/mask/ghost/morph -> formant, air, transient, width, output safety -> atomic analyzer taps.

## Low Latency 128 engine

- 128 log-spaced macro bands.
- Internal MIDI carrier with external-carrier blend.
- Per-band envelope following.
- Freeze envelope memory.
- Formant shift via safe envelope remapping.
- Mask, Ghost, and Morph modes.
- No heap allocation in `processBlock`.

## Spectral Ghost 1024 engine

- 1024-sample window.
- 256-sample hop.
- Hann window.
- Prepared storage only; no process-time resizing.
- 128 display/analysis bins.
- OLA-style spectral ghost reconstruction reference.

## Why two engines?

A best-in-world vocoder needs both low-latency musical response and high-resolution spectral resynthesis for frozen ghosts, morphing, and texture design.

## Known limitation — Spectral Ghost 1024 intraframe smear

`analyseBins()` assumes constant-frequency sinusoids within the 1024-sample frame (fixed `w = 2π*bandFreq/sr`). For vibrato-swept sinusoids captured by the freeze path, energy distributes across 2–3 adjacent log bands rather than a single bin. In freeze mode this smear is permanent — the frozen snapshot is blurred at capture and every OLA frame resynthesises that blur indefinitely.

The Low Latency 128 filterbank is not subject to this problem (per-sample envelope following, no frame assumption).

**Correction 2026-11-07 — the WASPAA correction is OUT, not the designated fix.** The Bristow-Johnson/Bogdanowicz β/λ intraframe sweep correction (WASPAA 2001) was piloted on 2026-11-06 (`Tests/FreezePilot.cpp`) against its pre-written rule (improve the frozen snapshot's peak concentration by ≥ 3 dB without degrading the stationary control by > 0.5 dB) and **failed**: best gain +0.47 dB on the frozen snapshot; the ARC Audio Bench cross-check measured 0.03 dB. The correction is therefore NOT the designated fix and is NOT future work for v0.7.0 — it stays out. Two further reasons the sentence above is stale: the Goertzel `analyseBins()` it names no longer exists (the rank-9 FFT cross-synthesis redesign replaced Goertzel analysis with modulator FFT), and part of the smear is interframe vibrato-extent averaging in the leaky frozen snapshot, which no per-frame β correction can remove. The freeze blur remains a known, bounded limitation (~5 dB peak-concentration loss on frozen vibrato, 4.8–6.2 dB); the actual fix path is the sinusoidal-model freeze (ProVox8-scale research), not WASPAA. See PAPER.md §5–6 for the full analysis and the pilot result.
