#!/usr/bin/env python3
"""Compiles azimuth.exe's fixed HLSL with Windows' d3dcompiler_47.dll.

Writes host/src/win32/CompiledShaders.inc, which the host's D3DCompile
(win32/D3dCompiler.cpp) answers from. Run on Windows with 64-bit Python:

    python precompile_shaders.py <azimuth.exe> <CompiledShaders.inc> [--dumps <folder>]

The entries already in CompiledShaders.inc are kept (the 0.9.7 entries were
compiled from an azimuth.exe that is no longer at hand), and the shaders of
the engine given are added: the table for its version below, whose addresses
come from its disassembly. --dumps adds the requests the host wrote to its
report folder's shaders/ (<key>.hlsl with the source as passed, <key>.txt
with entry, target and both flag words) when no entry answered them: the
1.0.2 engine builds most of its HLSL at run time.
"""
import ctypes, hashlib, os, re, struct, sys
from ctypes import POINTER, byref, c_char_p, c_size_t, c_uint, c_void_p

args = [a for a in sys.argv[1:] if a != '--dumps']
dumps = sys.argv[sys.argv.index('--dumps') + 1] if '--dumps' in sys.argv else None
if dumps:
    args.remove(dumps)
exe, inc = args

image = open(exe, 'rb').read()
header = struct.unpack_from('<I', image, 0x3c)[0]
sections = struct.unpack_from('<H', image, header + 6)[0]
optional = struct.unpack_from('<H', image, header + 20)[0]
base = struct.unpack_from('<Q', image, header + 48)[0]
table = header + 24 + optional


def offset(va):
    rva = va - base
    for n in range(sections):
        size, address, raw_size, raw = struct.unpack_from('<IIII', image, table + 40 * n + 8)
        if address <= rva < address + raw_size:
            return raw + rva - address
    raise ValueError(hex(va))


def string(va):
    at = offset(va)
    return image[at:image.index(b'\0', at)]


def span(va, size):
    at = offset(va)
    return image[at:at + size]


ENGINE_097 = '5d3ce30b'
ENGINE_102 = '42527964'
digest = hashlib.sha256(image).hexdigest()

# (source bytes as passed, entry, target, flags1, call site)
if digest.startswith(ENGINE_097):
    SHADERS = [
        (span(0x1402609b0, 0x5dc), b'main', b'cs_5_0', 0x800, 'engine+0x10d27d detile'),
        (string(0x140259920), b'vs', b'vs_5_0', 0x800, 'engine+0xa150d'),
        (string(0x1402519f0), b'plain', b'ps_5_0', 0x800, 'engine+0x329cf'),
        (string(0x1402519f0), b'curved', b'ps_5_0', 0x800, 'engine+0x329ee'),
        (string(0x1402673e0), b'ps', b'ps_5_0', 0x800, 'engine+0x17e2f2'),
    ]
elif digest.startswith(ENGINE_102):
    # The launcher's surface (engine+0x62f510): three pieces joined, then corner and picture.
    surface = string(0x14081c310) + span(0x1407eae70, 0x61b) + string(0x14081c390)
    # The overlay (engine+0x5a12c0 through engine+0x59fb80): one source, five entries.
    overlay = span(0x1408117c0, 0x3b3a)
    SHADERS = [
        (surface, b'corner', b'vs_5_0', 0x800, '1.0.2 engine+0x62fe0e launcher'),
        (surface, b'picture', b'ps_5_0', 0x800, '1.0.2 engine+0x62fedf launcher'),
        (overlay, b'place', b'vs_5_0', 0x800, '1.0.2 engine+0x5a0bc4 overlay'),
        (overlay, b'paint', b'ps_5_0', 0x800, '1.0.2 engine+0x5a0bc4 overlay'),
        (overlay, b'corner', b'vs_5_0', 0x800, '1.0.2 engine+0x5a0c91 overlay'),
        (overlay, b'halve', b'ps_5_0', 0x800, '1.0.2 engine+0x5a0c91 overlay'),
        (overlay, b'spread', b'ps_5_0', 0x800, '1.0.2 engine+0x5a0cf6 overlay'),
        (span(0x1408105a0, 0x5dc), b'main', b'cs_5_0', 0x800, '1.0.2 engine+0x5957cd detile'),
        (string(0x140802f40), b'vs', b'vs_5_0', 0x800, '1.0.2 engine+0x2bb1bd'),
    ]
else:
    sys.exit('azimuth.exe de versao desconhecida: ' + digest)

# What the host gives for a pixel shader it has no entry for (title 1.48): black,
# so the game goes on and every missing shader is dumped in one run.
SHADERS.append((b'float4 placeholder(float4 position : SV_Position) : SV_Target { return 0; }',
                b'placeholder', b'ps_5_0', 0x800, 'host placeholder'))

if dumps:
    for name in sorted(os.listdir(dumps)):
        if not name.endswith('.txt'):
            continue
        lines = open(os.path.join(dumps, name), 'rb').read().split(b'\n')
        source = open(os.path.join(dumps, name[:-4] + '.hlsl'), 'rb').read()
        SHADERS.append((source, lines[0], lines[1], int(lines[2]), 'console dump ' + name[:12] + ' ' + lines[4].decode(errors='replace')))
        if int(lines[3]):
            sys.exit(name + ': flags2 diferente de zero')

compiler = ctypes.WinDLL(r'C:\Windows\System32\d3dcompiler_47.dll')
compile_ = compiler.D3DCompile
compile_.argtypes = [c_void_p, c_size_t, c_char_p, c_void_p, c_void_p, c_char_p, c_char_p,
                     c_uint, c_uint, POINTER(c_void_p), POINTER(c_void_p)]
compile_.restype = ctypes.c_long


def take(blob):
    """The bytes of an ID3DBlob, which is then released."""
    vtable = ctypes.cast(blob, POINTER(POINTER(c_void_p))).contents
    pointer = ctypes.WINFUNCTYPE(c_void_p, c_void_p)(vtable[3])
    size = ctypes.WINFUNCTYPE(c_size_t, c_void_p)(vtable[4])
    release = ctypes.WINFUNCTYPE(c_uint, c_void_p)(vtable[2])
    data = ctypes.string_at(pointer(blob), size(blob))
    release(blob)
    return data


# The entries already in the table: (comment, key, code).
kept = []
if os.path.exists(inc):
    text = open(inc).read()
    for comment, index in re.findall(r'^// (.*)\nstatic const uint8_t shaderKey(\d+)\[32\]', text, re.M):
        key = bytes(int(b, 16) for b in re.search(r'shaderKey%s\[32\]=\{([^}]*)\}' % index, text).group(1).split(','))
        body = re.search(r'shaderCode%s\[\d+\]=\{\n(.*?)\n\};' % index, text, re.S).group(1)
        code = bytes(int(b, 16) for b in re.findall(r'0x([0-9a-f]{2})', body))
        kept.append((comment, key, code))

compiled = list(kept)
known = {key for _, key, _ in kept}
for source, entry, target, flags, site in SHADERS:
    key = hashlib.sha256(source + b'\0' + entry + b'\0' + target + b'\0' + struct.pack('<II', flags, 0)).digest()
    if key in known:
        continue
    code, errors = c_void_p(), c_void_p()
    result = compile_(source, len(source), None, None, None, entry, target, flags, 0, byref(code), byref(errors))
    messages = take(errors) if errors.value else b''
    if result < 0:
        sys.exit('%s: D3DCompile 0x%08x %s' % (site, result & 0xffffffff, messages.decode(errors='replace')))
    bytecode = take(code)
    print('%s: %s %s, %d bytes of HLSL -> %d bytes of DXBC' % (site, entry.decode(), target.decode(), len(source), len(bytecode)))
    compiled.append(('%s: %s, %s' % (site, entry.decode(), target.decode()), key, bytecode))
    known.add(key)

lines = ['// Generated by host/tools/precompile_shaders.py from azimuth.exe 0.9.7 and 1.0.2 with',
         "// Windows' d3dcompiler_47.dll: the engine's fixed HLSL (its D3DCompile calls),",
         '// keyed by the SHA-256 of the source bytes, entry point, target and both flag words.']
entries = []
for index, (comment, key, bytecode) in enumerate(compiled):
    lines.append('// ' + comment)
    lines.append('static const uint8_t shaderKey%d[32]={%s};' % (index, ','.join('0x%02x' % b for b in key)))
    lines.append('static const uint8_t shaderCode%d[%d]={' % (index, len(bytecode)))
    for at in range(0, len(bytecode), 24):
        lines.append('    ' + ','.join('0x%02x' % b for b in bytecode[at:at + 24]) + ',')
    lines.append('};')
    name = comment.rsplit(': ', 1)[-1].replace(',', '')
    entries.append('    {shaderKey%d,shaderCode%d,sizeof shaderCode%d,"%s"},' % (index, index, index, name))
lines.append('struct CompiledShader {const uint8_t* key;const uint8_t* code;size_t size;const char* name;};')
lines.append('static const CompiledShader compiledShaders[]={')
lines += entries
lines.append('};')
open(inc, 'w', newline='\n').write('\n'.join(lines) + '\n')
print('%d entradas (%d mantidas)' % (len(compiled), len(kept)))
