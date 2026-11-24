"""Deterministic gzip assets, embedded in the application image (no FS upload)."""
import gzip
from pathlib import Path

root = Path(__file__).resolve().parent
out = root / "main" / "generated"
out.mkdir(exist_ok=True)
for name, source in {
    "index.html": root / "main/index.html",
    "app.js": root / "main/app.js",
    "scene.js": root / "main/scene.js",
    "three.module.min.js": root / "vendor/three.module.min.js",
}.items():
    packed = gzip.compress(source.read_bytes(), compresslevel=9, mtime=0)
    dest = out / (name + ".gz")
    if not dest.exists() or dest.read_bytes() != packed:
        dest.write_bytes(packed)
    print(f"{name}: {source.stat().st_size} -> {len(packed)} bytes")
