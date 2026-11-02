"""Upload only this project's title folder; registration/launch happens on the console."""
import argparse
import ftplib
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--host", required=True)
parser.add_argument("--port", type=int, default=2121)
parser.add_argument("--dry-run", action="store_true")
args = parser.parse_args()
root = Path(__file__).resolve().parent / "dist" / "FAKE78002"
manifest = json.loads((root / "build-info.json").read_text())
if manifest["titleId"] != "FAKE78002":
    raise SystemExit("Unexpected title identity")
for name, digest in manifest["sha256"].items():
    path = (root / name).resolve()
    if not path.is_relative_to(root.resolve()):
        raise SystemExit("Manifest path outside title folder")
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise SystemExit(f"Build hash mismatch: {name}")
files = sorted(path for path in root.rglob("*") if path.is_file())
remote = "/data/homebrew/FAKE78002"
for path in files:
    print(f"{path.relative_to(root).as_posix()} -> {remote}/{path.relative_to(root).as_posix()}")
if args.dry_run:
    raise SystemExit(0)
with ftplib.FTP() as ftp:
    ftp.connect(args.host, args.port, timeout=15)
    ftp.login()
    directories = {remote}
    directories.update(f"{remote}/{path.parent.relative_to(root).as_posix()}" for path in files)
    directories.add("/data/homebrew")
    for directory in sorted(directories, key=lambda value: (value.count("/"), value)):
        try:
            ftp.mkd(directory)
        except ftplib.error_perm:
            # An existing directory is fine; verify it really exists and is accessible.
            ftp.cwd(directory)
    for path in files:
        destination = f"{remote}/{path.relative_to(root).as_posix()}"
        with path.open("rb") as source:
            ftp.storbinary(f"STOR {destination}", source)
    for name in ("eboot.bin", "sce_module/libc.prx", "radv/libvulkan.prx"):
        try:
            ftp.sendcmd(f"SITE CHMOD 755 {remote}/{name}")
        except ftplib.error_perm as error:
            print(f"Conferir permissao de execucao de {name}: FTP recusou chmod ({error})")
print("Upload concluido. Registrar e abrir FAKE78002 na interface do console.")
