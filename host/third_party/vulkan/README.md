# Cabeçalhos do Vulkan

`vulkan/vulkan_core.h`, `vulkan/vk_platform.h` e os 12 cabeçalhos de
`vk_video/` que o `vulkan_core.h` inclui, da Khronos, sem alterações.
Licença: Apache-2.0 (indicada no início de cada arquivo).

`VK_HEADER_VERSION` 354, a mesma versão que o RADV do PS5 anuncia (1.4.354).
Copiados de `PS5X360/.deps/references/PS5_Vulkan/.deps/native/radv-release/include/vulkan/`,
que acompanha o RADV construído do fork PS5_Mesa na revisão
`0b2d6d1a61d9bbf89cf8beb88a696144f67c61f8`.

| Arquivo | SHA-256 |
|---|---|
| `vulkan/vulkan_core.h` | `55c06a17793bb1a9d752aa0906116fff9d1fb74f5dd3896777b92f124c9637aa` |
| `vulkan/vk_platform.h` | `949d517bb83e1d88fd4f1cef02bd3cb9ab50d44e8354cc68227cf2dccfdd3307` |

Uso: `host/tools/gen_vulkan_commands.py` lê os protótipos de `vulkan_core.h` e
gera `host/src/gpu/vulkan_commands.inc`, que `host/src/gpu/vulkan_abi.c`
transforma nos adaptadores Windows x64 → System V do `vulkan-1.dll` do host.
