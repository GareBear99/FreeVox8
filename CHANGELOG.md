# Changelog

## 0.5.0 - 2026-11-07 (Ghost FFT redesign + docs)

- Ghost FFT cross-synthesis redesign (rank 9, Decision 5): replaced the Goertzel `analyseBins()` with windowed-FFT `analyseRing()` and true per-frame spectral cross-synthesis — all 9 resynthesis checks pass, silence in gives silence out, Ghost STOI 0.7939 beats TAL-Vocoder 3.0.3 (0.7434); Ghost CPU ~2.5% at 44.1 kHz (was ~39%).
- WASPAA β/λ intraframe sweep correction: pilot ran 2026-11-06 and FAILED its pre-written ≥ 3 dB rule (best +0.47 dB; ARC Audio Bench cross-check 0.03 dB) — the correction stays out and is NOT future work; the freeze-blur fix path is the sinusoidal-model freeze (ProVox8-scale research).
- Corrected stale WASPAA docs (the correction was presented as the designated / highest-value / primary-future-work fix) in PAPER.md, the DSP architecture, world-class spec, and production audit docs.
- Regenerated the release-evidence manifest (60 files, 2026-11-07).
- Ghost STOI: treat as ~0.78–0.79 with a ~0.014 run-to-run spread (a second independent render measured 0.7796); the 0.7939 overall and the TAL-beating conclusion stand.

## 0.5.0 - Dual-engine spectral vocoder RC

- Upgraded default realtime engine from 64 to 128 log-spaced bands.
- Added Engine selector: Low Latency 128 and Spectral Ghost 1024.
- Added `FreeVox8SpectralEngine.h` high-resolution STFT/OLA reference backend.
- Expanded factory presets to 24 entries.
- Updated docs, manifest, validation, and release evidence rules for dual-engine production certification.

## 0.5.0 - Production hardening RC

- Upgraded realtime core from 32 bands to 128 log-spaced macro bands.
- Added Transient Protect, Stereo Width, and Quality parameters.
- Expanded UI to expose production macros and a 128-bin spectral display.
- Added world-class product spec, DSP golden tests, and stricter production completion map.
- Updated factory preset manifest to 10 presets.
- Preserved no-allocation/no-lock/no-file-I/O realtime DSP doctrine.


## 0.5.0

- Upgraded FreeVox8 from the 16-band v0.2 scaffold to a 32-band production source release candidate.
- Fixed reset path from the previous seed engine so all actual filter states reset correctly.
- Removed realtime coefficient rebuild behavior: formant shift now performs RT-safe envelope remapping.
- Added fixed-band filter preparation outside `processBlock`.
- Expanded analyzer display from 8 bars to 16 bins.
- Added Proto-Synth inspired spectral grid visual language.
- Added Voxel Audio inspired low-weight UI/display doctrine.
- Added integration docs for the uploaded Proto-Synth and Voxel Audio references.
- Hardened validation script against accidental old product identity drift.

## 0.1.0

- Created standalone FreeVox8 source package from FreeEQ8 base hygiene.
- Added first JUCE target, processor/editor, DSP scaffold, presets, and release docs.
