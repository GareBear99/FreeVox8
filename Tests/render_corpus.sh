#!/usr/bin/env bash
# Render the fixed STOI corpus (see vox8-bench/corpus/CORPUS.md) through
# the FreeVox8 filterbank renderer at a given band count.
# Usage: Tests/render_corpus.sh [BANDS] [OUTDIR]
#   BANDS  : 128 (default), 32 or 16 -- builds the matching renderer
#            binary via build_render.sh if needed.
#   OUTDIR : where the <sp>_<id>_vocoded.wav + <sp>_<id>_clean44.wav
#            pairs go (default: ~/workspace/vox8-bench/corpus/renders).
#            Use a separate dir per band count (renders32, renders16)
#            so the 128-band renders are never clobbered.
#   CORPUS_DIR env: corpus root (default ~/workspace/vox8-bench/corpus).
# Protocol (fixed, same every run): filterbank Vocoder mode, carrier
# MIDI 60, carrierBlend 1.0, air 0, transientProtect 0 -- see
# Tests/RenderCorpus.cpp. Alignment + scoring are done by
# vox8-bench/corpus/score_stoi.py --renders OUTDIR.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BANDS="${1:-128}"
OUTDIR="${2:-$HOME/workspace/vox8-bench/corpus/renders}"
CORPUS_DIR="${CORPUS_DIR:-$HOME/workspace/vox8-bench/corpus}"
IDS="arctic_a0104 arctic_a0106 arctic_a0108 arctic_a0111 arctic_a0115 arctic_a0125 arctic_a0126 arctic_b0011 arctic_b0016 arctic_b0019"
if [ "$BANDS" = "128" ]; then BIN="$ROOT/build-test/FreeVox8_RenderCorpus";
else BIN="$ROOT/build-test/FreeVox8_RenderCorpus_$BANDS"; fi
if [ ! -x "$BIN" ]; then "$ROOT/Tests/build_render.sh" "" "$BANDS"; fi
mkdir -p "$OUTDIR"
for sp in bdl slt; do
    for id in $IDS; do
        in="$CORPUS_DIR/cmu_us_${sp}_arctic/wav/${id}.wav"
        "$BIN" "$in" "$OUTDIR/${sp}_${id}_vocoded.wav" "$OUTDIR/${sp}_${id}_clean44.wav" --note 60
    done
done
echo "Rendered $BANDS bands -> $OUTDIR"
