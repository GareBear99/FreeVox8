#!/usr/bin/env python3
from pathlib import Path
import hashlib, json, time
root = Path(__file__).resolve().parents[1]
out = root / "release_evidence"
out.mkdir(exist_ok=True)
files = []
for p in sorted(root.rglob("*")):
    if not p.is_file():
        continue
    rel = p.relative_to(root).as_posix()
    if any(part in {".git", "build", "JUCE", "release_evidence"} for part in p.relative_to(root).parts):
        continue
    data = p.read_bytes()
    files.append({"path": rel, "sha256": hashlib.sha256(data).hexdigest(), "bytes": len(data)})
manifest = {
    "product": "FreeVox8",
    "version": "0.5.0",
    "kind": "source-release-candidate-evidence",
    "generated_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    "file_count": len(files),
    "files": files,
}
(out / "FREEVOX8_RELEASE_MANIFEST.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(f"Wrote {out / 'FREEVOX8_RELEASE_MANIFEST.json'} with {len(files)} files")
