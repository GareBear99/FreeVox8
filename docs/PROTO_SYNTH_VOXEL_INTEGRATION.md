# FreeVox8 Integration Notes: Proto-Synth + Voxel Audio

This pass used the uploaded Proto-Synth Grid Engine and Voxel Audio packages as design references, not as copied runtime dependencies.

## Proto-Synth role

Proto-Synth informs the **visual language** of FreeVox8:

- blueprint/grid spectral display
- neural/spectral contour feeling
- low-weight geometry over image-heavy UI
- future morph view that can show carrier, modulator, frozen bins, and mask curves

The plugin should not embed the large browser prototype directly. The JUCE UI should keep the same doctrine in native code: simple shapes, atomic display taps, no UI locks in audio.

## Voxel Audio role

Voxel Audio informs the **operator and reliability doctrine**:

- local-first behavior
- low-weight visualization
- no overlapping panels
- clear validation/error policy
- export/preview discipline translated here as plugin release/test discipline
- selected color / spectral identity concepts for future presets and visual themes

Browser export logic, vault logic, OAuth/Stripe logic, and MP4 code are intentionally not imported into this audio plugin.

## What was implemented in v0.2

- 16-bin native spectral display replacing the earlier 8-bin display.
- Blueprint-grid analyzer background inspired by Proto-Synth.
- UI display remains read-only and uses processor atomic bin taps.
- DSP core remains independent of the UI and visual code.
- New docs describe how these projects influence FreeVox8 without creating dependency bloat.

## Future visual roadmap

1. Add three analyzer overlays:
   - Modulator envelope
   - Carrier energy
   - Frozen ghost bins
2. Add theme presets:
   - Classic FreeVox8
   - Proto Grid
   - Voxel Neon
   - AETHER Ghost
3. Add a collapse/compact UI mode for small laptop screens.
4. Add a strict `UI does not allocate per frame` audit once the look is finalized.
