# FreeVox8 DSP Golden Tests

These tests define the release gate before public binaries.

## Must pass

- Silence in, silence or intentional noise-air only when Air > 0.
- No NaN/Inf at all parameter extremes.
- Freeze toggling produces no clicks above acceptable threshold.
- Host bypass returns bit-stable dry signal path.
- MIDI note changes do not cause denormal spikes or phase explosions.
- Sidechain absent, mono, stereo, and disabled layouts all remain stable.
- 44.1, 48, 88.2, 96, and 192 kHz smoke tested.
- Buffer sizes: 1, 16, 32, 64, 128, 512, 1024, 2048 samples.
- pluginval strictness 10 passes for VST3/AU.

## Musical acceptance

- Vocoder mode intelligible on spoken vocal at 128 bands.
- Mask mode creates audible spectral pocket without broadband pumping.
- Ghost mode holds recognizable spectral character.
- Morph mode remains bounded and does not clip under loud carriers.
