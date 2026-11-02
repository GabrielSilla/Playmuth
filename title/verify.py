"""Structural checks for the native ELF and its fake SELF wrapper."""
import struct
import sys
from pathlib import Path


def verify(elf_path, self_path):
    data = Path(elf_path).read_bytes()
    assert data[:6] == b"\x7fELF\x02\x01", "expected ELF64 little endian"
    kind, machine = struct.unpack_from("<HH", data, 16)
    assert kind == 0xFE10 and machine == 62, "expected ET_SCE_EXEC_ASLR x86-64"
    entry, phoff = struct.unpack_from("<QQ", data, 24)
    phsize, phnum = struct.unpack_from("<HH", data, 54)
    loads = []
    types = []
    dynamic_tags = set()
    for i in range(phnum):
        ptype, flags, offset, address, _, filesz, memsz, align = struct.unpack_from(
            "<IIQQQQQQ", data, phoff + i * phsize)
        types.append(ptype)
        assert offset + filesz <= len(data), "segment outside ELF"
        if ptype == 2:
            for at in range(offset, offset + filesz, 16):
                tag, _ = struct.unpack_from("<QQ", data, at)
                dynamic_tags.add(tag)
        if ptype != 1:
            continue
        assert filesz <= memsz, "file segment larger than memory"
        assert flags & 3 != 3, "writable/executable load"
        assert align >= 0x4000 and offset % 0x4000 == address % 0x4000, "page alignment"
        assert address >= 0x100000000, "title linked below intended base"
        assert address + memsz <= 0x140000000, "title collides with engine base"
        loads.append((address, address + memsz, flags))
    assert loads and min(x[0] for x in loads) == 0x100000000, "unexpected base"
    assert any(a <= entry < b and flags & 1 for a, b, flags in loads), "entry outside code"
    assert 2 in types, "missing dynamic metadata"
    assert 0x61000001 in types, "missing SCE process parameters"
    assert {0x61000043, 0x61000045, 0x61000049} <= dynamic_tags, "missing SCE module/import metadata"
    wrapper = Path(self_path).read_bytes()
    assert wrapper[:4] == bytes.fromhex("4f153d1d"), "unexpected SELF magic"
    print(f"Verified native title: {len(loads)} loads, entry=0x{entry:x}, SELF={len(wrapper)} bytes")


if __name__ == "__main__":
    verify(*sys.argv[1:])
