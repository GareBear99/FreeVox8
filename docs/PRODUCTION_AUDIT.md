# FreeVox8 Production Audit

**v0.5.0** is a production-hardened source release candidate with a dual-engine vocoder architecture. It is not a certified public binary until local JUCE build, pluginval, DAW smoke tests, and release evidence pass.

## Improvements in 0.5.0

- Default low-latency engine upgraded to **128 log-spaced bands**.
- Added **Engine** selector: Low Latency 128 / Spectral Ghost 1024.
- Added high-resolution STFT/OLA reference backend scaffold in `FreeVox8SpectralEngine.h`.
- Preserved public parameter/session contract.
- Expanded completion map from MVP hardening to v1.0 certification gates.

## Remaining certification gates

- compile on macOS/Windows/Linux with JUCE
- pluginval strictness 10
- host tests in Reaper, Ableton, and Logic where applicable
- buffer-size and sample-rate switching tests
- preset recall and automation tests
- CPU profiling of Spectral Ghost 1024
