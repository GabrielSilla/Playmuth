# Fase 3 — caminho para o D3D12 do motor

Análise de 28/10/2026, feita depois do marco do título 0.17: a GPU do PS5
executa compute shaders pelo RADV, e o título abre e fecha sem erro.

## O que o motor pede

O host 0.7.1 parou em `CreateDXGIFactory2`: o motor exige fábrica DXGI,
adaptador e dispositivo D3D12 antes de pintar o launcher
(`host/src/win32/D3d12.cpp` ainda devolve `DXGI_ERROR_UNSUPPORTED`).

## Como o Playden resolve

No console, o Playden usa DLLs **do Windows** (PE x86-64) dentro do Wine.
Arquivos conferidos pelo manifesto do kit (`/data/playden/kits/playden-kit/manifest.json`),
com tamanho e SHA-256 iguais aos baixados:

| Arquivo no kit | Origem | Tamanho | SHA-256 |
|---|---|---|---|
| `runtime/wine-wow64/031302d-kit-0.13/lib/vkd3d-proton/x86_64-windows/d3d12.dll` | vkd3d-proton | 118.784 | `08e44b05…54cd` |
| `runtime/wine-wow64/031302d-kit-0.13/lib/vkd3d-proton/x86_64-windows/d3d12core.dll` | vkd3d-proton | 5.894.144 | `5b4257b3…b45f` |
| `template/bottle/drive_c/windows/system32/dxgi.dll` | DXVK 2.6.2 | 2.891.790 | `074616e1…ed39` |

O `d3d12.dll` de 86 KB em `win/wine/lib/wine/x86_64-windows` é o módulo
interno do Wine, e não o vkd3d-proton.

## Imports dessas DLLs

**Nenhuma importa `vulkan-1.dll`.** O vkd3d-proton e o DXVK carregam o Vulkan
em tempo de execução (`LoadLibrary` + `GetProcAddress("vkGetInstanceProcAddr")`).
O host pode, portanto, oferecer um `vulkan-1.dll` próprio cujas funções são
adaptadores `ms_abi` para o RADV, gerados a partir dos cabeçalhos do Vulkan.

Comparação com `host/src/win32/exports.inc` (o que o host já implementa):

| DLL | Já no host | Faltam |
|---|---|---|
| `d3d12.dll` | 19 de 68 | kernel32 8, msvcrt 41 |
| `d3d12core.dll` | 42 de 183 | kernel32 32, msvcrt 90, gdi32 14 (`D3DKMT*`), advapi32 4 (registro), dxgi 1 (`CreateDXGIFactory1`) |
| `dxgi.dll` (DXVK) | 54 de 203 | ucrt 95, kernel32 30, user32 14, advapi32 6, setupapi 4 |

Observações:
- `msvcrt` é quase toda C padrão, mas a família `printf` usa `va_list` do
  Windows, diferente do `va_list` do System V; precisa de formatador próprio.
- `d3d12core.dll` importa `RtlUnwindEx`/`RtlVirtualUnwind`; o DXVK também
  importa `__C_specific_handler`. O host ainda não tem desenrolamento SEH real.
- `D3DKMT*` e o registro servem a recursos compartilhados e configuração; podem
  responder "não suportado"/"não encontrado".

## Caminho recomendado

1. **Vulkan no host.** *Feito em 28/10/2026 (host e título 0.18; validado no
   Linux com `make test-vulkan`, 61 verificações; ainda não exercitado no
   console).* Módulo que carrega o RADV com as três regras do título
   (rodar o `INIT_ARRAY`, cache sem shaders antigos, RADV carregado até o fim) e
   um `vulkan-1.dll` virtual com adaptadores `ms_abi` gerados.
2. **vkd3d-proton como PE.** *Feito em 28/10/2026 no Linux (`host --d3d12`
   passou: dispositivo, fila, lista executada, fence); título 0.19 com
   `title-mode=d3d12`, ainda não testado no console. Junto veio o mínimo do
   passo 3: fábrica e adaptador DXGI, que o `D3D12CreateDevice` exige.* Carregar `d3d12.dll` e `d3d12core.dll` da
   instalação do Playden com o loader do host, como o RADV; completar
   kernel32/msvcrt/gdi32/advapi32 para eles.
   Marco: um modo de teste do título cria `ID3D12Device`, fila e fence e
   executa um comando, sem o motor.
3. **DXGI próprio no host**, em vez do DXVK: fábrica, adaptador e saída
   descrevendo o dispositivo Vulkan real, e depois o swapchain pela interface
   de swapchain do vkd3d-proton. Evita o ucrt, o C++/SEH e o setupapi do DXVK.
   Marco: o motor passa de `CreateDXGIFactory2` e `D3D12CreateDevice`.
4. **Apresentação** pelo WSI do RADV sobre VideoOut, e o quadro do launcher.
