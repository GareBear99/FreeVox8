#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

python3 scripts/validate_repo.py

if [[ ! -d JUCE && -z "${JUCE_DIR:-}" ]]; then
  echo "JUCE not found. Add ./JUCE or set JUCE_DIR=/path/to/JUCE before certification." >&2
  exit 2
fi

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release ${JUCE_DIR:+-DJUCE_DIR="$JUCE_DIR"}
cmake --build build --config Release
python3 scripts/make_evidence_pack.py

echo "Source validation, Release build, and evidence generation completed."
echo "Next gate: run pluginval strictness 10 against the generated VST3/AU artifacts."
