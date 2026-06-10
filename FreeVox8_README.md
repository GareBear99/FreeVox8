<div align="center">
  <img src="https://capsule-render.vercel.app/api?type=waving&height=200&color=0:0a0b12,50:2d1b69,100:7c3aed&text=FreeVox8&fontSize=52&fontColor=e8eaf0&animation=fadeIn&fontAlignY=36&desc=Dual-Engine%20Spectral%20Vocoder%20%26%20Ghost%20Resynthesizer&descSize=18&descColor=a0a6bc&descAlignY=58" width="100%">
</div>

<div align="center">
  <img src="https://img.shields.io/badge/Version-0.5.0-7c3aed?style=flat" alt="Version" />
  <img src="https://img.shields.io/badge/License-GPL--3.0-3b82f6?style=flat&logo=gnu&logoColor=white" alt="GPL-3.0" />
  <img src="https://img.shields.io/badge/Formats-VST3%20%7C%20AU%20%7C%20Standalone-6c7bbd?style=flat" alt="Formats" />
  <img src="https://img.shields.io/badge/Platform-macOS%20%7C%20Windows%20%7C%20Linux-94a3b8?style=flat" alt="Platform" />
  <img src="https://img.shields.io/badge/JUCE-7.0.12-f59e0b?style=flat" alt="JUCE 7.0.12" />
  <img src="https://img.shields.io/badge/Bands-128%20log--spaced-22c55e?style=flat" alt="128 bands" />
</div>

<br>

> 🎛️ Part of the [TizWildin Plugin Ecosystem](https://garebear99.github.io/TizWildinEntertainmentHUB/) — 20+ free audio plugins with a live update dashboard.
>
> [FreeEQ8](https://github.com/GareBear99/FreeEQ8) · [XyloCore](https://github.com/GareBear99/XyloCore) · [Instrudio](https://github.com/GareBear99/Instrudio) · [Therum](https://github.com/GareBear99/Therum_JUCE-Plugin) · [BassMaid](https://github.com/GareBear99/BassMaid) · [SpaceMaid](https://github.com/GareBear99/SpaceMaid) · [GlueMaid](https://github.com/GareBear99/GlueMaid) · [MixMaid](https://github.com/GareBear99/MixMaid) · [MultiMaid](https://github.com/GareBear99/MultiMaid) · [MeterMaid](https://github.com/GareBear99/MeterMaid) · [ChainMaid](https://github.com/GareBear99/ChainMaid) · [PaintMask](https://github.com/GareBear99/PaintMask_Free-JUCE-Plugin) · [WURP](https://github.com/GareBear99/WURP_Toxic-Motion-Engine_JUCE) · [AETHER](https://github.com/GareBear99/AETHER_Choir-Atmosphere-Designer) · [WhisperGate](https://github.com/GareBear99/WhisperGate_Free-JUCE-Plugin) · [RiftWave](https://github.com/GareBear99/RiftWaveSuite_RiftSynth_WaveForm_Lite) · [FreeSampler](https://github.com/GareBear99/FreeSampler_v0.3) · [VF-PlexLab](https://github.com/GareBear99/VF-PlexLab) · [PAP-Forge-Audio](https://github.com/GareBear99/PAP-Forge-Audio)
>
> 🎧 **SoundCloud:** [TizWildin on SoundCloud](https://soundcloud.com/tizwildin) — original music, remixes, VIP mixes, and experimental drops
>
> ▶️ **[YouTube](https://www.youtube.com/@garebearproductionz)** — music, visuals, demos, and releases
> 🌊 **[Voxel Audio](https://github.com/GareBear99/Voxel_Audio)** — free RGB waveform visualizer used in TizWildin YouTube visuals
> 🎵 **[All Platforms](https://ffm.bio/no4km87)** — Spotify, Apple Music, and more

---

**FreeVox8** is a production-grade spectral vocoder, ghost-freeze resynthesizer, and dynamic spectral masking plugin — free and open-source (GPL-3.0). Built with JUCE/C++ for VST3, AU, and Standalone.

The architecture employs a dual-engine design: a **128-band log-spaced filterbank** for low-latency musical vocoding and a **Spectral Ghost 1024** STFT/OLA backend for high-resolution ghost, freeze, and morph resynthesis. Both engines enforce allocation-free, lock-free audio processing.

> **"Great sound shouldn't cost anything"**

## ⬇️ Status

FreeVox8 v0.5.0 is a **production-hardened source release candidate**. The DSP architecture, dual-engine design, factory presets, and release documentation are complete. Public binary certification requires local JUCE build, pluginval strictness-10 pass, and DAW smoke tests. See [Production Gate](#-production-gate).

## ✨ Features

### Dual-Engine Architecture
- **Low Latency 128** — 128 log-spaced bands, real-time safe, default production path
- **Spectral Ghost 1024** — 1024-sample STFT/OLA, high-resolution ghost/freeze/morph resynthesis
- Engine selector parameter switches paths without breaking session state

### Modes
| Mode | Description |
|------|-------------|
| **Vocoder** | Classic carrier × modulator envelope spectral vocoder |
| **Mask** | Spectral pocket carving — removes frequency content matching the modulator |
| **Ghost** | Freeze-enhanced vocoder with spectral ghost overlay |
| **Morph** | Nonlinear blend of vocoded and dry signal via tanh saturation |

### Signal Path
- MIDI-controlled internal carrier: saw / sine / pulse / sub blend
- External-carrier blend from main input
- Optional sidechain modulator bus
- Freeze envelope memory (Low Latency 128 + Spectral Ghost 1024)
- Formant shift via RT-safe envelope remapping (no coefficient rebuilds)
- Air / noise excitation for breath and intelligibility
- Transient Protect macro for consonants and punch
- Stereo Width macro
- Quality macro for higher-density tone shaping
- Atomic spectral display taps (128 bins, lock-free)

### 24 Factory Presets
Classic Robot Clean · Ghost Choir Freeze · Alien Formant Down · Bright Talkbox Air · Vocal Pocket Mask · Deep Spectral Morph · Frozen Pad Wide · Sub Carrier Monster · and 16 more.

## 🆚 How FreeVox8 Compares

| Feature | **FreeVox8** | iZotope VocalSynth 2 | Roland VP-7 (hardware) | Antares AVOX |
|---------|:-----------:|:--------------------:|:---------------------:|:------------:|
| **Price** | **Free** | $199 | ~$800 | $99+ |
| **Open Source** | **GPL-3.0** | — | — | — |
| Bands | **128** | multi-band | ~12 | proprietary |
| Filterbank + STFT dual | **✓** | — | — | — |
| Ghost/Freeze mode | **✓** | — | — | — |
| Spectral Mask mode | **✓** | ✓ | — | — |
| Morph mode | **✓** | limited | — | — |
| MIDI carrier + external blend | **✓** | ✓ | ✓ | limited |
| Formant shift (RT-safe) | **✓** | ✓ | ✓ | ✓ |
| Sidechain modulator bus | **✓** | ✓ | — | — |
| Allocation-free audio thread | **✓** | unknown | N/A | unknown |
| Formats | VST3, AU | VST3, AU, AAX | hardware | VST3, AU, AAX |

> **FreeVox8 is the only free open-source spectral vocoder with dual-engine (filterbank + STFT), ghost-freeze resynthesis, spectral masking, and morph modes in a single allocation-free plugin.**

## 🏗️ Architecture

```
Main Input / Sidechain ──→ Parameter Snapshot ──→ Engine Selector
                                                         │
                          ┌──────────────────────────────┴──────────────────────────┐
                          │                                                          │
               Low Latency 128                                        Spectral Ghost 1024
         (128 log-spaced bands)                                  (1024-pt STFT / OLA)
                          │                                                          │
           Per-band IIR envelope follower                       analyseBins() — Goertzel DFT
           Freeze memory (running average)                      Freeze snapshot (single frame)
           Carrier synthesis (saw/sine/pulse/sub)               OLA resynthesis (256-hop, Hann)
           Vocoder / Mask / Ghost / Morph modes                 Vocoder / Ghost / Morph modes
           Formant shift (envelope remap)                       Formant shift (envelope remap)
           Air / Transient / Width / Quality                    Air / Clarity / Width
                          │                                                          │
                          └──────────────────────────────┬──────────────────────────┘
                                                         │
                                    Output Safety → Atomic Display Taps → UI
```

See [PAPER.md](PAPER.md) for the full DSP architecture paper including the known intraframe smear limitation in Spectral Ghost 1024 and the planned Bristow-Johnson/Bogdanowicz correction.

## 🔬 DSP Paper

[**PAPER.md**](PAPER.md) — Full academic-style architecture paper covering:
- Dual-engine design rationale
- Low Latency 128 filterbank implementation (band layout, carrier synthesis, envelope analysis, freeze, modes, formant shift)
- Spectral Ghost 1024 STFT/OLA implementation
- Known intraframe smear limitation and the Bristow-Johnson/Bogdanowicz correction (WASPAA 2001)
- Real-time safety architecture
- Production gate requirements
- Correspondence with Robert Bristow-Johnson (June 2026)
- Full references

## 🔧 Build

JUCE is intentionally not vendored. Add as `./JUCE` or pass `-DJUCE_DIR`.

```bash
git submodule add https://github.com/juce-framework/JUCE.git JUCE
git -C JUCE checkout 7.0.12
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### Validate source package

```bash
python3 scripts/validate_repo.py
```

## 🚦 Production Gate

FreeVox8 v1.0.0 public binary requires:

1. Release build on macOS (Universal), Linux (x86_64), Windows (x64)
2. pluginval strictness-level-10 pass for VST3 + AU
3. DAW smoke tests: Ableton Live, REAPER, Logic Pro
4. Golden test suite pass (`docs/DSP_GOLDEN_TESTS.md`)
5. 24 factory presets — save/restore verified
6. Signed/notarized installer or clearly labeled unsigned build
7. Release evidence pack (`scripts/make_evidence_pack.py`)

## 📋 Docs

| Doc | Description |
|-----|-------------|
| [PAPER.md](PAPER.md) | Full DSP architecture paper |
| [docs/FREEVOX8_DSP_ARCHITECTURE.md](docs/FREEVOX8_DSP_ARCHITECTURE.md) | Dual-engine summary |
| [docs/WORLD_CLASS_SPECTRAL_VOCODER_SPEC.md](docs/WORLD_CLASS_SPECTRAL_VOCODER_SPEC.md) | Product pillars and v1 path |
| [docs/DSP_GOLDEN_TESTS.md](docs/DSP_GOLDEN_TESTS.md) | Release gate test suite |
| [docs/PLUGINVAL_AND_DAW_TEST_PLAN.md](docs/PLUGINVAL_AND_DAW_TEST_PLAN.md) | pluginval + DAW test matrix |
| [docs/PRODUCTION_AUDIT.md](docs/PRODUCTION_AUDIT.md) | v0.5.0 audit and remaining gates |
| [docs/PRODUCTION_COMPLETION_MAP.md](docs/PRODUCTION_COMPLETION_MAP.md) | Completion status map |
| [docs/ARC_GOVERNANCE_RELEASE_MODEL.md](docs/ARC_GOVERNANCE_RELEASE_MODEL.md) | Release governance rules |
| [docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md) | Per-release checklist |

## 🛣️ Roadmap

### v0.5.0 (Current — Source RC)
- [x] 128-band Low Latency engine
- [x] Spectral Ghost 1024 STFT/OLA backend
- [x] Dual-engine selector
- [x] MIDI carrier (saw/sine/pulse/sub blend)
- [x] Sidechain modulator bus
- [x] Vocoder / Mask / Ghost / Morph modes
- [x] Freeze envelope memory (both engines)
- [x] Formant shift (RT-safe envelope remap)
- [x] Air / Transient Protect / Stereo Width / Quality macros
- [x] 128-bin atomic spectral display
- [x] 24 factory presets
- [x] ARC governance, golden tests, production audit docs

### v0.6.0 — SIMD + Performance
- [ ] AVX2/SSE2 vectorisation of `analyseBins()` inner loop
- [ ] CPU profiling report for Spectral Ghost 1024
- [ ] Pluginval strictness-10 CI integration

### v0.7.0 — Intraframe Sweep Correction
- [ ] Bristow-Johnson/Bogdanowicz β/λ estimation in `analyseBins()` (PAPER.md §6.1)
- [ ] Empirical freeze smear A/B test (vibrato vocal, with/without correction)
- [ ] Phase vocoder backend scaffold

### v1.0.0 — Public Binary Release
- [ ] Full production gate (all items in §8 of PAPER.md)
- [ ] Demo audio rendered
- [ ] Signed/notarized installers

### ProVox8 (Roadmap)
- [ ] 256-band filterbank
- [ ] Intraframe sweep correction
- [ ] Wavetable + noise-shaped carrier modes
- [ ] Spectral Dynamics mode (per-bin threshold clamping)

## 🤝 Contributing

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/your-feature`
3. Commit your changes: `git commit -m 'Add your feature'`
4. Push: `git push origin feature/your-feature`
5. Open a Pull Request

All DSP changes must pass the golden test suite before merge.

## 📝 License

GNU General Public License v3.0 — see [LICENSE](LICENSE).

JUCE has its own licensing requirements. For commercial use, see [JUCE Licensing](https://juce.com/discover/licensing).

## ⚠️ Legal Notice

FreeVox8 is an **original implementation**. It is not affiliated with, derived from, or endorsed by iZotope, Roland, Antares, or any other commercial vocoder vendor. Built using publicly documented DSP algorithms.

## 🙏 Acknowledgments

- **JUCE Framework** — cross-platform audio plugin infrastructure
- **Robert Bristow-Johnson** — RBJ Audio EQ Cookbook [FreeEQ8]; WASPAA 2001 intraframe paper [FreeVox8]; personal correspondence June 2026
- **Andy Simper (Cytomic)** — Simper SVF topology (FreeEQ8 ProEQ8 tier)
- **Voxel Audio** — operator and display doctrine
- **Proto-Synth Grid Engine** — spectral visual language

## 💖 Support

FreeVox8 is free and open source.

<a href="https://github.com/sponsors/GareBear99"><img src="https://img.shields.io/badge/sponsor-GitHub%20Sponsors-ea4aaa?logo=githubsponsors&style=for-the-badge" alt="GitHub Sponsors"></a>
<a href="https://buymeacoffee.com/garebear99"><img src="https://img.shields.io/badge/Buy%20Me%20a%20Coffee-ffdd00?logo=buy-me-a-coffee&logoColor=black&style=for-the-badge" alt="Buy Me a Coffee"></a>
<a href="https://ko-fi.com/luciferai"><img src="https://img.shields.io/badge/Ko--fi-ff5e5b?logo=ko-fi&logoColor=white&style=for-the-badge" alt="Ko-fi"></a>

- ⭐ Star this repo
- 🐛 [Report bugs](https://github.com/GareBear99/FreeVox8/issues)
- 📣 Tell a producer friend

## 📧 Contact

- **Issues**: [GitHub Issues](https://github.com/GareBear99/FreeVox8/issues)
- **Email**: neovectr.inc@gmail.com

---

**Other Projects**

[TizWildinEntertainmentHUB](https://github.com/GareBear99/TizWildinEntertainmentHUB) · [FreeEQ8](https://github.com/GareBear99/FreeEQ8) · [awesome-audio-plugins-dev](https://github.com/GareBear99/awesome-audio-plugins-dev) · [Voxel Audio](https://github.com/GareBear99/Voxel_Audio)

**Built with ❤️ by Gary Doman (GareBear99 / TizWildin)**

*"Great sound shouldn't cost anything"*

<p align="center">
  <img src="https://capsule-render.vercel.app/api?type=waving&section=footer&height=140&color=0:0a0b12,50:2d1b69,100:7c3aed" alt="footer" />
</p>
