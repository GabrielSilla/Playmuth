"""Record exact build artifacts and produce a folder-install ZIP."""
import hashlib
import json
import shutil
import zipfile
from pathlib import Path

root = Path(__file__).resolve().parent
dist = root / "dist" / "FAKE78002"
shutil.copy2(root / "THIRD_PARTY.md", dist / "THIRD_PARTY.md")
(dist / "licenses").mkdir(exist_ok=True)
for license_path in (root / "licenses").glob("*.txt"):
    shutil.copy2(license_path, dist / "licenses" / license_path.name)
# The host's runtime dependencies travel with the title (1.39): the RADV
# Vulkan driver and vkd3d-proton, never read from another program's install.
vendored = {"radv/libvulkan.prx": "db21b394c79af0fab48072a7a86e2f25609f2d122552831810a47bad0c641b94",
            "dll/d3d12.dll": "08e44b0563865a9ff842a76484794bfb7acc17a5fad050c15f315a889c8854cd",
            "dll/d3d12core.dll": "5b4257b328384ecfe4a66d27b534c61ce248a6e81c46a110cd3eab605956b45f"}
for name, digest in vendored.items():
    source = root / "vendor" / name
    if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
        raise SystemExit(f"vendor/{name}: hash diferente do esperado")
    (dist / name).parent.mkdir(exist_ok=True)
    shutil.copy2(source, dist / name)
files = ["eboot.bin", "sce_sys/param.json", "sce_sys/icon0.png", "sce_module/libc.prx",
         "THIRD_PARTY.md", *vendored]
files.extend(path.relative_to(dist).as_posix() for path in sorted((dist / "licenses").glob("*.txt")))
manifest = {
    "titleId": "FAKE78002",
    "version": "1.53",
    "displayName": "Playmuth",
    "consoleValidated": False,
    "defaultMode": "host",
    "foundationRevision": (root / "build/foundation-revision.txt").read_text().strip(),
    "sha256": {name: hashlib.sha256((dist / name).read_bytes()).hexdigest() for name in files},
}
(dist / "build-info.json").write_text(json.dumps(manifest, indent=2) + "\n")
archive = root / "dist" / "Playmuth-1.53.zip"
with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
    for path in sorted(dist.rglob("*")):
        if path.is_file():
            output.write(path, path.relative_to(dist.parent))
print(f"Package: {archive}")
