#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
VERSION=$(python3 - <<'EOF'
from pathlib import Path
import re
m = re.search(r'project\(FreeVox8 VERSION ([0-9.]+)', Path('CMakeLists.txt').read_text())
print(m.group(1) if m else '0.0.0')
EOF
)
BUILD_DIR="$ROOT/build"
ARTEFACTS="$BUILD_DIR/FreeVox8_artefacts/Release"
DMG_NAME="FreeVox8-v${VERSION}-macOS"
STAGING="$BUILD_DIR/${DMG_NAME}"
rm -rf "$STAGING"
mkdir -p "$STAGING/FreeVox8"
cp -R "$ARTEFACTS/VST3/FreeVox8.vst3" "$STAGING/FreeVox8/" 2>/dev/null || true
cp -R "$ARTEFACTS/AU/FreeVox8.component" "$STAGING/FreeVox8/" 2>/dev/null || true
cp "$ROOT/README.md" "$STAGING/README.txt"
hdiutil create -volname "$DMG_NAME" -srcfolder "$STAGING" -ov -format UDZO "$BUILD_DIR/${DMG_NAME}.dmg"
echo "$BUILD_DIR/${DMG_NAME}.dmg"
