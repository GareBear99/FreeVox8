# FreeVox8 World-Class Spectral Vocoder Spec

FreeVox8 is positioned as a spectral vocoder, ghost-freeze resynthesizer, and dynamic spectral masking engine.

## Locked v1 product pillars

1. Immediate musical vocoder tone.
2. Dynamic spectral masking.
3. Ghost freeze.
4. Morph mode.
5. Dual-engine quality: low-latency 128-band performance plus Spectral Ghost 1024 resynthesis.
6. RT safety first.

## Current implemented core

- 128 log-spaced macro bands.
- Internal carrier with saw/sine/pulse/sub blend.
- Optional sidechain modulator bus.
- Envelope freeze memory.
- Formant remap.
- Transient protect.
- Stereo width stage.
- Atomic display taps.
- Spectral Ghost 1024 reference backend.

## World-class path after v0.5.0

The next truly differentiating work is sound certification: compare against known vocoder references, refine consonant intelligibility, tune ghost freeze musicality, expand factory presets, optimize the STFT backend, and run pluginval/DAW tests until boringly stable.

**Correction 2026-11-07 — the intraframe sweep correction is NOT the highest-value DSP improvement, and it is NOT primary future work.** The Bristow-Johnson/Bogdanowicz β/λ estimation (WASPAA 2001) was piloted on 2026-11-06 (`Tests/FreezePilot.cpp`) against a pre-written ≥ 3 dB rule and **failed** (best gain +0.47 dB on the frozen snapshot; ARC Audio Bench cross-check 0.03 dB), so the correction stays out — it is not the correct fix and not designated for v0.7.0. The Goertzel `analyseBins()` it would have slotted into no longer exists (the rank-9 FFT cross-synthesis redesign replaced it with modulator FFT), and part of the smear is interframe vibrato-extent averaging that no per-frame β correction can remove. The freeze blur remains a known, bounded limitation (~5 dB peak-concentration loss on frozen vibrato); the actual fix path is the sinusoidal-model freeze (ProVox8-scale research). The highest-value DSP work after v0.5.0 is the sound certification above: competitor A/B, consonant intelligibility, ghost freeze musicality, presets, STFT backend optimisation, and pluginval/DAW tests.
