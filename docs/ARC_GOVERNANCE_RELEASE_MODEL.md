# FreeVox8 ARC-Style Release Governance

FreeVox8 borrows the governance pattern from the ARC/Arc-RAR/Omnibinary ecosystem without adding those systems to the audio runtime. The plugin must stay local, deterministic, and realtime safe. Governance belongs in release evidence, manifests, and validation scripts.

## Rules

1. Audio thread authority is narrow: read parameters, process samples, write atomic display taps.
2. No packaging, receipt generation, filesystem IO, networking, or manifest work may touch `processBlock`.
3. Every release must include a manifest, validation output, build notes, and known limitations.
4. Every DSP backend change must preserve parameter IDs unless a major-version migration doc is added.
5. Claims must match evidence: source package, build candidate, pluginval pass, DAW tested, or public binary.

## Release lanes

- **Floor:** last known stable source package.
- **Candidate:** current improved branch.
- **Promoted:** candidate after validation, local build, pluginval, and DAW smoke tests.

## Why this matters

A spectral plugin can sound impressive but still fail production if it allocates, blocks, denormal-spikes, or lies about readiness. The release model prevents hype from outrunning the actual engineering state.
