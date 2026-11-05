#!/usr/bin/env bash
# Build the STOI corpus renderer headlessly (same approach as
# build_dspverify.sh): compile RenderCorpus.cpp plus the JUCE modules
# it needs directly. Usage: Tests/build_render.sh [JUCE_DIR] [BANDS]
#   BANDS defaults to 128 (the production band count). Pass 32 or 16
#   for the reduced-band STOI runs (paper §4.2): the build defines
#   FREEVOX8_NUM_BANDS, which re-spaces the log filterbank over the
#   same 55 Hz–18.5 kHz range, and writes a suffixed binary
#   (FreeVox8_RenderCorpus_32) so the 128-band binary is untouched.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JUCE_DIR="${1:-${JUCE_DIR:-}}"
BANDS="${2:-${FREEVOX8_BANDS:-128}}"
if [ -z "$JUCE_DIR" ]; then
    if [ -d "$ROOT/../JUCE-8.0.5" ]; then JUCE_DIR="$ROOT/../JUCE-8.0.5";
    elif [ -d "$ROOT/JUCE" ]; then JUCE_DIR="$ROOT/JUCE";
    else echo "JUCE not found: pass JUCE_DIR" >&2; exit 1; fi
fi
if [ "$BANDS" = "128" ]; then OUT="$ROOT/build-test/FreeVox8_RenderCorpus";
else OUT="$ROOT/build-test/FreeVox8_RenderCorpus_$BANDS"; fi
mkdir -p "$ROOT/build-test"
g++ -std=c++17 -O2 -DNDEBUG \
    -DFREEVOX8_NUM_BANDS="$BANDS" \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
    -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 -DJUCE_USE_XINERAMA=0 \
    -DJUCE_USE_XRANDR=0 -DJUCE_USE_XCURSOR=0 -DJUCE_STANDALONE_APPLICATION=0 \
    -DJUCE_DSP_USE_INTEL_IPP=0 \
    -I"$JUCE_DIR/modules" -I"$ROOT/Source/DSP" \
    "$ROOT/Tests/RenderCorpus.cpp" "$ROOT/Tests/juce_modules.cpp" \
    -o "$OUT" \
    -lpthread -ldl
echo "Built: $OUT ($BANDS bands)"
