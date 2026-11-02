"""Reads a PS5 fake SELF (.prx): ELF header, program headers, SELF segment
table, dynamic tags and the INIT_ARRAY with its RELATIVE relocations."""
import struct, sys

data = open(sys.argv[1], "rb").read()
magic, = struct.unpack_from("<I", data, 0)
count, = struct.unpack_from("<H", data, 0x18)
entries = []
for n in range(count):
    flags, offset, csize, dsize = struct.unpack_from("<QQQQ", data, 0x20 + n * 32)
    entries.append((flags, offset, csize, dsize))
elf = 0x20 + count * 32
assert data[elf:elf + 4] == b"\x7fELF", hex(elf)
(e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags, e_ehsize,
 e_phentsize, e_phnum) = struct.unpack_from("<HHIQQQIHHH", data, elf + 16)
print(f"SELF magic {magic:08x}, {count} segment entries, ELF at {elf:#x}")
print(f"e_type {e_type:#x} e_entry {e_entry:#x} phnum {e_phnum}")
phdrs = []
for n in range(e_phnum):
    p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = \
        struct.unpack_from("<IIQQQQQQ", data, elf + e_phoff + n * e_phentsize)
    phdrs.append((p_type, p_flags, p_offset, p_vaddr, p_filesz, p_memsz))
    print(f"  phdr {n}: type {p_type:#010x} flags {p_flags:#x} off {p_offset:#x} "
          f"vaddr {p_vaddr:#x} filesz {p_filesz:#x} memsz {p_memsz:#x}")
# SELF segment entries carry the phdr index in bits 20..; blocked segments
# (bit 0x800) hold the data at 'offset', uncompressed when csize == dsize.
segdata = {}
for flags, offset, csize, dsize in entries:
    index = (flags >> 20) & 0xfff
    print(f"  self seg flags {flags:#x} phdr {index} off {offset:#x} csize {csize:#x} dsize {dsize:#x}")
    if flags & 0x800 and csize == dsize:
        segdata[index] = offset

def read(vaddr, size):
    for n, (p_type, p_flags, p_offset, p_vaddr, p_filesz, p_memsz) in enumerate(phdrs):
        if p_type == 1 and p_vaddr <= vaddr < p_vaddr + p_memsz and n in segdata:
            rel = vaddr - p_vaddr
            if rel >= p_filesz:
                return b"\0" * size
            start = segdata[n] + rel
            return data[start:start + size]
    return None

def file_part(p_offset, p_filesz):
    # Dynamic segments live in a loaded segment's file range or after the ELF.
    for n, (p_type, p_flags, off, p_vaddr, filesz, memsz) in enumerate(phdrs):
        if p_type == 1 and off <= p_offset < off + filesz and n in segdata:
            start = segdata[n] + (p_offset - off)
            return data[start:start + p_filesz]
    for n, (p_type, p_flags, off, p_vaddr, filesz, memsz) in enumerate(phdrs):
        if off == p_offset and n in segdata:
            return data[segdata[n]:segdata[n] + p_filesz]
    return None

for label, address in (("vaddr 0", 0), ("e_entry", e_entry)):
    code = read(address, 48)
    print(f"{label} {address:#x}: {code.hex(' ') if code else None}")

dynamic = next((p for p in phdrs if p[0] == 2), None)
tags = {}
blob = file_part(dynamic[2], dynamic[4]) if dynamic else None
if blob is None and dynamic:
    blob = read(dynamic[3], dynamic[4])
names = {12: "DT_INIT", 13: "DT_FINI", 25: "DT_INIT_ARRAY", 27: "DT_INIT_ARRAYSZ",
         26: "DT_FINI_ARRAY", 28: "DT_FINI_ARRAYSZ", 32: "DT_PREINIT_ARRAY", 33: "DT_PREINIT_ARRAYSZ",
         7: "DT_RELA", 8: "DT_RELASZ", 0x6100002f: "DT_SCE_RELA", 0x61000031: "DT_SCE_RELASZ",
         0x61000035: "DT_SCE_STRTAB", 0x61000039: "DT_SCE_SYMTAB"}
if blob:
    for n in range(0, len(blob), 16):
        tag, value = struct.unpack_from("<qQ", blob, n)
        if tag == 0:
            break
        tags.setdefault(tag, value)
        if tag in names:
            print(f"  {names[tag]} = {value:#x}")
init_array, size = tags.get(25), tags.get(27, 0)
relocs = {}
rela = tags.get(0x6100002f) or tags.get(7)
relasz = tags.get(0x61000031) or tags.get(8, 0)
if rela is not None:
    # SCE relocations sit in the dynlib data segment (PT_SCE_DYNLIBDATA, file offset).
    dynlib = next((p for p in phdrs if p[0] == 0x61000000), None)
    table = file_part(dynlib[2] + rela, relasz) if dynlib else read(rela, relasz)
    for n in range(0, len(table or b""), 24):
        r_offset, r_info, r_addend = struct.unpack_from("<QQq", table, n)
        relocs[r_offset] = (r_info & 0xffffffff, r_info >> 32, r_addend)
    print(f"  {len(relocs)} relocacoes")
if init_array is not None:
    print(f"INIT_ARRAY {init_array:#x}, {size // 8} entradas")
    for n in range(size // 8):
        slot = init_array + n * 8
        raw, = struct.unpack("<Q", read(slot, 8))
        kind = relocs.get(slot)
        target = kind[2] if kind and kind[0] == 8 else raw
        code = read(target, 16)
        print(f"  [{n}] slot {slot:#x} raw {raw:#x} reloc {kind} -> {target:#x}: {code.hex(' ') if code else None}")
