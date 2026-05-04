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
