# Changelog

## 0.5.1 - Documentation + DSP paper

- Added `PAPER.md` — full academic-style DSP architecture paper covering dual-engine design, Low Latency 128 filterbank, Spectral Ghost 1024 STFT/OLA, intraframe smear limitation, Bristow-Johnson/Bogdanowicz correction (WASPAA 2001, ref [4]), real-time safety architecture, production gate, and full references.
- Documented intraframe smear problem in `analyseBins()` (§5 of PAPER.md): fixed-frequency Goertzel accumulation distributes energy across 2–3 adjacent log bands for vibrato-swept sinusoids captured mid-swing during freeze. Effect is permanent in freeze-loop path.
- Recorded personal correspondence with Robert Bristow-Johnson (June 2026) confirming the freeze-loop vs. time-scaling structural asymmetry and applicability of WASPAA 2001 eq. 9–11 to the freeze resynthesis use case. Cited as ref [10] in PAPER.md.
- Rewrote README.md with FreeVox8-native content: dual-engine architecture diagram, feature table, comparison table, roadmap with SIMD and intraframe correction milestones, full docs index.
- Updated docs index to reference PAPER.md.

## 0.5.0 - Dual-engine spectral vocoder RC

- Upgraded default realtime engine from 64 to 128 log-spaced bands.
- Added Engine selector: Low Latency 128 and Spectral Ghost 1024.
- Added `FreeVox8SpectralEngine.h` high-resolution STFT/OLA reference backend.
- Expanded factory presets to 24 entries.
- Updated docs, manifest, validation, and release evidence rules for dual-engine production certification.

## 0.5.0 - Production hardening RC

- Upgraded realtime core from 32 bands to 128 log-spaced macro bands.
- Added Transient Protect, Stereo Width, and Quality parameters.
- Expanded UI to expose production macros and a 128-bin spectral display.
- Added world-class product spec, DSP golden tests, and stricter production completion map.
- Updated factory preset manifest to 10 presets.
- Preserved no-allocation/no-lock/no-file-I/O realtime DSP doctrine.


## 0.5.0

- Upgraded FreeVox8 from the 16-band v0.2 scaffold to a 32-band production source release candidate.
- Fixed reset path from the previous seed engine so all actual filter states reset correctly.
- Removed realtime coefficient rebuild behavior: formant shift now performs RT-safe envelope remapping.
- Added fixed-band filter preparation outside `processBlock`.
- Expanded analyzer display from 8 bars to 16 bins.
- Added Proto-Synth inspired spectral grid visual language.
- Added Voxel Audio inspired low-weight UI/display doctrine.
- Added integration docs for the uploaded Proto-Synth and Voxel Audio references.
- Hardened validation script against accidental old product identity drift.

## 0.1.0

- Created standalone FreeVox8 source package from FreeEQ8 base hygiene.
- Added first JUCE target, processor/editor, DSP scaffold, presets, and release docs.
