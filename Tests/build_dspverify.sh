#!/usr/bin/env bash
# Build the DSP verification harness headlessly (no JUCE CMake, no juceaide,
# no X11): compile DSPVerify.cpp plus the JUCE modules it needs directly.
# Usage: Tests/build_dspverify.sh [JUCE_DIR]
#   JUCE_DIR defaults to $JUCE_DIR, else ../FreeEQ8/JUCE, else ./JUCE.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
JUCE_DIR="${1:-${JUCE_DIR:-}}"
if [ -z "$JUCE_DIR" ]; then
    if [ -d "$ROOT/../JUCE-8.0.5" ]; then JUCE_DIR="$ROOT/../JUCE-8.0.5";
    elif [ -d "$ROOT/JUCE" ]; then JUCE_DIR="$ROOT/JUCE";
    else echo "JUCE not found: pass JUCE_DIR" >&2; exit 1; fi
fi
mkdir -p "$ROOT/build-test"
g++ -std=c++17 -O2 -DNDEBUG \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 \
    -DJUCE_USE_CURL=0 -DJUCE_WEB_BROWSER=0 -DJUCE_USE_XINERAMA=0 \
    -DJUCE_USE_XRANDR=0 -DJUCE_USE_XCURSOR=0 -DJUCE_STANDALONE_APPLICATION=0 \
    -DJUCE_DSP_USE_INTEL_IPP=0 \
    -I"$JUCE_DIR/modules" -I"$ROOT/Source/DSP" \
    "$ROOT/Tests/DSPVerify.cpp" "$ROOT/Tests/juce_modules.cpp" \
    -o "$ROOT/build-test/FreeVox8_DSPVerify" \
    -lpthread -ldl
echo "Built: $ROOT/build-test/FreeVox8_DSPVerify"
