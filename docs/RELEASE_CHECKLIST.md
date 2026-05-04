# FreeVox8 Release Checklist

## Source package gate

- [ ] `python3 scripts/validate_repo.py` passes
- [ ] CMake configure passes with local JUCE
- [ ] VST3 builds
- [ ] AU builds on macOS
- [ ] Standalone builds
- [ ] No old FreeEQ8/ProEQ8 active target names remain
- [ ] README version matches `Source/Config.h`
- [ ] Changelog updated

## Audio gate

- [ ] Bypass is click-free
- [ ] Parameters are smoothed enough for normal automation
- [ ] No denormals detected
- [ ] No heap allocation in audio callback during normal playback
- [ ] Sidechain absent path works
- [ ] Sidechain present path works
- [ ] MIDI carrier responds to note-on
- [ ] Freeze does not pop badly

## Host gate

- [ ] pluginval basic pass
- [ ] Reaper VST3 smoke test
- [ ] Ableton VST3 smoke test
- [ ] Logic AU smoke test
- [ ] Standalone launches

## Public release gate

- [ ] Demo audio rendered
- [ ] Screenshots exported
- [ ] GitHub release notes finalized
- [ ] Manual drafted
- [ ] Known limitations documented
