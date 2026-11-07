# FreeVox8 Production Audit

**v0.5.0** is a production-hardened source release candidate with a dual-engine vocoder architecture. It is not a certified public binary until local JUCE build, pluginval, DAW smoke tests, and release evidence pass.

## Improvements in 0.5.0

- Default low-latency engine upgraded to **128 log-spaced bands**.
- Added **Engine** selector: Low Latency 128 / Spectral Ghost 1024.
- Added high-resolution STFT/OLA reference backend scaffold in `FreeVox8SpectralEngine.h`.
- Preserved public parameter/session contract.
- Expanded completion map from MVP hardening to v1.0 certification gates.

## DSP paper and known limitations

`PAPER.md` documents the full architecture and formally records the known intraframe smear limitation in `FreeVox8SpectralEngine.analyseBins()`. Personal correspondence with Robert Bristow-Johnson (June 2026) confirmed the structural asymmetry between time-scaling and freeze-loop paths and the applicability of the WASPAA 2001 intraframe correction to the freeze resynthesis use case. **Correction 2026-11-07: the empirical verification has now run — and the correction failed.** The WASPAA β/λ pilot (`Tests/FreezePilot.cpp`, 2026-11-06) measured best +0.47 dB against its pre-written ≥ 3 dB rule (ARC Audio Bench cross-check 0.03 dB), so the correction stays out and is no longer future work. The Goertzel `analyseBins()` it would have slotted into no longer exists (rank-9 FFT redesign). The freeze blur remains a known, bounded limitation (~5 dB on frozen vibrato); the fix path is the sinusoidal-model freeze (ProVox8-scale research).

## Remaining certification gates

- compile on macOS/Windows/Linux with JUCE
- pluginval strictness 10
- host tests in Reaper, Ableton, and Logic where applicable
- buffer-size and sample-rate switching tests
- preset recall and automation tests
- CPU profiling of Spectral Ghost 1024
