# FreeVox8

**FreeVox8** is a JUCE-based spectral vocoder, ghost-freeze resynthesis, morph, and dynamic spectral masking plugin for vocals, synths, drums, and experimental sound design.

This repository is a standalone plugin source package derived from the FreeEQ8 codebase hygiene patterns, not an EQ rename. The audio path is built around a dual engine: a conservative **128-band realtime-safe spectral vocoder core** plus a **Spectral Ghost 1024** high-resolution backend scaffold for ghost/freeze and morph resynthesis.

## Current feature set

- 128-band log-spaced spectral envelope vocoder core
- Spectral Ghost 1024 high-resolution engine option
- MIDI-controlled internal carrier with saw/sine/pulse/sub blend
- External-carrier blend from the main input
- Optional sidechain modulator bus
- Vocoder, Mask, Ghost, and Morph modes
- Freeze envelope memory for ghost/frozen textures
- Formant shift via RT-safe envelope remapping
- Air/noise excitation for intelligibility and breath
- Transient Protect macro for consonants and punch
- Stereo Width macro
- Quality macro for higher-density tone shaping
- Atomic spectral display taps for low-cost UI visualization
- Source validation, release manifest, evidence packaging, and production test docs

## Build

JUCE is intentionally not vendored in this source package. Add JUCE as `./JUCE` or pass `-DJUCE_DIR`.

```bash
git submodule add https://github.com/juce-framework/JUCE.git JUCE
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Validate source package

```bash
python3 scripts/validate_repo.py
```

## Production gate

This package is source-complete for local JUCE compilation, but public binary release certification still requires:

1. Release build on target OS.
2. pluginval strictness 10.
3. DAW smoke tests in Ableton/Reaper/Logic where applicable.
4. Factory preset/session restore checks.
5. Signed/notarized installer or clearly labeled unsigned build.
6. Attached release evidence pack.

See `docs/DSP_GOLDEN_TESTS.md`, `docs/PLUGINVAL_AND_DAW_TEST_PLAN.md`, and `docs/PRODUCTION_COMPLETION_MAP.md`.

## Positioning

https://ffm.bio/no4km87

> FreeVox8 — spectral vocoder, ghost resynthesis, and dynamic vocal masking.

SEO keywords: free vocoder plugin, spectral vocoder, vocal resynthesis, vocoder VST3, JUCE vocoder plugin, spectral masking plugin, formant shifter plugin, robot voice plugin, ghost vocal effect, open source audio plugin.
