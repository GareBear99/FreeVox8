# FreeVox8 Pluginval and DAW Test Plan

## Required preflight

```bash
python3 scripts/validate_repo.py
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR=/path/to/JUCE
cmake --build build --config Release
```

## Pluginval

Run the built VST3 through pluginval at strictness 8 or higher.

```bash
pluginval --strictness-level 8 --validate-in-process path/to/FreeVox8.vst3
```

Pass criteria:

- no crashes
- no parameter serialization failure
- no invalid bus layout failure
- no editor open/close crash
- no process precision failure

## DAW smoke test matrix

Minimum manual smoke tests before public binary release:

| Host | Format | Tests |
|---|---|---|
| REAPER | VST3 | load, save, automate Mix/Formant, sidechain, MIDI note input |
| Ableton Live | VST3/AU | audio track load, sidechain routing, MIDI carrier routing |
| Logic Pro | AU | scan, load, save project, reopen project |
| Standalone | App | audio device open/close, MIDI input, preset changes |

## Audio regression test ideas

- 1 kHz sine does not explode output.
- Vocal sample into Vocoder mode produces carrier-shaped output.
- Sidechain in Mask mode reduces target energy.
- Freeze captures and holds envelope without clicking.
- Automation sweep of Formant Shift does not rebuild filters or glitch catastrophically.
