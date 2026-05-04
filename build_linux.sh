#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release "${@}"
cmake --build build --config Release --target FreeVox8_Standalone
echo "Standalone artifact: build/FreeVox8_artefacts/Release/Standalone"
