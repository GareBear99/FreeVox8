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

The single highest-value DSP improvement identified is the **intraframe sweep correction** for the Spectral Ghost 1024 freeze path (PAPER.md §5–6). The current `analyseBins()` assumes stationary sinusoids within the 1024-sample frame — a known limitation that blurs the frozen spectral snapshot for vibrato-swept pitched material. The Bristow-Johnson/Bogdanowicz β/λ estimation (WASPAA 2001) is the correct fix and is designated as primary future work for v0.7.0.
