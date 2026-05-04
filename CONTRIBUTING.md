# Contributing to FreeVox8

FreeVox8 is a JUCE/CMake spectral vocoder and ghost-resynthesis plugin.

## Development rules

- Do not allocate memory in `processBlock`.
- Do not add file IO, network IO, locks, sleeps, or preset writes to the audio thread.
- Keep parameters backward-compatible once public releases begin.
- Test parameter changes while audio is running.
- Prefer small, reviewable DSP changes.

## Build

```bash
cmake -S . -B build -DJUCE_DIR=/path/to/JUCE -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

## Pull requests

Include the host/OS tested, sample rates tested, and whether sidechain and MIDI carrier routing were tested.
