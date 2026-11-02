# Evidências que orientam o port

## PS5 com Playden (22/10/2026)

Fonte: `../ps5-diagnostics` (Playden 0.6.0, kit 0.12, prospero-win `f729534f`, Wine 11.17, firmware informado 13.00).

- GPU: `PS5 (vendor 0x1002)`, vkd3d-proton 3.0.1 sobre RADV (Mesa 26.2.0 no teste do Dead Space).
- Recursos informados ao motor: resource binding tier 3, tiled resources tier 1, root signature 1.1, enhanced barriers 1, unified memory 0.
- O launcher funcionou. Primeiro bloqueio: o Wine trocou o runtime VC11 privado pelo builtin, e o motor exige o original. Isso foi resolvido com `msvcr110,msvcp110=n`.
- Ao iniciar o jogo (duas tentativas):
  - `CRASHED exception 0xc0000005 at game+0x125efda writing 0x1007e80004 on thread 36`
  - `CRASHED exception 0xc0000005 at game+0x125efda writing 0x1007250004 on thread 36`
  - cinco milissegundos depois: `CRASHED ... at 0x8fffd9ac12 writing 0x1046fe9898 (Azimuth end)`
- Mesma instrução do jogo nas duas tentativas, escrevendo em endereços diferentes por volta de `0x10_0700_0000`. Ainda falta analisar a instrução no executável do FH2 e o mapa de memória de título do motor.

## O que o port do Xbox ensinou sobre o motor (AzimuthUwp 0.2.x)

- O código do motor lê o TLS estático em `ThreadLocalStoragePointer[0]`, então o bloco precisa estar no slot 0. O host reserva 4096 bytes, e o motor usa cerca de 532.
- Ao iniciar o jogo, o motor carrega `data/bin/azimuth-tls.dll`. É um helper mínimo que declara os 0x150 bytes de TLS do FH2 e exporta o índice e a thread que o carregou. No PS5 nativo, esse índice é um slot do TEB falso.
- Dependências de desktop do jogo:
  - runtime VC11 privado (com hash conferido);
  - `ole32`/`combase`, `ws2_32`, Media Foundation (`MFPlat`, `MFReadWrite`), `MMDevAPI`, `xaudio2_9`, `windowscodecs`, `bcrypt`.

  Para as DLLs de sistema, o motor monta o caminho com `GetSystemDirectoryW`.
- Pipelines: todos os shaders do jogo são DXBC (2344 pipelines). Sem cache, a preparação levou cerca de 6 minutos no Xbox. Com o vkd3d-proton, a biblioteca de pipelines vira pipeline cache do Vulkan e passa a persistir.
- Threads: o motor cria cerca de 11 threads no launcher, com TLS callbacks e `DLL_THREAD_ATTACH`.
- Lista completa de imports do motor e do FH2: `../AzimuthUwp/azimuth-original-audit.txt` e `../AzimuthUwp/fh2-bindings.tsv`. Código decompilado: `../azimuth-analysis`.
