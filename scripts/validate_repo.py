#!/usr/bin/env python3
from pathlib import Path
import sys, json

root = Path(__file__).resolve().parents[1]
required = [
    "CMakeLists.txt", "README.md", "CHANGELOG.md", "RELEASE_MANIFEST.json",
    "Source/PluginProcessor.cpp", "Source/PluginProcessor.h",
    "Source/PluginEditor.cpp", "Source/PluginEditor.h",
    "Source/DSP/FreeVox8DSP.h", "Source/DSP/FreeVox8SpectralEngine.h", "Source/UI/SpectralDisplay.h",
    "docs/PRODUCTION_AUDIT.md", "docs/FREEVOX8_DSP_ARCHITECTURE.md",
    "docs/PROTO_SYNTH_VOXEL_INTEGRATION.md", "docs/RELEASE_CHECKLIST.md",
    "docs/ARC_GOVERNANCE_RELEASE_MODEL.md", "docs/PLUGINVAL_AND_DAW_TEST_PLAN.md",
    "docs/PRODUCTION_COMPLETION_MAP.md",
    "docs/WORLD_CLASS_SPECTRAL_VOCODER_SPEC.md", "docs/DSP_GOLDEN_TESTS.md",
    "presets/FreeVox8_factory_presets.json",
    "scripts/make_evidence_pack.py", "scripts/smoke_source.sh", "scripts/certify_local_release.sh",
]

missing = [p for p in required if not (root / p).exists()]
if missing:
    print("Missing required files:")
    for p in missing:
        print(" -", p)
    sys.exit(1)

cmake = (root / "CMakeLists.txt").read_text(errors="ignore")
if "project(FreeVox8 VERSION 0.5.0" not in cmake:
    print("CMake version is not 0.5.0")
    sys.exit(1)
if "juce_add_plugin(FreeVox8" not in cmake:
    print("FreeVox8 target missing")
    sys.exit(1)
if "juce_add_plugin(FreeEQ8" in cmake or "juce_add_plugin(ProEQ8" in cmake:
    print("Old EQ plugin target still active")
    sys.exit(1)

config = (root / "Source/Config.h").read_text(errors="ignore")
if 'FREEVOX8_VERSION "0.5.0"' not in config:
    print("Config version mismatch")
    sys.exit(1)
if "kNumMacroBands = 128" not in config:
    print("Config macro-band count mismatch")
    sys.exit(1)

dsp = (root / "Source/DSP/FreeVox8DSP.h").read_text(errors="ignore")
for needle in ["kNumBands = 128", "FreeVox8SpectralEngine", "engineMode", "buildFixedBandFilters", "makeBandEdge", "readShiftedEnvelope", "transientProtect", "stereoWidth", "quality", "process("]:
    if needle not in dsp:
        print(f"DSP expected marker missing: {needle}")
        sys.exit(1)
for banned in ["malloc", "free(", "fstream", "ofstream", "ifstream", "std::thread"]:
    if banned in dsp:
        print(f"DSP banned realtime-risk marker found: {banned}")
        sys.exit(1)

spectral = (root / "Source/DSP/FreeVox8SpectralEngine.h").read_text(errors="ignore")
for needle in ["kFftSize = 1024", "kHopSize = 256", "renderFrame", "analyseBins", "Spectral Ghost 1024"]:
    if needle not in spectral:
        print(f"Spectral backend marker missing: {needle}")
        sys.exit(1)
processor = (root / "Source/PluginProcessor.cpp").read_text(errors="ignore")
if '"engine"' not in processor or "Spectral Ghost 1024" not in processor:
    print("Engine selector parameter is missing")
    sys.exit(1)

presets = json.loads((root / "presets/FreeVox8_factory_presets.json").read_text())
if presets.get("version") != "0.5.0" or len(presets.get("presets", [])) < 24:
    print("Preset manifest is not v0.5.0 with at least 24 presets")
    sys.exit(1)

manifest = json.loads((root / "RELEASE_MANIFEST.json").read_text())
if manifest.get("product") != "FreeVox8" or manifest.get("version") != "0.5.0":
    print("Release manifest identity mismatch")
    sys.exit(1)

allow = {"docs/FreeEQ8_BASE_README.md", "scripts/validate_repo.py"}
for path in root.rglob("*"):
    if not path.is_file() or path.relative_to(root).as_posix() in allow:
        continue
    if any(part in {".git", "build", "JUCE", "release_evidence"} for part in path.relative_to(root).parts):
        continue
    text = path.read_text(errors="ignore")
    if "juce_add_plugin(FreeEQ8" in text or "juce_add_plugin(ProEQ8" in text:
        print(f"Old plugin target text found in {path.relative_to(root)}")
        sys.exit(1)

print("FreeVox8 v0.5.0 source-package validation passed.")
