#!/usr/bin/env python3
# list_apis.py: writes docs/APIS_IMPLEMENTADAS.md from the code itself: every
# export in host/src/win32/exports.inc (library, function, the host file that
# implements it) and every COM method in the *_abi.c vtables, under the
# interface comment that precedes it. Run from the project root:
#   python3 host/tools/list_apis.py
import collections, pathlib, re

root = pathlib.Path(__file__).resolve().parents[2]
src = root / 'host' / 'src'

exports = []
for m in re.finditer(r'EXPORT\(\s*(\w+)\s*,\s*(\w+)\s*,', (src / 'win32' / 'exports.inc').read_text(encoding='utf-8')):
    if (m.group(1), m.group(2)) != ('dll', 'name'):   # the macro's own description at the top
        exports.append((m.group(1), m.group(2)))

# Where each win_<name> is defined (a C or C++ definition, not a declaration).
sources = {p: p.read_text(encoding='utf-8', errors='replace') for p in src.rglob('*') if p.suffix in ('.cpp', '.c')}
def home(name):
    pattern = re.compile(r'\bwin_' + re.escape(name) + r'\s*\([^;{]*\)\s*(?:noexcept\s*)?\{', re.S)
    for path, text in sources.items():
        if pattern.search(text):
            return path.relative_to(src).as_posix()
    return '?'

aliases = {'KERNEL32': 'kernel32', 'USER32': 'user32', 'GDI32': 'gdi32', 'ADVAPI32': 'advapi32', 'SHELL32': 'shell32', 'WS2_32': 'ws2_32'}
by_library = collections.OrderedDict()
for library, name in exports:
    by_library.setdefault(aliases.get(library, library), []).append(name)

out = ['# APIs implementadas pelo host',
       '',
       'Gerado por `host/tools/list_apis.py` a partir do código (não editar à mão). '
       f'{len(exports)} exports em `host/src/win32/exports.inc`, mais as interfaces COM das vtables `*_abi.c` '
       'e os 763 comandos Vulkan gerados em `host/src/gpu/vulkan_commands.inc` (todos os do `vulkan_core.h` 1.4.354, '
       'cada um com um adaptador Windows x64 → System V em `gpu/vulkan_abi.c`).',
       '',
       'Cada export é uma função `win_<nome>` em C/C++ (System V) com uma entrada `ms_<nome>` na convenção do Windows '
       '(`guest_abi.c`), que registra o nome no anel de chamadas da thread (`hostGateNamed`). Um import que não está '
       'nesta lista vira uma parada de diagnóstico com o nome da DLL e da função; `GetProcAddress` de um nome '
       'ausente devolve nulo e registra "ainda sem implementacao".',
       '',
       '## Exports por biblioteca',
       '']
for library in sorted(by_library, key=str.lower):
    names = by_library[library]
    out.append(f'### {library}.dll ({len(names)})')
    out.append('')
    out.append('| Função | Implementação |')
    out.append('|---|---|')
    for name in sorted(names, key=str.lower):
        out.append(f'| `{name}` | `{home(name)}` |')
    out.append('')

out.append('## Interfaces COM implementadas (vtables na convenção do Windows)')
out.append('')
out.append('Os métodos estão na ordem dos slots do cabeçalho do Windows. Um slot sem implementação devolve `E_NOTIMPL` e registra o próprio nome.')
out.append('')
for abi in ['gpu/dxgi_abi.c', 'win32/audio_abi.c', 'win32/wic_abi.c', 'win32/xaudio2_abi.c']:
    text = (src / abi).read_text(encoding='utf-8')
    out.append(f'### `{abi}`')
    out.append('')
    section = None; methods = collections.OrderedDict()
    for line in text.splitlines():
        header = re.match(r'//\s*(I[A-Z]\w+(?:[ ,/]+(?:and\s+)?I[A-Z]\w+)*)', line)
        if header:
            section = header.group(1).strip(); methods.setdefault(section, [])
            continue
        m = re.match(r'(?:VOID)?METHOD\((\w+)', line)
        if m and section:
            methods[section].append(m.group(1))
    for section, names in methods.items():
        if names:
            out.append(f'- **{section}**: ' + ', '.join(f'`{n}`' for n in names))
    out.append('')

(root / 'docs' / 'APIS_IMPLEMENTADAS.md').write_text('\n'.join(out) + '\n', encoding='utf-8', newline='\n')
print(len(exports), 'exports;', sum(1 for v in by_library.values() for n in v if home(n) == '?'), 'sem definicao achada')
