#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release "${@}"
cmake --build build --config Release --target FreeVox8_All
echo "FreeVox8 artifacts: build/FreeVox8_artefacts/Release"
