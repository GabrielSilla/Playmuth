"""Catch payload-only memory/syscall routines before packaging a native title."""
import re
import subprocess
import sys
from pathlib import Path

elf = sys.argv[1]
# The C GPU probe must not use FreeBSD's stdio macros over native FILE.
gpu_object = Path(elf).parent / "obj/gpu.o"
undefined = subprocess.check_output(["nm", "-u", str(gpu_object)], text=True)
if "__isthreaded" in undefined:
    raise SystemExit("GPU object still uses SDK stdio macros that inspect opaque FILE")
symbols = subprocess.check_output(["readelf", "--wide", "--dyn-syms", elf], text=True)
for name in ("mmap", "mprotect", "munmap", "sysarch"):
    if not re.search(r"\bUND\s+" + name + r"\s*$", symbols, re.MULTILINE):
        raise SystemExit(f"{name} must be an import from the system library")
assembly = subprocess.check_output(["objdump", "-d", elf], text=True)
raw = [line for line in assembly.splitlines() if re.search(r"\bsyscall\s*(?:#.*)?$", line)]
if raw:
    raise SystemExit("Raw syscall instructions in native title:\n" + "\n".join(raw[:20]))
relocations = subprocess.check_output(["readelf", "--wide", "--relocs", elf], text=True)
if re.search(r"R_X86_64_RELATIVE\s+0\s*$", relocations, re.MULTILINE):
    raise SystemExit("Null relative relocation would acquire the ASLR slide")
print("Native runtime verified: memory/sysarch imports; no raw syscall instructions or null relative relocations")
