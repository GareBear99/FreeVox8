#!/usr/bin/env bash
set -euo pipefail
python3 scripts/validate_repo.py
python3 scripts/make_evidence_pack.py
echo "FreeVox8 source smoke passed."
