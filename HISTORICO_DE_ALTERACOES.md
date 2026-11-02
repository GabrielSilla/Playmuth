# Histórico de alterações — Azimuth_PS5

Atualizado em **28/10/2026**. Projeto: `C:\Projects\Azimuth_PS5`.

Este documento consolida a implementação, as correções, as tentativas de diagnóstico e os resultados registrados no projeto. Distingue o que passou no PC, o que foi observado no PS5 e o que continua sendo hipótese. As versões do host, do GPU probe e do título têm numeração independente.

Não há repositório Git na raiz desta cópia. Portanto, este é um inventário técnico e cronológico baseado nos fontes, documentos, artefatos e relatórios disponíveis, e não um diff recuperado de commits. Os binários antigos não devem ser confundidos com o pacote atual.

## 1. Objetivo e estado ao encerrar esta etapa

O objetivo é executar o **Azimuth 0.9.7 original** diretamente no PS5, com um loader PE e adaptações de APIs do Windows, para posteriormente iniciar o Forza Horizon 2. A arquitetura adotada é um host nativo; o Wine não foi incorporado ao host. O RADV usado nos testes é uma biblioteca externa da instalação do Playden.

O executável original do motor foi mantido inalterado. SHA-256 esperado:

```text
5d3ce30b440dab2604fe42ff7cd129394f0b98418e22b2a44270f548199bc6a9
```

O jogo e o firmware não foram modificados nem incluídos no pacote. As APIs ainda sem implementação devem produzir uma parada diagnosticável ou um erro de ausência de suporte, sem simular objetos funcionais.

Situação atual:

- O loader e a camada de sistema levaram o motor original até o bloqueio de DXGI no PS5.
- O host chegou à versão **0.7.1**, com **224 dos 256 imports** do motor atendidos naquele marco. Isso não significa cobertura completa de Win32 ou das dependências do jogo.
- O título nativo `FAKE78002` instala, inicia e executou os seis testes básicos do smoke com sucesso na versão 0.2.
- No título, o RADV carrega pelo runtime do sistema, negocia a interface ICD, cria instância e dispositivo lógico e enumera a GPU.
- Nos relatórios até a versão 0.9, envio sem command buffers e envio com command buffer vazio concluíram com fence sinalizada.
- **A GPU executou comandos no PS5 no título 0.11:** escritas pela CP (WRITE_DATA e CP DMA) concluíram com o conteúdo correto. **A execução de compute shader ainda não funciona**: o preenchimento de 4096 bytes não conclui, e o título é encerrado.
- O último pacote gerado é o **título 0.10 / GPU probe 0.16**. No PS5 ele escolheu a memória tipo 3 (heap 1, flags `0x7`), e o resultado foi o mesmo do 0.9: o submit do preenchimento retornou 0, a fence continuou pendente e o relatório terminou depois da primeira pausa, sem conclusão. **A troca do tipo de memória não mudou o resultado.**
- No código do RADV de referência, `vkCmdFillBuffer` com tamanho **≥ 4096 bytes** numa GPU sem VRAM dedicada é executado por um **compute shader** (dispatch), e não por CP DMA. O teste sempre usou exatamente 4096 bytes; portanto, todos os preenchimentos testados até agora foram dispatches de compute. Essa é a principal diferença em relação ao command buffer vazio, que conclui. É uma hipótese a testar, não uma causa comprovada.
- Para testá-la foi gerado o **título 0.11 / GPU probe 0.17**. **No PS5, a GPU executou comandos pela primeira vez:** `vkCmdUpdateBuffer` (CP WRITE_DATA, 16 palavras) e `vkCmdFillBuffer` de 256 bytes (CP DMA, 64 palavras) concluíram em cerca de 5,7 ms, com todas as palavras corretas. O preenchimento de 4096 bytes, por **compute shader**, ficou pendente por pelo menos 351 ms (283 consultas, todas retornaram `VK_NOT_READY`). O processo terminou entre consultas, sem sinal e sem stderr. **O bloqueio está na execução de shaders**, e não no envio, nas fences ou na memória.
- O sintoma visual informado nos testes anteriores foi tela preta, depois “Aguarde” e aviso de que o aplicativo apresentou um erro, sem código numérico informado.

Esta solicitação passou a ser a documentação das alterações. Nenhum novo build de diagnóstico foi preparado para substituir o 0.10 durante a elaboração deste documento.

## 2. Estrutura criada e mantida

Os caminhos abaixo são relativos à raiz do projeto.

| Caminho | Implementação ou finalidade |
|---|---|
| `probe/` | Testes iniciais de execução x86-64, ABI, TEB/GS e memória, em payload e Linux |
| `host/` | Loader PE do motor, TEB/PEB, fronteira de ABI, APIs Win32, GDI e diagnósticos |
| `gpu-probe/` | Teste isolado de RADV/Vulkan, compartilhado com o título |
| `title/` | Aplicativo nativo `FAKE78002`, build, empacotamento, verificadores e controles de teste |
| `console-reports/` | Cópias dos resultados recebidos do PS5, organizadas por versão e etapa |
| `docs/evidencias.md` | Evidências anteriores do Playden e do port Xbox que orientaram o projeto |
| `docs/radv-runtime-options.md` | Investigação do carregamento do RADV e alternativa de integração estática |
| `README.md` | Arquitetura, política, plano e resultados históricos |
| `title/README.md` | Instruções do título, histórico dos diagnósticos e situação de cada versão |

## 3. Probe inicial: plataforma, ABI e memória

Arquivos principais: `probe/main.c`, `probe/msabi.c`, `probe/msabi.h`, `probe/Makefile`.

Foram implementados testes de memória RW→RX, chamada de código gerado, fronteira `ms_abi`, acesso ao TEB pelo registrador GS em várias threads e reservas nos endereços fixos usados pelo motor e pelo modelo de título Xbox.

No Linux e no PS5, o probe inicial passou em seis verificações. O teste do console comprometeu 6 GiB e demorou cerca de 68,9 s, durante os quais a interface ficou travada. Como o payload compartilha recursos de um processo do sistema, esse resultado **não mede o orçamento de memória de um título**.

Na versão 0.2 do probe de plataforma, o teste pesado de memória passou a depender da presença de `/data/azimuth-ps5/memory-test`. Os testes básicos do título posterior usam reservas pequenas, sem comprometer vários GiB.

Evidência: `console-reports/2026-10-26-probe-0.1/probe.txt`.

## 4. Loader do motor e runtime de execução

Arquivos principais: `host/src/PeImage.*`, `Image.*`, `Teb.*`, `Platform.*`, `Sha256.*`, `guest_abi.*`, `GuestThreads.*` e `main.cpp`.

Foram integrados:

- Leitura e validação do PE, SHA-256, layout do Azimuth 0.9.7 e load config.
- Mapeamento da imagem, relocação, permissões por região e base preferida `0x140000000`.
- TEB e PEB por thread, TLS estático no slot 0 e execução dos callbacks TLS antes do entry point original.
- Fronteira de chamadas Windows x64 para System V. O código `ms_abi` é compilado separadamente para FreeBSD, pois o alvo PS5 do SDK não aceita essa convenção diretamente.
- Resolução de imports e `GetProcAddress` a partir da tabela comum `host/src/win32/exports.inc`.
- Paradas que informam DLL, função, endereço do motor e valores encontrados na pilha como backtrace aproximado.
- Busca do motor em `/data/azimuth-ps5/azimuth.exe` e nas pastas `/mnt/usbN/PC/FH2/`.

### Correção de encerramento e GS — host 0.1 → 0.2

O host 0.1 desligou o console no teste. Ele registrava/notificava e chamava `_exit` ainda dentro da execução do motor, com GS apontando para o TEB, e usava pilha fornecida pelo host.

O 0.2 passou a retornar ao quadro do host por `longjmp`, restaurar GS antes de log/notificação/chamadas de encerramento, usar a pilha do sistema e terminar normalmente pelo `main`. Cada linha do relatório passou a ser sincronizada com `fsync`. O controle `host-stop` permite parar em `mapa`, `thread`, `gs` ou `callbacks`.

No PS5, o 0.2 validou o motor, executou dois callbacks TLS e o entry point original, parando em `KERNEL32.dll!GetSystemTimeAsFileTime` e encerrando normalmente.

### Threads, sinais e PEB

`GuestThreads` passou a fornecer contexto de convidado por thread, TLS callbacks de entrada/saída, destrutores FLS e retorno controlado para o host em `ExitThread` ou parada global. As threads são anunciadas antes da criação e têm o início registrado; o host espera por elas antes de terminar.

O host 0.5 falhou sem diagnóstico quando as primeiras threads passaram a executar em paralelo. O 0.6 acrescentou captura de sinais fatais e melhor coordenação da parada. O resultado no PS5 passou a coincidir com o Linux até `GetSystemMetrics`.

Também foi adicionado `ProcessParameters` ao PEB: o UCRT consulta esse bloco ao iniciar threads; sem ele, uma thread acessava o endereço 8. A correção do layout real de contexto de sinal do PS5, descoberta no GPU probe, foi aplicada também a `GuestThreads.cpp`.

## 5. Camada Win32 implementada

| Arquivo em `host/src/win32/` | Alterações consolidadas |
|---|---|
| `Kernel32.cpp` | Horários e contadores; IDs; último erro; informações de CPU; seções críticas e SRW; TLS/FLS; heap; startup info; ambiente; linha de comando; code pages; handles de módulos do host |
| `Locale.cpp` | Classificação e conversão de caracteres; comparação ordinal; locale en-US |
| `Exceptions.cpp` | Localização da imagem por endereço e diagnóstico de exceções; não equivale a um subsistema completo de exceções Windows |
| `Handles.cpp` | Tabela compartilhada de handles e conversão de `errno` para erros Win32 |
| `Files.cpp` | Mapeamento de unidades/caminhos Windows; abrir/fechar; ler/escrever; posição/tamanho; informações; enumeração com curingas; criação, remoção, renomeação, cópia e espaço livre |
| `Profile.cpp` | Leitura/gravação de INI, UTF-16 e gravação por temporário seguido de `rename`, com semântica portada do AzimuthUwp |
| `Threads.cpp` | Threads, eventos, timers, esperas, condições sobre SRW, `InitOnce`, `WaitOnAddress`, `Sleep`, callbacks de pool e informações de CPU/memória |
| `Memory.cpp` | `VirtualAlloc`, `VirtualFree`, `VirtualProtect` e `VirtualQuery` sobre memória POSIX; reservas alinhadas a 64 KiB e commit por página |
| `User32.cpp` | Classes e janelas virtuais, mensagens, foco/ativação, timers, callbacks e monitor virtual de 1920×1080; entrada de controle ainda não integrada |
| `Gdi.cpp` | DCs, DIBs, cópias e escalonamento de imagem, alpha blend, recorte, texto e estados de desenho |
| `Bcrypt.cpp` | Seis funções de hash com SHA-256/SHA-1 do host; algoritmos/HMAC não suportados param com diagnóstico |
| `D3d12.cpp` | Respostas de ausência de suporte para DXGI/D3D12; não cria fábrica, adaptador ou dispositivo fictícios |

Os caminhos `E:\PC\FH2` e `E:\` são resolvidos sobre o disco do motor; acessos fora das unidades atendidas são recusados e registrados. Algumas operações possuem limites explícitos: por exemplo, E/S `OVERLAPPED` é concluída imediatamente e `MessageBoxW` atende o fluxo com botão OK, registrando/notificando a mensagem.

Em `guest_abi.c` também entraram callbacks de `WNDPROC`/`TIMERPROC` e captura de `CONTEXT` x64 para `RtlCaptureContext`.

### Marcos do host

| Versão | Resultado ou alteração principal |
|---|---|
| 0.3 | 76/256 imports; CRT inicializou; parada em `GetFullPathNameW`, reproduzida no PS5 |
| 0.4 | 106/256; arquivos e INI; criação/acesso a `azimuth.log`; parada em sincronização |
| 0.5 | 144/256; threads e sincronização; Linux chegou a `GetSystemMetrics`; PS5 falhou sem diagnóstico |
| 0.6 | Coordenação de threads e sinais; PS5 chegou a `GetSystemMetrics` com saída normal |
| 0.7 | 216/256; USER32, GDI e memória; motor criou janela e parou na fábrica DXGI; PS5 também revelou uso de BCrypt pela checagem da pasta do jogo |
| 0.7.1 | 224/256; hashes BCrypt; PS5 chegou ao mesmo bloqueio DXGI do Linux e encerrou normalmente |

### Vulkan para módulos do Windows (fase 3, passo 1)

O host passou a oferecer um `vulkan-1.dll` para módulos do Windows como o vkd3d-proton, que carregam o Vulkan com `LoadLibrary` + `GetProcAddress("vkGetInstanceProcAddr")` (análise em `docs/fase3-d3d12.md`).

| Arquivo | Conteúdo |
|---|---|
| `host/third_party/vulkan/` | Cabeçalhos Khronos 1.4.354 (`vulkan_core.h`, `vk_platform.h`, `vk_video/`), sem alterações, com origem e hashes no README |
| `host/tools/gen_vulkan_commands.py` | Gera `host/src/gpu/vulkan_commands.inc` a partir do `vulkan_core.h`: 763 comandos |
| `host/src/gpu/vulkan_abi.c` | Um adaptador Windows x64 → System V por comando, compilado para FreeBSD como o `guest_abi.c`. `VkAllocationCallbacks` do chamador vira nulo, porque os callbacks estão na convenção do Windows |
| `host/src/gpu/Vulkan.cpp` | `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr` do `vulkan-1.dll`: guardam a função do driver no adaptador e devolvem o adaptador. Escondem `VK_EXT_debug_utils`, `VK_EXT_debug_report` e `VK_EXT_device_memory_report` (passam callbacks ao driver). `GetProcAddress` no `vulkan-1.dll` também resolve os comandos globais |
| `host/src/gpu/Radv.cpp` | O driver: no título, o RADV do Playden com as regras validadas nos títulos 0.14–0.17 (build conferido por tamanho e SHA-256, construtores pela entrada `+0x4ef0`, módulo nunca descarregado); no Linux, o `libvulkan.so.1`; num payload, nenhum |

`make test-vulkan` (no Linux, sobre o lavapipe) carrega o `vulkan-1.dll` como um módulo do Windows faria e chama tudo pelos adaptadores: instância com alocador do chamador, extensões filtradas e `VK_INCOMPLETE`, dispositivo, `vkCmdUpdateBuffer` + `vkCmdFillBuffer`, fence com `uint64_t` na pilha. 61 verificações passaram, com 0 de 1024 palavras erradas. No console, esse caminho ainda não foi exercitado: nada carrega o `vulkan-1.dll` até o vkd3d-proton entrar (passo 2).

### vkd3d-proton carregado pelo host (fase 3, passo 2)

O host passou a carregar DLLs do Windows de arquivos, além do motor, e a oferecer um DXGI próprio. Isso basta para o vkd3d-proton do Playden criar um `ID3D12Device`.

| Arquivo | Conteúdo |
|---|---|
| `host/src/Modules.cpp` | Loader de DLLs: só `d3d12` e `d3d12core`, procurados em `/data/azimuth-ps5/dll` e na instalação do Playden (no Linux, em `AZIMUTH_DLL_DIR`). Mapeia com o `Image` do motor, liga os imports ao host (o resto vira parada de diagnóstico com o nome do import), dá índice e blocos de TLS a cada thread, roda callbacks TLS e `DllMain` sem segurar o lock do loader (uma parada dentro deles desfaz a thread com `longjmp`), lê os exports e segue forwarders. O `HMODULE` é a base |
| `host/src/GuestThreads.cpp` | O motor passou a ser opcional; cada thread recebe os blocos TLS das DLLs e `DLL_THREAD_ATTACH`/`DETACH` |
| `host/src/win32/Kernel32.cpp` | `LoadLibraryA/W/ExW`, `GetModuleHandleA/W/ExA/ExW`, `GetModuleFileNameA/W` e `GetProcAddress` sobre as DLLs carregadas. Uma DLL de arquivo tem prioridade sobre a biblioteca `d3d12` do host, que só atende os imports do motor |
| `host/src/win32/Msvcrt.cpp` | O `msvcrt.dll` que o mingw-w64 importa (87 funções), no layout do Windows: `long` de 32 bits, `wchar_t` de 16, `FILE*` opaco, `printf` com o `va_list` do Windows. `stdout`/`stderr` viram linhas do `host.txt` |
| `host/src/win32/Advapi32.cpp` | Registro sem chaves e `D3DKMT*` sem suporte |
| Kernel32/Threads/Files | `InitializeCriticalSection`, `TryEnterCriticalSection`, variáveis de ambiente das DLLs, `InitOnceExecuteOnce` (callback na convenção do Windows), semáforos, `SleepConditionVariableCS`, `CreateEventA`, `CreateFileA` e afins. Mapeamento de arquivo e `DeviceIoControl` respondem "não suportado" |
| `host/src/gpu/Dxgi.cpp`, `dxgi_abi.c` | Fábrica (`IDXGIFactory` a `6`) e adaptador (`IDXGIAdapter` a `4`) com vtables na convenção do Windows; o adaptador descreve o dispositivo Vulkan do driver (nome, vendor, device, memória). GUIDs conferidos contra os binários do DXVK e do vkd3d-proton. Ainda sem saídas nem swapchain. `CreateDXGIFactory1/2` devolvem essa fábrica |
| `host/src/gpu/Vulkan.cpp` | Oferece `VK_KHR_win32_surface` (e `VK_KHR_surface`, se o driver não tiver), que o vkd3d-proton exige, e as tira da lista antes do `vkCreateInstance` do driver; `vkCreateWin32SurfaceKHR` ainda devolve erro |
| `host/src/gpu/D3d12Check.cpp`, `host --d3d12`, `title-mode=d3d12` | Verificação sem o motor: `D3D12CreateDevice`, fila direta, alocador, lista vazia, `ExecuteCommandLists`, `Signal` e fence |

No Linux (lavapipe), `host --d3d12` **passou**: as duas DLLs carregaram na base preferida (68/68 e 179/183 imports no host), o vkd3d-proton 3.0.1 criou instância e dispositivo, iniciou as threads `vkd3d_queue`, `vkd3d_fence` e de cache em disco, e a fence chegou a 1. O log do próprio vkd3d-proton aparece no `host.txt` como `[stderr]`. Título 0.19 tem o modo `d3d12`; ainda não testado no console.

## 6. GDI, fontes e quadros

Em `host/src/gfx/Font.*`, foi integrado rasterizador TrueType com métricas, glifos compostos e cobertura por área; sem hinting/ClearType. Em `Png.*`, foi implementada gravação PNG RGB com deflate stored, sem exigir zlib.

O host procura `arial.ttf` e `arialbd.ttf` nas pastas de fontes documentadas. No PS5, foi encontrada a fonte no disco USB. O caminho de captura grava `/data/azimuth-ps5/frame.png` por temporário e `rename` quando há um quadro novo.

No Linux, o teste GDI passou em **37 verificações**, com TEB pelo GS e callback `ms_abi`, e gerou uma imagem de teste do launcher. Isso não significa que o motor original já pintou o launcher neste port: ele pede DXGI/D3D12 antes de produzir esse quadro. Até o bloqueio registrado, não havia frame do motor para apresentar.

## 7. GPU probe em payload: alterações 0.1–0.8

Arquivo central: `gpu-probe/main.c`. O teste carrega RADV, resolve a interface Vulkan, cria instância/dispositivo e tenta preencher 4096 bytes com `0xa5c3e10f`, conferindo 1024 palavras na CPU.

| GPU probe | Mudanças e observações |
|---|---|
| 0.1 | Implementação inicial; passou no Linux com Vulkan/lavapipe. No PS5, parou no carregamento do RADV sem diagnóstico de falha |
| 0.2 | Tratadores de sinais, escrita direta do diagnóstico, watchdog e carregamento explícito das dependências AGC/VideoOut |
| 0.3 | Dump de registradores, módulos e pilha; falhas de calibração; tentativa de pilha alternativa. O PS5 falhou já na calibração |
| 0.4 | Calibração por etapas, recuperação editando o contexto e registro de entrada no tratador. O PS5 repetiu a falha porque o layout do contexto estava incorreto |
| 0.5 | Dump bruto do contexto e proteção contra repetição. A análise revelou os offsets reais dos registradores e que a pilha alternativa era problemática nesse fluxo |
| 0.6 | Uso do contexto real em `ucontext+0x40`, RIP em `+0xe0`, RSP em `+0xf8`; calibração recuperada; pilha alternativa opcional; recriação do relatório |
| 0.7 | Mapeamento por `__sys_dynlib_load_prx`, inspeção interna e tentativa de entrada manual em `base+0x4ef0`; controle `carregado`. Uma execução ficou presa ao carregar dependência, com suspeita de estado residual de teste anterior |
| 0.8 | Recuperação e limpeza dos módulos; controles de processos anteriores; diagnóstico confirmou falha de busca de instrução na entrada do RADV no payload |

A tentativa de localizar/encerrar probes anteriores foi limitada pelo layout divergente de `kinfo_proc` no PS5. No título, esse encerramento por nome/PID foi desativado para não atuar em processos com informações não confiáveis.

As calibrações provocam deliberadamente escrita em `0x10` e chamada nula e recuperam o controle. **Esses sinais no início do relatório não são, por si só, o erro do RADV.** Os valores iniciais de RIP obtidos com o layout errado do SDK foram superados pela calibração posterior.

Conclusão operacional: o RADV foi mapeado no payload, mas sua primeira instrução não pôde ser executada naquele contexto, apesar do segmento aparecer executável. O trabalho de GPU passou para um título nativo. Não foi implementada uma solução de GPU em payload.

## 8. Aplicativo nativo `FAKE78002`

O diretório `title/` foi criado para reunir smoke, host e GPU probe em um eboot. O title ID é diferente do Playden (`FAKE78001`). A base do eboot é `0x100000000`, abaixo da base preferida do motor, e o linker/validador verificam o layout sem regiões simultaneamente graváveis e executáveis.

| Arquivo | Alterações |
|---|---|
| `main.cpp` | Startup do aplicativo, `__dso_handle`, seleção de modo, relatório `title.txt`, smoke de C++/threads/TLS/memória e diagnóstico de sinal |
| `build.sh`, `Makefile` | Compilação do host e GPU probe com `AZIMUTH_TITLE`, fronteira ABI separada, link nativo, conversão e geração do SELF |
| `prepare.sh` | Preparação e verificação da base pública fixada e do runtime acompanhante |
| `eboot.ld` | Linker de base alta, com símbolos de limites de unwind requeridos pelo libunwind |
| `eboot.map` | Símbolos do executável locais para atender às restrições do conversor |
| `runtime/emutls.c` | Ajuste da ordem de destruição do TLS emulado no título |
| `check_runtime.py` | Verificação de imports de memória/GS, ausência de syscall direta, ausência de relocação nula relativa e de macros de FILE incompatíveis no objeto GPU |
| `verify.py` | Verificação de ELF/SCE, arquitetura, base, segmentos, alinhamento, entrada e SELF |
| `package.py` | Manifesto com versão/hashes, licenças e ZIP da pasta instalável |
| `deploy.py` | Ferramenta FTP com validação de hashes e dry-run; o usuário realizou os envios/testes manualmente |
| `make_icon.py`, `sce_sys/` | Ícone de bússola e metadados do título, incluindo incremento de contentVersion |
| `console-controls/` | Arquivos prontos para selecionar GPU e paradas de mapeamento/módulo/instância |
| `tests/` | Regressão de destrutores TLS e testes Linux do GPU probe e de seus diagnósticos |

Foi usado o startup nativo da base pública, sem `crt1.o` de payload. O link prioriza as implementações C++ estáticas e as bibliotecas de sistema antes da biblioteca C de compatibilidade do SDK.

### Correções de runtime — título 0.1 → 0.2

O 0.1 iniciou, mas não executou o destrutor TLS e parou no primeiro teste de memória. Foram feitas as seguintes correções:

- `mmap`, `mprotect`, `munmap` e `sysarch` passaram a ser imports de sistema, evitando versões de compatibilidade com syscall direta.
- `host/src/Platform.cpp`, no build de título, usa `sysarch` importado para ler/escrever GS.
- `__cxa_thread_atexit_impl` foi definido como zero para selecionar o fallback pthread do libc++abi.
- `emutls.c` ganhou uma rodada adicional de destruição pthread no título, mantendo o armazenamento TLS vivo até os destrutores C++ terminarem.
- O teste Linux usa `-no-pie` porque o GNU ld, nesse caso, produz uma relocação relativa de zero sob PIE; o link PS5 com lld é verificado separadamente.
- O smoke registra a etapa antes de cada reserva de memória e captura sinais com o layout medido do PS5.

A regressão reproduziu **0/64 destrutores** antes do ajuste e **64/64**, com ordem e valores corretos, depois. No PS5, o título 0.2 passou nos seis testes: construtores, exceções/unwind, isolamento TLS, destrutor TLS e reservas em `0x140000000` e `0x1000000000`.

## 9. Carregamento do RADV no título

O probe procura o módulo em `/data/azimuth-ps5/radv/libvulkan.prx` e depois na instalação do Playden em `/data/homebrew/FAKE78001/win/wine/lib/wine/x86_64-unix/libvulkan.prx`.

Foram adotadas as APIs internas de Sysmodule para dependências, em lugar de assumir que os caminhos absolutos dos módulos do sistema funcionariam no título:

| Dependência | ID |
|---|---|
| libSceAgcDriver | `0x80000080` |
| libSceAgc | `0x80000094` |
| libSceVideoOut | `0x80000022` |

O probe consulta se cada dependência já está carregada e só tenta descarregar aquilo que ele próprio carregou. Nos testes de título elas já estavam carregadas.

Outras alterações importantes:

- Uma parada planejada em `carregado` passou a terminar como parada válida, em vez de falso resultado de falha.
- A obtenção do descritor em C passou a chamar **`(fileno)(report)`**, evitando a macro do SDK que acessava campos privados de um `FILE` incompatível com a libc nativa. No console, o descritor real registrado foi 12.
- Registradores capturados são preservados e detalhados depois do retorno ao quadro normal do host.
- Inspeção de segmentos, informações internas de módulo e `sceKernelGetModuleInfoFromAddr` para nome/base/offset das instruções capturadas.
- Na versão 0.5 do título, o caminho de execução voltou a **`sceKernelLoadStartModule`** normal. O mapeamento puro continuou reservado a `gpu-stop=carregado`; o caminho histórico de payload ficou separado.

O mapeamento bruto seguido de entrada manual falhou no título 0.4 com SIGSEGV, leitura de `0x70`, RIP `0x800041aeb`, fora do RADV. O carregamento normal no 0.5 resolveu aquela etapa: entrada 0, exports resolvidos, ICD versão 5 e descarregamento 0. Isso não identifica a função exata responsável pela falha antiga.

No teste de instância do 0.5, Vulkan 1.4.354 enumerou `PlayStation 5 GPU (RADV NAVI21)`, vendor `0x1002`, dois heaps e duas famílias de filas. Os tamanhos anunciados dos heaps (4096 e 8192 MiB) **não comprovam o orçamento real disponível para este título**.

## 10. Histórico dos títulos e testes de envio

| Título / GPU probe | Alteração | Resultado registrado no PS5 |
|---|---|---|
| 0.1 | Primeiro pacote nativo, smoke e modos host/GPU | Iniciou; destrutor TLS não rodou; parou no teste de memória |
| 0.2 | Correções de runtime, TLS e memória | Smoke 6/6; teste GPU inicial expôs caminhos de dependências e resultado incorreto da parada planejada |
| 0.3 | Sysmodule por IDs e correção da parada de mapeamento | Mapeamento passou; entrada manual falhou, ainda sem diagnóstico suficiente |
| 0.4 / 0.10 | Correção de `fileno` e captura preservada | Registrou SIGSEGV por leitura de `0x70` fora do RADV |
| 0.5 / 0.11 | Carregamento normal pelo runtime e inspeção de endereço | Módulo/ICD passaram; instância e enumeração passaram. Teste completo criou dispositivo e buffer, mas parou antes de registrar conclusão do envio/espera |
| 0.6 / 0.12 | Logs separados para submit/wait, checagem de retornos, SIGSYS, reinstalação dos handlers após criar dispositivo, watchdog com atomics, escrita direta e join | Submit retornou 0 em 0,05 ms; último registro na entrada de `vkWaitForFences` com timeout de 2 s |
| 0.7 / 0.13 | Troca de espera bloqueante por `vkGetFenceStatus`, polling de 1 ms por até 2 s entre chamadas que retornam, confirmação de início do watchdog | Submit 0, primeiro status `VK_NOT_READY`; sem conclusão ou fim do título |
| 0.8 / 0.14 | Pausa antes do RADV, envio sem command buffers, logs antes/depois da primeira pausa de polling | Pausa e envio vazio passaram; preenchimento permaneceu pendente; primeira pausa retornou 0 |
| 0.9 / 0.15 | Teste de command buffer vazio e barreira `TRANSFER_WRITE → HOST_READ` após fill | Command buffer vazio passou; preenchimento continuou sem conclusão registrada, usando tipo 2 |
| 0.10 / 0.16 | Preferência por memória `DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT`, com fallback; log de tipo/heap/flags | Tipo 3 escolhido (heap 1, flags `0x7`, mapeado em `0x4000000000`). Envio vazio e command buffer vazio concluíram; com o preenchimento, o submit retornou 0 em 0,03 ms, a fence estava `VK_NOT_READY` e a primeira pausa retornou 0. Não houve registro posterior |
| 0.11 / 0.17 | Três escritas pela GPU, cada uma com buffer, command buffer e fence próprios: `vkCmdUpdateBuffer` de 64 bytes (CP WRITE_DATA), `vkCmdFillBuffer` de 256 bytes (CP DMA) e de 4096 bytes (compute). A primeira sem conclusão encerra o probe. O polling registra as dez primeiras consultas antes e depois da chamada e, depois, uma a cada 100 ms; a etapa do watchdog é definida uma vez por polling. `contentVersion` 01.000.010 | 4a e 4b concluíram; **4c (CP WRITE_DATA) e 4d (CP DMA) concluíram com todas as palavras corretas**. 4e (compute): submit 0, fence `VK_NOT_READY` em todas as 283 consultas até 351 ms; a última consulta registrada retornou normalmente, e o processo terminou antes do registro seguinte (cerca de 100 ms depois), sem sinal |
| 0.12 / 0.18 | `RADV_DEBUG=metashaders,asm` definido com o `setenv` da libc do sistema antes de `vkCreateInstance` e conferido com `getenv`; cópia da janela dos shaders (`0x200000000` + 4 MiB) para `gpu-shader-window.bin` com `pwrite` página a página (página ilegível = `EFAULT`, sem falha) depois de gravar o preenchimento por compute, com as 16 palavras em torno do PC `0x20004001C` no relatório. `contentVersion` 01.000.011 | `setenv` funcionou (`getenv` devolveu o valor). **O RADV caiu na CPU ao gravar o preenchimento por compute**: SIGSEGV lendo o endereço 0 em `libSceLibcInternal.sprx+0x4953`, durante o dump do shader, antes da cópia da janela e de qualquer envio. O probe recuperou a falha, descarregou o RADV (0) e o título terminou normalmente; o klog não registrou falha de GPU. Sem desmontagem nem cópia da janela |
| 0.13 / 0.19 | Remove o `RADV_DEBUG` (o dump do RADV falha nesse runtime) e mantém a cópia da janela dos shaders para `gpu-shader-window.bin` depois de gravar o preenchimento por compute. `contentVersion` 01.000.012 | Cópia da janela: 612 páginas legíveis, incluindo `0x200040000..0x200080000`. O klog repetiu `ILLEGAL_INST` no PC `0x20004001C`. **O código ali tem todos os opcodes zerados**: `s_endpgm` aparece como `s_nop 0` (`0xbf800000`), `global_store_dwordx4` como `0xdc008000`, `s_mov_b32`/`v_mov_b32` com opcode 0; operandos (`v[4:7]`, `s[2:3]`) e o preenchimento `s_code_end` corretos |
| 0.14 / 0.20 | Depois do `sceKernelLoadStartModule`, confere a flag de construtores do RADV (`+0x17f3278`) e as tabelas de opcodes do ACO (`+0x17fa088`). Se a flag estiver zerada, chama a entrada do RADV (`+0x4ef0`), que roda o `INIT_ARRAY`, e registra flag e tabelas antes e depois. Corrige a releitura do PC (arquivo aberto para leitura e escrita). `contentVersion` 01.000.013 | **Construtores executados**: flag 0 → 1, tabelas de opcodes 0 → `ffff0032 00390052`. 4c e 4d concluíram de novo. 4e falhou igual (klog: `ILLEGAL_INST` em `0x20004001C`), e a janela copiada é **idêntica byte a byte à do 0.13**. Causa: o RADV usa o cache de shaders em disco `/app0/radv-shader-cache` (no título, `/mnt/usb1/FAKE78002/radv-shader-cache`). A única entrada de `radv_builtin_shaders`, gravada em 27/10 às 21:21 (primeiro teste completo, 0.5), contém o shader com opcodes zerados, e o RADV a reaproveita em vez de recompilar |
| 0.14 / 0.20, cache apagado | Mesmo eboot, com `/mnt/usb1/FAKE78002/radv-shader-cache` apagado pelo usuário | **[PASSOU] — a GPU executou o compute shader no PS5.** Construtores executados (flag 0 → 1). 4a–4e concluíram; 4e (compute, 4096 bytes) com fence sinalizada em 5,71 ms e **0 de 1024 palavras diferentes**; `vkDeviceWaitIdle` 0. O shader em `0x200040000` agora é o preenchimento correto para gfx1030 (`v_lshl_add_u32`, `v_mov_b32` ×4, `v_lshlrev_b32`, `v_min_u32`, `global_store_dwordx4`, `s_endpgm`), e o RADV gravou um cache novo. Sem falha de GPU no klog. **Pendente:** depois de `Fim do titulo: codigo=0`, o `exit()` da libc rodou handlers de `atexit`; um falhou, o tratador do probe chamou `_exit(3)`, que retornou durante a saída, e o `ud2` seguinte em `onFault` gerou SIGILL ("App Crash reason=0x4"). Hipótese: handlers registrados pelo RADV (construtores e `util_queue` do Mesa) apontam para o módulo, que o probe descarrega antes do `exit`. O 0.12 teve a mesma saída |
| 0.15 / 0.21 | O RADV iniciado pelo runtime não é mais descarregado no fim do probe: fica carregado até a saída do processo, porque os handlers de `atexit` que ele registra precisam do seu código no `exit()`. `contentVersion` 01.000.014 | **[PASSOU] de novo** (16, 64 e 1024 palavras corretas; RADV mantido carregado). Na saída, o klog mostra primeiro `exit() exit_value=0` (os handlers de `atexit` agora rodaram), depois `exit_value=3` duas vezes e SIGILL em `0x1004047b6` (`ud2` depois do `_exit(3)` em `onFault`). Ou seja, a thread principal recebeu outro sinal depois do `exit(0)`, e o tratador do probe chamou `_exit(3)`, que retornou. O relatório do tratador se perdeu porque o arquivo já estava fechado; não se sabe qual sinal foi |
| 0.16 / 0.22 | O relatório do probe fica aberto até o fim do processo (sem `fclose`), e a etapa passa a "saida do processo". Um sinal recebido depois do fim do probe é registrado (sinal, código, `rip` e módulo, endereço, `rsp`, pid de origem e 16 posições da pilha) e devolvido ao comportamento padrão com `SIG_DFL`, sem `_exit(3)`. `contentVersion` 01.000.015 | `[PASSOU]` de novo. O probe terminou e deixou o relatório aberto, e nenhum `SINAL NA SAIDA` foi registrado. O klog mostra só `exit() exit_value=0` (sem os `exit_value=3` do probe) e, em seguida, SIGILL "privileged instruction fault" em `0x8000ba9bf`, dentro do `exit()` da `libSceLibcInternal`, com `r15` na `libkernel` e nenhum frame do título além do `start`. Isso é compatível com a syscall de saída voltando ao processo e a libc executando uma instrução privilegiada (provavelmente `hlt`). A causa não foi identificada |
| 0.16 / 0.22, modo smoke | Mesmo eboot, com `title-mode=smoke` (sem RADV nem GPU) | Smoke passou (`codigo=0`), mas a saída também falhou: `exit() exit_value=0`, depois `0x8c` (o tratador do smoke recebeu SIGSYS e chamou `_exit(140)`), `0x84` (SIGILL) e SIGILL no eboot ("App Crash reason=0x4"). **O erro na saída não depende da GPU.** A base pública do título documenta: "Do not return from `main` or call an exit function" (`docs/TROUBLESHOOTING.md`), e o esqueleto mantém o `main` vivo (`docs/DEPLOYMENT.md`). O `_start` da base chama `exit(status)` quando o `main` retorna. Provavelmente o smoke do 0.2 também terminava assim, sem que isso fosse visível sem o klog |
| 0.17 / 0.22 | O `main` do título não retorna mais: ao terminar, grava `Fim do titulo` e `Titulo aguardando ser fechado pelo menu do sistema` no `title.txt`, mostra a notificação "teste terminado (codigo N); feche o aplicativo pelo menu do PS5" e fica em `sleep`, em todos os modos. O GPU probe não mudou. `contentVersion` 01.000.016 | Primeira execução em modo smoke (o `title-mode` ainda estava em `smoke`): verificações passaram, `title.txt` terminou com a linha de espera, e o usuário fechou pelo menu. O klog mostra `KillApp()` pedido pelo sistema e `ProcessTerm()`, **sem `exit()`, SIGSYS, SIGILL nem "App Crash"**. Execução em modo `gpu`: **`[PASSOU]`** (16, 64 e 1024 palavras corretas, `vkDeviceWaitIdle` 0), `title.txt` com `codigo=0` e a linha de espera, notificação mostrada; o usuário fechou pelo menu. O klog mostra só `KillApp()`, `Kill App` e `ProcessTerm()`, **sem `exit()`, SIGSYS, SIGILL, falha de GPU nem "App Crash"**. **Primeiro teste completo do título com GPU sem erro do sistema** |
| 0.18 / 0.22 | Inclui no host o `vulkan-1.dll` para módulos do Windows (fase 3, passo 1; ver `docs/fase3-d3d12.md`). Os modos do título se comportam como no 0.17, porque nada carrega o `vulkan-1.dll` ainda. `contentVersion` 01.000.017 | Não precisa de teste no console; o caminho novo foi validado no Linux |
| 0.19 / 0.22 | Novo modo `title-mode=d3d12`: o host roda sem o motor e carrega o vkd3d-proton (`d3d12.dll`, `d3d12core.dll` da instalação do Playden) com o próprio loader, sobre o `vulkan-1.dll` e o DXGI do host; cria dispositivo, fila, alocador e lista, executa a lista e espera a fence. Relatório em `host.txt`. `contentVersion` 01.000.018 | **PS5: o `d3d12.dll` foi lido do Playden e mapeado em `0x35e4e0000` (68/68 imports), mas o host caiu em `d3d12+0x11fb` (`movl %edx,(%r12)`, o `DllMain` do mingw gravando o motivo), escrevendo em `0x35e4f10b4`, dentro da `.data`. Nenhum log do vkd3d-proton, nenhuma falha de GPU no klog** |
| 0.20 / 0.22 | Diagnóstico da falha do 0.19: depois de mapear uma DLL, o host sonda cada página (`write` para `/dev/null` testa leitura; `read` de `/dev/zero` testa escrita, com o byte restaurado) e registra `R-`/`RW` por seção; o tratador de falhas passa a registrar o código si, o trap e o err (escrita ou leitura). `contentVersion` 01.000.019 | **PS5: causa confirmada.** O PS5 protege memória em blocos de 16 KiB: em cada bloco vale a última proteção aplicada. `.data` (`0x11000`) ficou `R-`, `.edata` ficou `RW` e `.tls` ficou `R-`; a falha foi escrita (`err 0x7`) numa página presente. O `d3d12.dll` tem 3 blocos mistos e o `d3d12core.dll` 4, e em cada um deles um bloco junta `.text` e `.data` |
| 0.21 / 0.22 | A proteção das imagens passa a ser por bloco do tamanho da granularidade do sistema (`platform::protectionGranularity`, 16 KiB no PS5; no Linux, 4 KiB ou 16 KiB com `AZIMUTH_BLOCK16K`). Nas DLLs carregadas, um bloco com seções de proteções diferentes recebe a união delas, **inclusive gravável e executável (RWX) em 1 bloco por DLL**, com log; uma única página de 4 KiB gravável e executável continua recusada, e o motor continua recusando blocos mistos. `contentVersion` 01.000.020 | **PS5:** o `d3d12.dll` passou (blocos unidos, 1 RWX aceito pelo console, `DllMain` concluído). O `d3d12core.dll` (5,9 MB) não carregou: `std::bad_alloc` já na leitura do arquivo, porque o log "Carregando" nem apareceu; o `D3D12CreateDevice` terminou em `E_NOINTERFACE` |
| 0.22 / 0.22 | Os bytes das imagens PE passam a ficar num `std::vector` com alocador próprio (`LargeBuffer.h`): blocos a partir de 256 KiB vêm de `mmap` em vez do `malloc` do sistema. A leitura dos arquivos usa o tamanho do arquivo de uma vez, e o log "Lendo ... (N bytes)" aparece antes da leitura. `contentVersion` 01.000.021 | **PS5:** o `d3d12core.dll` carregou e inicializou (5.894.144 bytes, 179/183 imports). O RADV carregou pelo host (id 24786, construtores pela entrada `0x8011cef0`, ICD versão 5), a fábrica DXGI descreveu "PlayStation 5 GPU (RADV NAVI21)" (8192 MiB locais, 4096 compartilhados) e o vkd3d-proton 3.0.1 criou a instância Vulkan (`vkCreateInstance 0`). `D3D12CreateDevice` devolveu `0x8007000e` (`E_OUTOFMEMORY`) cerca de 30 ms depois, sem linha `[stderr]` explicando |
| 0.23 / 0.22 | No `msvcrt` do host, blocos de 1 MiB ou mais e qualquer bloco que o `malloc` do sistema recuse vêm de `mmap` (tabela para `free`/`realloc`; os 12 primeiros fracassos são logados com o tamanho). `calloc`, `_aligned_malloc` e `_aligned_free` seguem a mesma regra. Os comandos Vulkan que devolvem `VkResult` registram uma vez cada resultado negativo do driver (`Vulkan: <comando> devolveu N`). `contentVersion` 01.000.022 | **PS5: [PASSOU].** `D3D12CreateDevice` (FL 11_0) devolveu `S_OK`; o vkd3d-proton 3.0.1 criou fila direta, alocador, lista e fence sobre o RADV do PS5, executou a lista e a fence chegou a 1 em 0 ms. Detectou SM 6.8, DXR 1.1, `VK_EXT_descriptor_buffer` e `VK_EXT_mutable_descriptor_type`. Nenhuma linha de "falhou no malloc"; o único erro do driver foi `vkGetPhysicalDeviceImageFormatProperties2` devolvendo -11 (formato não suportado, sondagem normal) |
| 0.24 / 0.22 | O `D3D12CreateDevice` que o motor importa (ordinal 101) passa a encaminhar ao `d3d12.dll` do vkd3d-proton, carregado pelo host, com o adaptador que o DXGI do host devolveu; o motor agora pode criar o dispositivo D3D12. A verificação `--d3d12` usa o mesmo caminho (fábrica, `EnumAdapters1`, `D3D12CreateDevice` do host). `D3D12GetDebugInterface` continua `E_NOINTERFACE`. `contentVersion` 01.000.023 | Passou no Linux (4 KiB e 16 KiB simulado). **PS5, primeira tentativa de `title-mode=host`:** "azimuth.exe nao encontrado". O arquivo existe em `/mnt/usb1/PC/FH2` (3.087.872 bytes), mas o título (sandbox) provavelmente não acessa o `/mnt/usb1`; o `usbN` da mensagem é só rótulo. **Segunda tentativa, com o exe copiado para `/data/azimuth-ps5` (3.087.872 bytes, modo 0777): a mesma mensagem.** Portanto a sandbox não era a causa, e o `access(2)` do título provavelmente responde errado (o `fopen`, o `stat` e o `ifstream` leem o mesmo caminho). Contorno anterior: cópia do exe em `/data/azimuth-ps5/azimuth.exe` (feita pelo Prospero Manager, o original intacto). **Pendente (opção 2):** registrar o `errno` de cada candidato e descobrir como o título alcança o pendrive, para o motor ver a pasta do jogo |
| 0.25 / 0.22 | O host deixa de usar `access(2)` (no motor e na procura de fontes do GDI) e testa a leitura com `fopen` (`platform::readable`); a mensagem de "não encontrado" lista o `errno` de cada candidato. `contentVersion` 01.000.024 | **PS5:** o motor foi achado em `/mnt/usb1/PC/FH2` (a cópia em `/data` já não estava lá), com o hash certo; `access(2)` era o defeito, e o título lê o `/mnt/usb1`. O mapeamento então parou em "Bloco de protecao com secoes de protecoes diferentes; recusado" (o motor tem 3 blocos de 16 KiB mistos: `.text`+`.rdata` em `0x220000`, `.data`+`.rdata` em `0x2bc000` e `.data`+`.pdata` em `0x328000`; nenhum junta escrita e execução) |
| 0.26 / 0.22 | O motor passa a ser mapeado com blocos mistos recebendo a união das proteções (`sharedBlocks`), **sem** permitir bloco gravável e executável: o `Image` ganhou `allowWritableExecutable` (só as DLLs). O log lista os blocos unidos do motor. `contentVersion` 01.000.025 | **PS5:** o motor mapeou (3 blocos unidos), criou a janela e uma fábrica DXGI (`CreateDXGIFactory2` do motor passou pela primeira vez); depois o título fechou sozinho. O klog mostra `abort is called`: a pilha (simbolizada com o `llvm-pie.elf`, endereço = vaddr + `0x400000`) é `win_CreateCompatibleBitmap` → `operator new` → `__cxa_throw` → `std::terminate` → `abort`. Ou seja, `std::bad_alloc` no bitmap de 1920x1080 do launcher (≈8 MB) e a exceção não atravessa o quadro `ms_abi`. É o mesmo limite do `malloc` do título para blocos grandes que já tinha derrubado a leitura do `d3d12core.dll` |
| 0.27 / 0.22 | Alocador único do host (`HostHeap.cpp`): `operator new`/`delete` (todas as formas), o heap de processo do motor (`HeapAlloc` e afins) e o `msvcrt` das DLLs passam por ele. Blocos de 1 MiB ou mais, e qualquer bloco que o `malloc` recuse, vêm de `mmap`; todo bloco tem cabeçalho de 32 bytes (liberar, medir e mover sem tabela). Substitui a tabela do 0.23. `contentVersion` 01.000.026 | **PS5: sem queda.** O motor criou a janela e a fábrica DXGI, a thread "game folder check" rodou, e o RADV carregou pelo host (id 57554) quando o motor listou o adaptador ("PlayStation 5 GPU (RADV NAVI21)"). O host parou por diagnóstico, e o título terminou com `codigo=0`: "Incomplete: no Forza_Durango_Release_Final.exe in Mount". Antes disso, 4 `FindFirstFile` em `E:PCFH2FH2_Anthem_...Mount*` deram erro 5 (acesso negado), embora a pasta exista no pendrive com `forza_durango_release_final.exe` (minúsculas) e outros 7 arquivos listados pelo Prospero Manager |
| 0.28 / 0.22 | A listagem de pastas (`FindFirstFile`) passa por `listDirectory`: `opendir`/`readdir` e, se o título for recusado, `sceKernelGetdents` sobre um descritor da pasta (formato `dirent` do PS4/PS5). Registra o `errno` real da falha (até 8 vezes) e, quando o plano B funciona, quantos nomes leu. `contentVersion` 01.000.027 | **PS5: o motor chegou ao launcher e criou o dispositivo D3D12.** `opendir` falhou com `errno 1` (EPERM) na pasta `Mount`, e o plano B com `sceKernelGetdents` leu 20 nomes; a thread "game folder check" terminou normalmente. O RADV carregou pelo host, `D3D12CreateDevice` **do motor** devolveu `0x00000000` pelo vkd3d-proton (`d3d12.dll` e `d3d12core.dll` carregados), e o host gravou `frame.png` com o launcher do Azimuth (1920x1080, pasta do jogo detectada, "Forza Horizon 2", v0.9.7). Parada de diagnóstico seguinte: `d3d12.dll!D3D12SerializeRootSignature` (import 74), chamado de `engine+0xf65fb` |
| 0.29 / 0.22 | `D3D12SerializeRootSignature` do motor passa a encaminhar ao `d3d12.dll` do vkd3d-proton (o único import por nome do motor nesse DLL; os outros dois são `D3D12CreateDevice` e `D3D12GetDebugInterface`, por ordinal). `contentVersion` 01.000.028 | **PS5: sem parada.** `D3D12SerializeRootSignature` devolveu `0x00000000` (2 vezes); o motor leu `datache` (`opendir` com `errno 1`, `getdents` leu 9 e 8 nomes), não achou o cache de pipelines (`erro 2`) e criou mais threads (280 e 284), seguindo rodando sem mensagem de erro. **A tela do PS5 ficou preta:** o host só gravava o quadro do launcher em `frame.png`, sem apresentá-lo no VideoOut |
| 0.30 / 0.22 | Apresentação do host na tela (`gfx/VideoOut.cpp`): cada quadro novo da janela do motor (hoje o launcher em GDI) é copiado para um de dois framebuffers VideoOut de 16 MiB em memória direta, no layout de tiles de 64 KiB (128x128, B8G8R8A8 SDR, `0x8000000000000000`), com `clflush`, e apresentado com `sceVideoOutSubmitFlip` no vblank; a primeira chamada esconde a tela de abertura (`sceSystemServiceHideSplashScreen`) e abre o VideoOut. O loop do host passa a olhar quadros a cada 100 ms; o `frame.png` fica limitado a um a cada 2 s. O título passa a ligar `libSceVideoOut` e `libSceSystemService`. Receita e tiling da referência `PS5_Vulkan` (`src/demo_renderer.cpp`). `contentVersion` 01.000.029 | **PS5: o launcher apareceu na TV.** "VideoOut aberto: 2 framebuffers de 1920x1080 (B8G8R8A8 SDR, 64 KiB tiles), 32 MiB de memória direta" e "primeiro quadro apresentado". Logo depois, parada de diagnóstico: `KERNEL32.dll!RtlCaptureStackBackTrace` (import 156), chamado de `engine+0x16670` na thread 280 |
| 0.31 / 0.22 | 23 imports do motor sem máquina própria passam a responder como o Windows sem o recurso (`win32/Misc.cpp`, `Threads.cpp`): `RtlCaptureStackBackTrace` (0 quadros), `AddVectoredExceptionHandler` e `RtlAdd/DeleteFunctionTable` (aceitos, ainda não usados), `FlushInstructionCache`, ambiente (`Get/SetEnvironmentVariableW`), `GetSystemDirectoryW`, `FormatMessageW`, console ausente (`GetConsoleMode`, `WriteConsoleW`, `GetConsoleOutputCP`, `SetConsoleCtrlHandler`), `UnhandledExceptionFilter`, `GetQueuedCompletionStatus` (espera o prazo), `GetOverlappedResultEx`, `GetWriteWatch`, `CreateFileMappingW`, COM sem classes (`CoTaskMemAlloc/Free`, `CoInitializeEx`, `CoCreateInstance` com `REGDB_E_CLASSNOTREG`, `CoGetApartmentType`) e `SHCreateItemFromParsingName` (`E_NOTIMPL`). Continuam paradas de diagnóstico: `D3DCompile`, `RtlUnwindEx`, `RtlVirtualUnwind` e `RtlLookupFunctionEntry` (despacho de exceções). `contentVersion` 01.000.030 | **PS5: nova parada, agora do próprio motor.** A thread de trabalho 280 (rotina `engine+0x1e2940`) chamou o tratador de falha do motor: "Azimuth stopped: exception 0xc0000409 at 0x0 on thread 280: the C++ runtime's abort, where terminate ends (an exception nothing caught)" (o `azimuth.log` em `dataogs` traz o mesmo; o `crash dump` falhou com erro 21 porque o `dbghelp.dll` não existe). O host não logou nenhuma parada depois da mensagem e o `title.txt` ficou sem "Fim do titulo": o título parou de forma abrupta. Os frames de retorno apontam para o `raise`/`signal` do CRT do motor (`engine+0x1e9410`), isto é, um `abort()`; não se sabe quem o chamou. Nenhuma `RaiseException` chegou ao host, então não foi uma exceção C++ lançada |
| 0.32 / 0.22 | Cada thread do motor guarda as últimas 64 funções do host que chamou (`hostGateNamed`); o relatório de parada e a `MessageBoxW` do motor passam a listar as últimas chamadas da thread. `contentVersion` 01.000.031 | **PS5:** a mensagem do motor veio da thread 260 ("Azimuth end", o relator de falhas), cujas últimas chamadas são `IsDebuggerPresent`, `HeapAlloc`/`HeapFree`, `WideCharToMultiByte` e `MessageBoxW`, ou seja, só mostram o relatório, e não o que levou ao `abort()` da thread 280. O klog mostra o título **suspenso** (`gc_suspend_phase0_g2 : pid=322 done`), sem falha, sinal nem `abort` do sistema: o título não caiu, o erro é só a mensagem do motor |
| 0.33 / 0.22 | Os anéis de últimas chamadas ao host passam a ser guardados por thread durante toda a execução, e a `MessageBoxW` do motor lista as últimas 40 chamadas de **todas** as threads do motor (a thread que falha raramente é a que relata). `contentVersion` 01.000.032 | **PS5: causa encontrada.** O anel da thread 280 mostra a vida inteira dela: `GetLastError FlsGetValue FlsSetValue SetLastError EnterCriticalSection LeaveCriticalSection EnterCriticalSection LeaveCriticalSection RtlCaptureStackBackTrace ...` (a última parte é o relatório de falha). Desmontando o CRT do motor (`engine+0x1f6794`, o `__acrt_getptd`): se `FlsGetValue` devolve 0, o CRT chama `FlsSetValue(índice, -1)`; se isso falha (retorno 0), `rdi` fica nulo e o CRT chama `abort()` (`engine+0x1f6861`). Ou seja, `FlsSetValue` falhou na primeira chamada da thread. No host, `FlsSetValue` só falha se `flsData(true)` não conseguir o bloco de FLS, que era alocado com `std::calloc`, o `malloc` do título, o mesmo que já recusava blocos grandes e que o resto do host deixou de usar. Nenhuma `HeapAlloc` aparece na thread, coerente com a falha antes da alocação do bloco de dados por thread |
| 0.34 / 0.22 | O bloco de FLS de cada thread passa a vir de `heapAlloc` (e é liberado no fim da thread), e a `MessageBoxW` do motor registra quantos blocos o `malloc` do sistema recusou. `contentVersion` 01.000.033 | Passou no Linux; aguarda teste no PS5 (`title-mode=host`) |

Leitura do 0.11: nenhuma chamada ficou presa, porque a última consulta retornou. O processo foi encerrado de fora entre duas consultas, entre cerca de 350 e 450 ms depois do envio. A referência PS5_Vulkan registra o mesmo padrão no console: uma falha assíncrona da GPU encerra o título depois do comando que a causou, e o kernel log (klog) mostra a falha (`docs/RADV_PHASE.md`, `docs/M5_PHASE_B.md`, `docs/PROBE_MILESTONES.md`). Isso é compatível com uma falha de GPU durante o compute shader, mas só o klog da execução pode confirmar.

**Klog da repetição do 0.11 (klogsrv em `192.168.15.7:3232`), confirmado.** O processo do título (pid 233, vmid 1) recebeu a exceção `0xa0d0c012`, **`GPU_FAULT_WAVEFRONT_ERROR_ASYNC`**, enviada pelo sistema. Antes dela, o kernel listou quatro ondas com erro, em dois SAs, todas no mesmo ponto:

```text
PC=0x000000020004001C  STATUS=0x00012040  MODE=0x000001C0  TRAPSTS=0x40000800  ILLEGAL_INST
```

Depois o sistema gerou o coredump, reiniciou a GPU e encerrou o título. Leitura:
- **As ondas executaram código de shader e pararam numa instrução ilegal**, no byte `0x1C` (oitava dword) de um código em `0x2_0004_0000`. Não foi falha de acesso à memória (`MEMVIOL`), nem travamento da fila.
- O código está na janela de 32 bits (palavra alta 2), onde a referência espera os shaders. Isso **enfraquece a hipótese da janela**: o código foi colocado no lugar certo, mas a instrução nesse endereço não é aceita pela GPU.
- Restam duas explicações: o ACO gera, para "NAVI21", uma instrução que esta GPU não executa (a referência PS5_Vulkan descreve a GPU do PS5 como híbrida entre GFX10.3 e GFX1013 e registra instruções que não funcionam nela), ou a GPU lê nesse endereço um conteúdo diferente do que a CPU gravou.
- É preciso ver o código do shader de preenchimento: o próximo passo é ativar o dump do RADV (`RADV_DEBUG=metashaders,asm`) antes de criar a instância, para o `gpu-stderr.txt` mostrar a instrução no offset `0x1C`.

Evidência: `console-reports/2026-10-28-title-0.11-gpu-klog/klog.txt`.

O mesmo `libvulkan.prx` executa shaders no processo do Playden (o vkd3d-proton chegou ao launcher), então a diferença deve estar no ambiente do título, e não no driver em si. Candidatos, nenhum confirmado:
- **Janela de endereços de 32 bits dos shaders.** Na referência, shaders acessam código, tabelas de descritores e push constants por ponteiros de 32 bits com a palavra alta fixa (`address32_hi` = 2, janela `0x2_0000_0000`–`0x2_FFFF_FFFF`, onde o kernel coloca mapeamentos sem endereço). Um BO de 32 bits fora dessa janela faz o shader ler endereços errados e causa falha da GPU. Neste título, o heap da libc já ocupa essa janela (fences em `0x2000027ba0`); não se sabe como o winsys do RADV do Playden reserva a janela.
- **Inicialização feita pelo eboot do Playden** (ele importa Agc e AgcDriver diretamente) ou variáveis de ambiente definidas pelo Wine, que este título não reproduz.
- **Metadados do título** (`param.json`) diferentes dos do `FAKE78001`.

No 0.9, o buffer tinha 4096 bytes, alinhamento 16, máscara de tipos `0x000007ad`, tipo 2, heap 0, flags `0x6`, mapeado em `0x4000000000`. O tipo 3 também é permitido pela máscara e foi anunciado com heap 1 e flags `0x7`; por isso o 0.10 passou a priorizá-lo. O 0.10 usou de fato o tipo 3 e falhou da mesma forma, o que **afasta o tipo de memória como causa principal**.

Leitura do relatório do 0.10: o laço de `pollFence` chama `enter()` a cada consulta, o que zera o contador de etapa do watchdog. Por isso o watchdog nunca avisa durante o polling. O laço também só registra a primeira consulta e o resultado final. Como o resultado final após 2000 ms não aparece, o processo terminou dentro dessa janela (entre cerca de 0,48 s e 2,5 s), mas o relatório não mostra em qual consulta nem se alguma chamada travou. O `gpu-stderr.txt` ficou vazio, e nenhum sinal foi capturado. Isso é compatível com um encerramento do processo pelo sistema (ex.: falha da GPU detectada pelo driver), mas não comprova essa causa.

No Mesa de referência (`PS5_Mesa`, `src/amd/vulkan/meta/radv_meta_buffer.c`), `radv_prefer_compute_or_cp_dma` usa compute quando `size >= RADV_BUFFER_OPS_CS_THRESHOLD` (4096), exceto em GPU com VRAM dedicada. O PS5 se anuncia como GPU integrada, e o teste usa exatamente 4096 bytes. Logo, os preenchimentos do 0.5 ao 0.10 foram um dispatch de compute shader (meta shader compilado pelo ACO, enviado à memória da GPU e executado), enquanto o command buffer vazio não executa shader. O `libvulkan.prx` do Playden é Mesa 26.2.0, não necessariamente a mesma revisão da referência; o limiar de 4096 é antigo no upstream.

A barreira foi adicionada para estabelecer a dependência de memória entre escrita de transferência e leitura pela CPU após a fence, conforme os [exemplos de sincronização da Khronos](https://docs.vulkan.org/guide/latest/synchronization_examples.html). Sua ausência não foi comprovada como causa do fechamento, e o teste 0.9 continuou falhando.

### Limites dos mecanismos de diagnóstico

O watchdog tem prazo de etapa de 30 s e avisos periódicos de 2 s. Sua thread agora confirma que começou; ele usa escrita direta sem depender do mutex do logger e é encerrado com `pthread_join` no caminho normal.

O polling aplica o prazo apenas entre chamadas que retornam. Não garante interromper uma chamada interna bloqueada ou sobreviver a um encerramento do processo pelo sistema. A ausência de heartbeat ou de sinal capturado não identifica, sozinha, a causa.

Fence pendente ou erro no polling encerram por `_exit(5)`, evitando liberar recursos possivelmente ainda em uso ou entrar em `vkDeviceWaitIdle`. O caminho de sucesso registra comparação, limpeza, `vkDeviceWaitIdle`, destruição e fim do probe. Os logs também registram os retornos de início/fim de command buffer, criação de fence e submissão.

## 11. Controles e arquivos usados no console

| Arquivo/valor | Efeito |
|---|---|
| `/data/azimuth-ps5/title-mode`: ausente ou `smoke` | Smoke do runtime |
| `title-mode`: `host` | Executar o motor pelo host |
| `title-mode`: `gpu` | Executar o GPU probe |
| `/data/azimuth-ps5/gpu-stop`: `carregado` | Só mapear RADV, sem executar entrada |
| `gpu-stop`: `modulo` | Carregar/iniciar módulo e negociar ICD, sem criar instância |
| `gpu-stop`: `instancia` | Instância/enumeração, sem criar dispositivo lógico |
| `gpu-stop` ausente | Teste completo: filas, preenchimento e comparação |
| `gpu-stop`: `falha` ou `falha-syscall` | Falhas deliberadas para testar diagnóstico, incluindo SIGSYS |
| `gpu-stop`: `sem-calibracao` ou `pilha-alternativa` | Controles especiais de investigação das calibrações |

Há também o controle histórico `dependencias`. `host-stop` e `memory-test` pertencem aos testes do host/plataforma, não substituem `title-mode`.

Destino instalado do eboot: `/data/homebrew/FAKE78002/eboot.bin`.
Relatórios: `/data/azimuth-ps5/title.txt`, `gpu-probe.txt`, `gpu-stderr.txt` e, no modo host, `host.txt`. Os nomes com sufixos `(1)`, `(2)` etc. eram cópias em Downloads; as evidências preservadas estão organizadas por execução em `console-reports/`.

Entre os builds recentes, foi solicitado substituir apenas o eboot. O `libc.prx` acompanhante permaneceu igual. O pacote completo inclui metadados, ícone, manifesto e licenças.

## 12. Build, verificações e artefatos atuais

Ambiente registrado: Windows/PowerShell com WSL Ubuntu, clang/lld 21.1.8 e ps5-payload-sdk v0.45 em `/opt/ps5-payload-sdk`.

Comandos do título, dentro do WSL:

```bash
cd /mnt/c/Projects/Azimuth_PS5/title
make
make verify
make test-tls
make test-gpu
```

Esses comandos estão documentados para reprodução; não foram todos reexecutados ao escrever este documento.

Validações já realizadas:

- Smoke 6/6 no PS5, título 0.2.
- Regressão Linux do TLS: defeito reproduzido e 64/64 destrutores após a correção.
- Testes de janelas/GDI: 37 verificações no Linux.
- GPU probe no Linux, nas variantes comum e `AZIMUTH_TITLE`: envio vazio, command buffer vazio, comparação correta das 1024 palavras e encerramento normal do watchdog.
- Diagnósticos de falha nula, falha no modo título e SIGSYS, além da parada planejada de mapeamento, no Linux.
- No build 0.34, `make`, `make verify`, `make test-gpu` e `make test-tls` passaram; no Linux, a verificação D3D12, `make test-gdi`, `make test-vulkan` e o motor real continuam como antes.
- No build 0.33, `make`, `make verify`, `make test-gpu` e `make test-tls` passaram; no Linux, a verificação D3D12, `make test-gdi`, `make test-vulkan` e o motor real continuam como antes.
- No build 0.32, `make`, `make verify`, `make test-gpu` e `make test-tls` passaram; no Linux, a verificação D3D12, `make test-gdi`, `make test-vulkan` e o motor real continuam como antes.
- No build 0.31, `make`, `make verify`, `make test-gpu` e `make test-tls` passaram; no Linux, a verificação D3D12, `make test-gdi`, `make test-vulkan` e o motor real continuam como antes.
- No build 0.30, `make`, `make verify`, `make test-gpu` e `make test-tls` passaram, e `make test-gdi` continua passando no Linux. O VideoOut não é exercitável no Linux.
- No build 0.29, `make`, `make verify`, `make test-gpu` e `make test-tls` passaram; a verificação D3D12, `make test-gdi` e `make test-vulkan` continuam passando no Linux.
- No build 0.28, `make`, `make verify`, `make test-gpu` e `make test-tls` passaram; no Linux, `make test-gdi` e a execução do motor continuam como antes.
- No build 0.27, `make`, `make verify`, `make test-gpu` e `make test-tls` passaram, além da verificação D3D12, do motor real, de `make test-gdi` e de `make test-vulkan` no Linux.
- No build 0.26, `make`, `make verify`, `make test-gpu` e `make test-tls` passaram, além de `make test-gdi` e `make test-vulkan`; o motor real rodou no Linux (4 KiB e 16 KiB simulado) até a checagem da pasta do jogo.
- No build 0.25, `make`, `make verify`, `make test-gpu` e `make test-tls` passaram; no Linux a mensagem de não encontrado lista o errno de cada candidato, e `make test-gdi` passa.
- No build 0.24, `make`, `make verify`, `make test-gpu` e `make test-tls` passaram; a verificação D3D12 do host passou no Linux pelo adaptador do DXGI (4 KiB e 16 KiB simulado), e `make test-vulkan` e `make test-gdi` continuam passando. O motor não foi executado no Linux: o `azimuth.exe` não está nesta máquina.
- No build 0.23, `make`, `make verify`, `make test-gpu` e `make test-tls` passaram; a verificação D3D12 do host passou no Linux (4 KiB e 16 KiB simulado), e `make test-vulkan` e `make test-gdi` continuam passando.
- No build 0.22, `make`, `make verify`, `make test-gpu` e `make test-tls` passaram; a verificação D3D12 do host passou no Linux (4 KiB e 16 KiB simulado), e `make test-vulkan` e `make test-gdi` continuam passando.
- No build 0.21, `make`, `make verify`, `make test-gpu` e `make test-tls` passaram; a verificação D3D12 do host passou no Linux com blocos de 4 KiB e de 16 KiB simulados, e `make test-vulkan` e `make test-gdi` continuam passando.
- No build 0.20, `make`, `make verify`, `make test-gpu` e `make test-tls` passaram.
- No build 0.19, `make`, `make verify`, `make test-gpu` e `make test-tls` passaram; no host, a verificação D3D12 (`--d3d12`) passou no Linux sobre o lavapipe, e `make test-vulkan` e `make test-gdi` continuam passando.
- No build 0.18, `make`, `make verify`, `make test-gpu` e `make test-tls` passaram, além de `make test-vulkan` (61 verificações pelos adaptadores Windows x64 sobre o lavapipe) e `make test-gdi` no host.
- No build 0.17, `make`, `make test-gpu`, `make verify` e `make test-tls` passaram.
- No build 0.16, `make`, `make test-gpu`, `make verify` e `make test-tls` passaram. O registro de sinais na saída não foi exercitado no Linux.
- No build 0.15, `make`, `make test-gpu`, `make verify` e `make test-tls` passaram.
- No build 0.14, `make`, `make test-gpu`, `make verify` e `make test-tls` passaram. O caminho novo só existe no build do PS5 (no Linux o RADV é carregado com `dlopen`).
- No build 0.13, `make`, `make test-gpu`, `make verify` e `make test-tls` passaram.
- No build 0.10, `make test-gpu` e `make verify` passaram. As verificações do ELF/SELF e a inspeção de integridade foram concluídas.
- No build 0.12, `make`, `make test-gpu` (incluindo o `setenv`/`getenv` de `RADV_DEBUG` e a cópia da janela, vazia no Linux), `make verify` e `make test-tls` passaram. `setenv`, `pwrite` e `pread` são imports do sistema.
- No build 0.11, `make`, `make test-gpu` (as três escritas pela GPU com 16, 64 e 1024 palavras corretas, nas variantes comum e de título), `make verify` (integridade do SELF válida) e `make test-tls` (64/64) passaram.
- Nenhum desses resultados Linux prova execução do preenchimento no PS5.

Artefatos do último build:

| Caminho | Conteúdo |
|---|---|
| `title/dist/FAKE78002/eboot.bin` | Executável do título 0.34, GPU probe 0.22 |
| `title/dist/Azimuth-PS5-title-0.34.zip` | Pacote completo |
| `title/dist/FAKE78002/build-info.json` | Hashes, versão e `consoleValidated: false` |
| `title/build/llvm-pie.elf`, `eboot.elf` | Intermediários de link/conversão |
| `title/build/build.log`, `verify.log`, `self-inspection.txt` | Build e validações |
| `title/build/gpu-test.log`, `gpu-test/`, `tls-test/` | Resultados e arquivos de testes locais |

SHA-256 do eboot 0.34:

```text
28513a3b7133479a6542c3390461f06d4e14b8b7bb2d2063e97be0951ed3f900
```

SHA-256 dos eboots anteriores: 0.33 `33d49f69f46558fb17c7d9e8ae152225ec0941adff8fa18868d29b93a4c70050`; 0.32 `181c5ce3d37257eceb8d9e2c6ef0bde47d225c49cb3fba714fd684db1b97318a`; 0.31 `1a1443f76cd541a3d4d44460eabb9a440a11501d5ba8f26b6adf7b0d02e2ce8e`; 0.30 `26f864e55be9e7a3571ed371b3e46748d1f3bbb175ad689926c09f5b78fe0c39`; 0.29 `5e22d7b7f0f6bb94153cfa15c75dce84cbfeec98b3b7da168ba9c90c0cb69adb`; 0.28 `64b6551cb2b78bfdf22a0db61d8745c73cca9cb729d89bd6ed54405f4ae5626b`; 0.27 `bb1715476df3ba0cff22194d938c8cf24f2db2a554104f78c828665dcd034c09`; 0.26 `c76cac7729d8545cf9dc5b2307bf73a0e94296736217b4ba18911e1f5761c492`; 0.25 `ef169fcb752c3af59573bd1634aff1424a351e4c405d9b34a97f71f26740b81e`; 0.24 `0dd97aac5991ea36f6fc7aaa0752962947a7eef85f454f607d4bc696ec23563c`; 0.23 `91bca968741a468d81f167723b524a7c0eff8e7d1d7e640792a3fe65f7596103`; 0.22 `02203e4bcc9d9f4ed342576a315a19681d72d63f42789cd46f1de920521d79e4`; 0.21 `e9f73459fdf97b28db5af5a7a6a2cc3f739b16cb35dbff09b34e40b66c24ca47`; 0.20 `99043f080c051d0f70bd292dbd8a6972577673ab3ba71e783f96e738853c8ea4`; 0.19 `ca0c695c79b755fe0cc4bdfd8352a99b2ca928b7db96abda72fc543263b12a3f`; 0.18 `6eac5f04f50079f2b70ea13c25e4caf0929ee01a0eb0c8cb77fab7db17b57d1c`; 0.17 `07cf9b659174a37ec333f848a163b8e8cf5025d024dc941ef900f55fd36f6b83`; 0.16 `bdf0977c7a8f771d59f1a5937e3881a96c1ba97cff247a2d242ea2f72ac8c59a`; 0.15 `ccdd8e56cbd859aacfa2c2e29d5d5090e6b25768d58dab38858dddf986301cab`; 0.14 `516d3bf9f2d361ba07670dbc280d9c7f3c2ed8d5673698d86ac34aee4e01ef37`; 0.13 `6ef6fa48f8963ee1f56f67cd388b9186c1b02abc30f605eb48603aa35b2251cc`; 0.12 `0105d1e8b7c569cd645b5dcc4fda6b5727d19c3f8ec7e64468f83e77a2fbffbc`; 0.11 `288183e0b04b3e865856bb913bd64a09d441d09b8d2c4a88908979239541a2b7`; 0.10 `13ecc8c9f79c185ca6f3331292e47675172813acfeb0f113030e12c9ec0bf8d2`.

SHA-256 do runtime acompanhante `sce_module/libc.prx`:

```text
e6ff45d16adf687855cc3b33b0c8a4132b6504360b221e0a34c7e99fb3ba0036
```

## 13. Dependências e referências utilizadas

- Base pública de título: `mpereiraesaa/ps5-native-app-boilerplate`, revisão fixada `9c0b994a048521af6fb84c73ded364504fe250e9`. Startup, conversor ELF/SCE, assinador, base do linker e runtime acompanhante; licença GPL-3.0-or-later.
- SDK: ps5-payload-sdk v0.45. `runtime/emutls.c` deriva da revisão `a7685ce8e97ce1f8a4f38253a41d54241c9124f1`, sob Apache-2.0 WITH LLVM-exception, com a mudança de rodada de destruição documentada.
- Código do AzimuthUwp orientou loader, INI, janelas e GDI. A fronteira ABI, plataforma e integrações específicas do PS5 ficam neste projeto.
- O RADV/`libvulkan.prx` dos testes é externo, da instalação do Playden; não foi reconstruído ou corrigido por este projeto.
- `title/THIRD_PARTY.md` e `title/licenses/` registram os componentes e licenças usados no pacote. O runtime acompanhante tem superfície de compatibilidade; sua presença não significa implementação completa de todas as APIs exportadas.

Foi consultada uma alternativa de RADV **estático**, com `libvulkan_radeon.ps5.a` e `libps5platform.a`, no projeto local `C:/Projects/PS5X360/.deps/references/PS5_Vulkan`. Ela exige integração própria de memória, threads, libc e link. **Não foi implementada nem testada no Azimuth_PS5**, e esses projetos externos não foram modificados durante a investigação.

As revisões/proveniência dessa alternativa estão em `docs/radv-runtime-options.md`. Não se deve tratar seus resultados de hardware como resultados deste port.

## 14. Evidências preservadas e pendências

As pastas em `console-reports/` cobrem o probe inicial, hosts 0.2–0.7.1, GPU probes em payload e os títulos. Para a sequência recente de GPU:

- `2026-10-27-title-0.4-gpu-entry/`: diagnóstico da entrada manual.
- `2026-10-27-title-0.5-gpu-module/`: carregamento normal/ICD com saída limpa.
- `2026-10-27-title-0.5-gpu-instance/`: instância/enumeração com saída limpa.
- `2026-10-27-title-0.5-gpu-submit/`: primeira tentativa completa no título.
- `2026-10-28-title-0.6-gpu-submit/` até `2026-10-28-title-0.9-gpu-submit/`: refinamentos de envio, espera, pausas, command buffer vazio e barreira.
- `2026-10-28-title-0.10-gpu-submit/`: memória tipo 3, mesma falha do 0.9.
- `2026-10-28-title-0.11-gpu-submit/`: CP WRITE_DATA e CP DMA concluíram; compute shader falhou.
- `2026-10-28-title-0.11-gpu-klog/`: kernel log da repetição do 0.11, com a falha `GPU_FAULT_WAVEFRONT_ERROR_ASYNC` (`ILLEGAL_INST` em `0x20004001C`).

A evidência de entrada do título 0.3 foi preservada apenas como trecho; não há como reconstruir um relatório completo a partir disso. Datas nos nomes das pastas indicam a organização das cópias preservadas, não uma medição independente do horário de execução no console.

Pendências técnicas:

1. **Resolvido no PS5:** a GPU executa compute shaders pelo RADV (0.14), e o título fecha sem erro (0.17). Para isso, o RADV precisa ter o `INIT_ARRAY` executado antes de qualquer compilação, o cache de shaders gerado antes dessa correção precisa ser apagado e o título não pode retornar do `main`.
2. Comparar o ambiente do Playden com o do título: imports e inicialização do eboot, variáveis de ambiente, `param.json` e ocupação da janela `0x2_xxxxxxxx`.
3. Se necessário, investigar o driver externo e seus requisitos de execução ou avaliar a integração estática; ambas continuam trabalho futuro.
4. Só depois de validar comandos reais, implementar fábrica/adaptador DXGI e integrar vkd3d-proton com a ABI do host.
5. Completar dependências do jogo, orçamento de memória de título, apresentação VideoOut, entrada e áudio. Esses itens não estão concluídos.

Não há causa raiz comprovada para os fechamentos recentes. O estado documentado é um host funcional até DXGI e um título com Vulkan parcialmente validado, com execução de preenchimento ainda bloqueada.


## Título 0.35 — entrada pelo controle

- Novo `host/src/Input.{h,cpp}`: thread que lê o controle (ScePad) e converte botões em teclas (Cruz/Options = Enter, Círculo = Esc, direcional e analógico = setas).
- `win32::postKey` em `User32.cpp` posta WM_KEYDOWN/WM_KEYUP à janela ativa e alimenta `GetKeyState`/`GetAsyncKeyState`.
- `title/build.sh` linka `libScePad` e `libSceUserService`; `main.cpp` inicia a leitura no modo do motor. Contagem de versão: 0.35 (contentVersion 01.000.034).
- Não testado no console. Pendentes: XInput para o jogo, swapchain DXGI e apresentação do D3D12 no VideoOut.

## Título 0.36 — diagnóstico do controle

- O 0.35 conectou o controle, mas o launcher não reagiu e o log ficou mudo. O 0.36 registra mudanças de botão, teclas postadas (com a janela alvo) e teclas entregues ao motor por `DispatchMessageW` (até 60/40/20 linhas). contentVersion 01.000.035. Evidência do 0.35 em `console-reports/2026-10-28-title-0.35-host`.

## Título 0.37 — estado das threads

- Teste do 0.36 (`console-reports/2026-10-28-title-0.36-host`): pad e teclas postadas funcionam; nenhuma tecla chegou ao `DispatchMessageW` do motor, o que indica a thread da janela bloqueada. O 0.37 registra, aos 10/20/30 s, as últimas chamadas de cada thread. contentVersion 01.000.036.

## Título 0.38 — despejo antecipado

- O 0.37 não produziu nenhum despejo (log parado na partida). O 0.38 despeja aos 3/6/12 s e marca o início de cada coleta. contentVersion 01.000.037.

## Título 0.39 — PNG em thread de fundo

- Causa dos despejos ausentes no 0.37/0.38: o laço principal ficava preso em `savePng` (`frame.png.tmp` parcial no console). A gravação do PNG agora roda numa thread própria, a cada 30 s no máximo. contentVersion 01.000.038.

## Título 0.40 — thread vigia

- O 0.39 manteve o despejo ausente. O despejo passou a uma thread vigia independente do laço principal, que informa a etapa do laço (`loopStage`) e as últimas chamadas de cada thread. contentVersion 01.000.039.

## Título 0.41 — amostragem das threads

- Pedido realinhado: o launcher deve ficar vivo na TV (sem `--game=`; a tentativa de passar `--game=` foi desfeita antes de compilar).
- Teste do 0.40 (`console-reports/2026-10-28-title-0.40-host`): a thread 256 do launcher parou de chamar o host depois de `CreateFontW`; laço do host na etapa 4.
- `sampleGuestThreads()` (GuestThreads.cpp): registro dos pthreads do motor e amostras do `rip` por SIGUSR2, com os endereços do motor na pilha. contentVersion 01.000.040.

## Título 0.42 — agendamento justo

- Teste do 0.41 (`console-reports/2026-10-28-title-0.41-host`): a thread 284 criada pela 264 nunca rodou; launcher, laço do host e gravação do PNG pararam juntos. No Linux, com `data/bin` copiado do pendrive, o mesmo motor segue normal e as amostras por SIGUSR2 funcionam (no console vieram vazias).
- `platform::shareCpu()`: SCHED_RR na prioridade atual e afinidade com o conjunto de CPUs do processo, aplicado a toda thread do motor (`runAsGuest`), ao laço do host e à vigia, com log dos valores originais. contentVersion 01.000.041.

## Título 0.43 — onde o launcher trava

- Teste do 0.42 (`console-reports/2026-10-28-title-0.42-host`): com SCHED_RR o launcher respondeu ao controle por alguns segundos (teclas entregues ao `DispatchMessageW`); travou quando a 264 criou a thread 284 (`CreateThread` sem retorno).
- Entradas do host com `__attribute__((cleanup(hostLeave)))` (guest_abi.c): o despejo mostra a função do host em que a thread está ou "no codigo do motor". Dono da trava do GDI (`win32::gdiOwner`), contagem das threads registradas e tempo do `pthread_create`. Conferido no Linux com o motor real. contentVersion 01.000.042.

## Título 0.44 — memória flexível

- Teste do 0.43 (`console-reports/2026-10-28-title-0.43-host`): título encerrado ~1 s após a partida, dentro do `pthread_create` da thread 280, sem falha capturada pelo host. Hipótese: falta de memória flexível.
- Vigia registra `sceKernelAvailableFlexibleMemorySize`/`ConfiguredFlexibleMemorySize` e os bytes mapeados pelo HostHeap (`heapMappedBytes`). FrameSaver reutiliza o buffer do quadro; `frame.png` gravado só no início e no fim. contentVersion 01.000.043.

## Título 0.45 — quem consome a memória

- Teste do 0.44 (`console-reports/2026-10-28-title-0.44-host`): 448 MiB de memória flexível; 294 MiB livres 0,5 s após a partida; encerramento ~0,3 s depois, após criar a thread 284.
- Medição a cada 100 ms; log de reservas `VirtualAlloc` ≥ 64 MiB (com a tabela de páginas do host), commits ≥ 8 MiB e blocos ≥ 8 MiB do HostHeap. No Linux, com o motor no launcher, o processo fica em ~180 MiB residentes. contentVersion 01.000.044.

## Título 0.46 — blocos grandes na memória direta

- Teste do 0.45 (`console-reports/2026-10-28-title-0.45-host`): 448 MiB de memória flexível; blocos grandes do HostHeap em 215 MiB e 200 MiB livres em menos de 1 s, depois encerramento pelo sistema. Causa dos fechamentos e travamentos: esgotamento da memória flexível.
- HostHeap: blocos ≥ 1 MiB vêm da memória direta (`MagicDirect`, granularidade de 64 KiB, endereço físico guardado no início do mapeamento, zerados na alocação), com `mmap` como reserva. Linux inalterado. contentVersion 01.000.045.

## Título 0.47 — consumo de memória direta

- Teste do 0.46 (`console-reports/2026-10-28-title-0.46-host`): sem fechamento; memória flexível estável. Blocos grandes de 150 para 872 MiB em 300 ms após a thread 280 iniciar; travamento no `pthread_create` da thread 284.
- Log do tamanho da memória direta, do maior trecho livre (`sceKernelAvailableDirectMemorySize`), de cada bloco ≥ 1 MiB com a thread que pediu (até 300) e das recusas. contentVersion 01.000.046.

## Título 0.48 — o host fora do malloc do título

- Teste do 0.47 (`console-reports/2026-10-28-title-0.47-host`): 12 GB de memória direta, quase toda livre; blocos de 64–192 KiB recusados pelo `malloc` do título e servidos pela memória direta. Diagnóstico: heap do `malloc` do título esgotado pelas alocações pequenas do motor; `pthread_create` (que usa esse heap) deixava de voltar.
- HostHeap: blocos < 1 MiB em arenas próprias de 64 MiB (memória direta no console, mmap no Linux) com 68 classes de tamanho e listas livres (`MagicArena`); o `malloc` do título não é mais usado pelo host. Conferido no Linux com o motor real. contentVersion 01.000.047.

## Título 0.49 — amostrador limitado à pilha

- Teste do 0.48 (`console-reports/2026-10-28-title-0.48-host`): sem travamento; memória estável; SIGSEGV em `onSample` (cópia de 512 palavras além do topo da pilha da thread 260) no despejo dos 3 s.
- O registro de cada thread guarda o endereço do quadro de `runAsGuest`; o tratador só lê a pilha até ele. contentVersion 01.000.048.

## Título 0.50 — arenas sem disputa

- Teste do 0.49 (`console-reports/2026-10-28-title-0.49-host`): sem erro; launcher compila os shaders com congelamentos. 15 threads de compilação em `malloc`/`free` paradas na trava única das arenas; o launcher (256) também, segurando a trava do GDI.
- HostHeap: 16 arenas, cada uma com a própria trava, escolhidas por hash do `pthread_self()`; o cabeçalho guarda classe e arena. Conferido no Linux. contentVersion 01.000.049.

## Título 0.51 — prioridade para quem desenha

- Teste do 0.50 (`console-reports/2026-10-28-title-0.50-host`): congelamentos persistem; threads de compilação e do launcher amostradas em `0x80000038a` (chamada ao kernel não identificada).
- `platform::shareCpu(raise)`: prioridade base fixa (a do título, 700) para toda thread; thread principal do motor, laço do host, vigia e controle em 600 (RR). contentVersion 01.000.050.

## Título 0.52 — fontes sem acesso ao disco a cada quadro

- Teste do 0.51 (`console-reports/2026-10-28-title-0.51-host`): prioridades aplicadas (600 para launcher, laço, vigia e controle; 700 para o resto), congelamento igual. Thread 256 amostrada numa chamada ao kernel dentro de `DrawTextW` (retorno `engine+0x25ddd`, IAT `0x1402236d0`).
- `loadFontFile` (Gdi.cpp): cache por nome de arquivo, inclusive ausências; antes, cada fonte criada pelo launcher a cada quadro fazia `fopen` em `/data` antes do acerto no cache. Amostrador registra 16 bytes antes do `rip` fora do motor. contentVersion 01.000.051.

## Título 0.53 — sem leitura do código do sistema

- Teste do 0.52 (`console-reports/2026-10-28-title-0.52-host`): encerramento no despejo dos 3 s, na leitura dos bytes antes de um `rip` da `libkernel` (páginas só de execução, presumivelmente). A leitura foi removida. contentVersion 01.000.052.

## Título 0.54 — cache de pipelines do vkd3d-proton

- Teste do 0.53 (`console-reports/2026-10-29-title-0.53-host`): sem erro; congelamento só no início da compilação; pipelines recompilados a cada abertura porque o vkd3d-proton não conseguia mapear `vkd3d-proton.cache` (`MapViewOfFile` ausente).
- `CreateFileMappingA/W`, `MapViewOfFile`, `UnmapViewOfFile` (Files.cpp), só leitura, por cópia (`pread`) num bloco do HostHeap; escrita e nomes recusados. Conferido no Linux com os arquivos de cache do console. contentVersion 01.000.053.

## Título 0.55 — mapeamento sem dup

- Teste do 0.54 (`console-reports/2026-10-29-title-0.54-host`): `Failed to MapViewOfFile` persistiu no console, sem log do host; vkd3d-proton testa `CreateFileMappingA` contra `INVALID_HANDLE_VALUE`, então a falha estava no `CreateFileMapping` (suspeita: `dup`).
- `Mapping` guarda `std::shared_ptr<File>` em vez de um descritor duplicado; logs em todos os caminhos de falha. contentVersion 01.000.054.

- Confirmado no console (`console-reports/2026-10-29-title-0.55-host`): merge de 2362 entradas do `.write` no `vkd3d-proton.cache`, cache só de leitura mapeado em 0,2 ms; launcher pronto sem recompilar.

## Título 0.56 — azimuth-tls.dll

- Teste do 0.55 com Start (`console-reports/2026-10-29-title-0.55-start`): `LoadLibraryExW(E:\PC\FH2\data\bin\azimuth-tls.dll)` recusado pelo host; o motor parou com "Cannot load the native static-TLS helper".
- Modules.cpp: `azimuth-tls` na lista de DLLs carregáveis; `data/bin` do motor (montagem de `E:\PC\FH2`) nas pastas de busca. contentVersion 01.000.055.

## Título 0.57 — msvcr110 e msvcp110

- Teste do 0.56 com Start (`console-reports/2026-10-29-title-0.56-start`): `azimuth-tls.dll` carregado; parada em "Cannot load desktop dependency msvcr110.dll: 0x7e".
- `msvcr110` e `msvcp110` (de `data\bin`) na lista de DLLs carregáveis; 89 imports de KERNEL32 do `msvcr110` sem implementação viram paradas de diagnóstico. contentVersion 01.000.056.

## Título 0.58 — funções da msvcr110 e da msvcp110

- Teste do 0.57 com Start (`console-reports/2026-10-29-title-0.57-start`): `msvcr110.dll` mapeado (106/194 imports); parada em `GetThreadTimes` (`msvcr110+0x90f87`, thread 256).
- 53 funções novas de KERNEL32 (threads, `CREATE_SUSPENDED` + `ResumeThread`, SList, tempo, versão, heap, locale *Ex, arquivos e diretórios). Restam 38 imports da `msvcr110` e 0 de KERNEL32 da `msvcp110` sem implementação. Launcher conferido no Linux. contentVersion 01.000.057.

## Título 0.59 — RtlPcToFileHeader para DLLs

- Teste do 0.58 com Start (`console-reports/2026-10-29-title-0.58-start`): SIGSEGV em `msvcr110+0x214cb` (endereço `0xae538`) ao lançar exceção C++, porque `RtlPcToFileHeader` devolvia base nula fora do motor.
- `RtlPcToFileHeader` usa `moduleContaining` para os DLLs carregados. Novos: `CreateMutexA/W`, `ReleaseMutex`, `CreateEventExW`, `ResetEvent`, `WaitForMultipleObjectsEx`, `CompareFileTime`. contentVersion 01.000.058.

## Título 0.60 — caminho real dos DLLs do motor

- Teste do 0.59 com Start (`console-reports/2026-10-29-title-0.59-start`): exceção "Cannot open file to hash: C:\windows\system32\msvcr110.dll"; o motor calcula o hash das dependências pelo nome dado por `GetModuleFileNameW`.
- `moduleWindowsPath` traduz o caminho do arquivo pelas montagens (`E:\PC\FH2\data\bin\...`); `LocaleNameToLCID`. contentVersion 01.000.059.

## Título 0.61 — dependências de desktop

- Teste do 0.60 com Start (`console-reports/2026-10-29-title-0.60-start`): `msvcr110`/`msvcp110` inicializados; parada em "Cannot load desktop dependency ws2_32.dll".
- `win32/Desktop.cpp`: `ws2_32` (eventos WSA), `mfplat` e `mfreadwrite` (Media Foundation sem decodificadores), `vccorlib110` (`GetActivationFactoryByPCWSTR`, nome decorado registrado à mão em `guest_abi.c`, devolve REGDB_E_CLASSNOTREG). contentVersion 01.000.060.

## Título 0.62 — reservas em blocos de 16 KiB

- Teste do 0.61 com Start (`console-reports/2026-10-29-title-0.61-start`): SIGSEGV (SEGV_MAPERR) no `memcpy` do motor (`engine+0x204ef3`, destino `0x20c79c020`) após `VirtualAlloc`; funções do `ws2_32` procuradas e ausentes.
- Memory.cpp: o mapeamento de cada reserva é arredondado à granularidade do kernel (16 KiB no console), evitando que o corte do excesso desmapeie o fim da própria reserva; log de reservas, commits e liberações ≥ 1 MiB ou com endereço pedido. `ws2_32`: `WSAStartup`, `WSACleanup`, `WSAGet/SetLastError`, `htonl/htons/ntohl/ntohs`, `inet_addr`, `inet_ntoa`. contentVersion 01.000.061.

## Título 0.63 — dependências do xg_x.dll

- Teste do 0.62 com Start (`console-reports/2026-10-29-title-0.62-start`): reserva de 59 MiB em `0x21c000000` funcionou; parada em "Missing XG dependency msvcrt.dll!vsprintf_s" ao ligar o `xg_x.dll` do jogo.
- `msvcrt`: `vsprintf_s`, `sqrt`, `__C_specific_handler`, `_XcptFilter`. Biblioteca `ntdll` do host (entradas manuais em `guest_abi.c`). `RtlAddFunctionTable`/`RtlDeleteFunctionTable` guardam as tabelas; `RtlLookupFunctionEntry` real (tabelas registradas e `.pdata` do motor e dos DLLs); `RtlVirtualUnwind` como parada explícita. contentVersion 01.000.062.

## Título 0.64 — VirtualProtect por bloco de 16 KiB

- Teste do 0.63 com Start (`console-reports/2026-10-29-title-0.63-start`): imports do `xg_x.dll` resolvidos; SIGSEGV de busca de instrução (`err 0x15`) em `0x20c78df84`, no código do `xg_x.dll` mapeado pelo motor.
- Memory.cpp `commit`: cada bloco da granularidade do kernel recebe a união das proteções das páginas (como `Image` faz para DLLs); bloco gravável e executável recusado vira executável, com log. contentVersion 01.000.063.

## Título 0.65 — placeholders e seções (VirtualAlloc2, MapViewOfFile3)

- Teste do 0.64 com Start (`console-reports/2026-10-29-title-0.64-start`): `xg_x.dll` executou; o motor parou com "desktop memory API unavailable: VirtualAlloc2" (procurado em `KernelBase.dll`).
- Memory.cpp reescrito com placeholders (`VirtualAlloc2`, `VirtualFree` preserve/coalesce), seções de memória compartilhada (`CreateFileMapping` sem arquivo; memória direta no console, memfd no Linux), `MapViewOfFile3`, `UnmapViewOfFile2`; `KernelBase` → `kernel32`. Teste `make test-memory` no Linux. contentVersion 01.000.064.

## Título 0.66 — reservas grandes só de endereços

- Teste do 0.65 com Start (`console-reports/2026-10-29-title-0.65-start`): "contiguous graphics heap reservation error=0x8"; o motor pede um placeholder de 20 GiB em `0x800000000` (`engine+0x7ba30`).
- Memory.cpp (console): reservas ≥ 1 GiB via `sceKernelReserveVirtualRange`; commits mapeiam memória direta em blocos de 16 KiB (zerada), decommit/liberação devolvem; o modo passa às divisões, junções e vistas de seção. Log das reservas que falham. contentVersion 01.000.065.

## Título 0.67 — reservas fora da área do título

- Teste do 0.66 com Start (`console-reports/2026-10-29-title-0.66-start`): o console desligou logo após `sceKernelReserveVirtualRange` fixo recusar (EINVAL) 12 GiB em `0x60000000000` e 4 GiB em `0x60300000000`; o motor parou com "contiguous graphics heap reservation error=0x8".
- Memory.cpp: reservas fixas fora de `0x1000000000`-`0xFC00000000` falham no host sem chamar o kernel; exigências de endereço (`MEM_ADDRESS_REQUIREMENTS`) com menor/maior endereço respeitados; log das falhas com a faixa e os endereços de retorno do motor. contentVersion 01.000.066.

## Título 0.68 — alinhamento grande feito pelo host

- Teste do 0.67 com Start (`console-reports/2026-10-29-title-0.67-start`): sem queda do console. Reservas de 6 TiB vêm do motor (`engine+7e597`) e são toleradas; o heap gráfico contíguo (`engine+7ba30`) falhou na alternativa de 20 GiB alinhada em 4 GiB (EINVAL do kernel).
- `reserveAddresses`: reserva tamanho + alinhamento com alinhamento de bloco e corta as sobras. contentVersion 01.000.067.

## Título 0.69 — log do heap gráfico

- Teste do 0.68 com Start (`console-reports/2026-10-29-title-0.68-start`): placeholder de 20 GiB reservado em `0x1000000000` e dividido em pedaços de 4 GiB; o título sumiu em seguida sem falha registrada.
- Log de placeholders ocupados (com a pilha do motor), de commits com memória direta (início ≥ 1 MiB com memória livre, fim ≥ 64 MiB) e de `MapViewOfFile3`. contentVersion 01.000.068.

## Título 0.70 — medição de memória contínua

- Teste do 0.69 com Start (`console-reports/2026-10-29-title-0.69-start`): mesma morte silenciosa logo após dividir o placeholder de 20 GiB; nenhum placeholder ocupado, nenhuma seção, nenhum commit com memória direta.
- A vigia mede a memória flexível durante toda a execução; log dos `VirtualFree` de divisão/junção de placeholders. contentVersion 01.000.069.

## Título 0.71 — falhas de memória registradas

- Teste do 0.70 com Start (`console-reports/2026-10-29-title-0.70-start`): memória flexível em 360 MiB livres; quatro divisões de 4 GiB do placeholder em `0x1000000000`; morte silenciosa logo depois.
- Memory.cpp: invólucros de `VirtualAlloc2`, `VirtualFree`, `MapViewOfFile3` e `VirtualProtect` registram toda falha (parâmetros, erro, pilha do motor) e toda chamada em região só de endereços. contentVersion 01.000.070.

## Título 0.72 — sinais e pilha alternativa

- Teste do 0.71 com Start (`console-reports/2026-10-29-title-0.71-start`): nenhuma falha nas chamadas de memória; morte silenciosa logo após a quarta divisão do heap gráfico, sem outra chamada ao host.
- GuestThreads.cpp: o tratador cobre também SIGTRAP (int3), SIGABRT e SIGSYS e roda numa pilha alternativa de 256 KiB por thread do motor (`sigaltstack`, `SA_ONSTACK`), cobrindo estouro de pilha. contentVersion 01.000.071.

## Título 0.73 — lista livre protegida e tratador sem _exit

- Teste do 0.72 com Start (`console-reports/2026-10-29-title-0.72-start`): "FPU not available". SIGSEGV em `heapAlloc` (thread vigia) ao ler `0x33c3095a6f80` da lista livre corrompida; o tratador chamou `_exit`, que gera SIGSYS no título, em laço.
- HostHeap: blocos livres com `MagicFree`, próximo e verificação ligada ao endereço; lista descartada e log quando um bloco é escrito após o free; free duplo detectado e registrado. GuestThreads: falha em thread do host estaciona a thread em vez de `_exit`. contentVersion 01.000.072.

## Título 0.74 — despejos contínuos e seguros

- Teste do 0.73 com Start (`console-reports/2026-10-29-title-0.73-start`): sem erro na tela, nenhuma corrupção do alocador detectada; motor em silêncio após dividir o heap gráfico; a vigia caiu (SIGSEGV na libkernel) ao ler um nome inválido do registro de chamadas e ficou parada.
- `describeRing` valida os ponteiros de nome; amostrador por sinais removido dos despejos; despejos a cada 10 s durante toda a execução. contentVersion 01.000.073.

## Título 0.75 — free de ponteiro estranho não vai mais ao free do sistema

- Teste do 0.74 com Start (`console-reports/2026-10-29-title-0.74-start`): blocos escritos depois do free em várias classes; registros de chamadas de metade das threads de trabalho corrompidos (dono duplo de bloco); thread 256 viva em laço de travas SRW.
- HostHeap: ponteiro sem marca do alocador em `heapFree` é registrado e ignorado, em vez de ir ao `free` do título. contentVersion 01.000.074.

## Título 0.76 — quarentena e liberação conferida

- Teste do 0.75 com Start (`console-reports/2026-10-29-title-0.75-start`): nenhum free de ponteiro estranho; blocos escritos depois do free em quase todas as classes, logo após as divisões do heap gráfico.
- HostHeap: quarentena de 4096 blocos por arena antes do reuso; dump de 48 bytes do bloco corrompido; `sceKernelReleaseDirectMemory` só depois de `sceKernelMunmap` bem-sucedido, com log das falhas. Captura do klog (porta 3232) passa a acompanhar os testes. contentVersion 01.000.075.

## Título 0.77 — tratador de falhas limitado à pilha

- Teste do 0.76 com Start (`console-reports/2026-10-29-title-0.76-start-b`, com klog): bloco livre de 64 bytes sobrescrito por registros de 10 bytes com contador crescente (thread 276); falha seguinte na thread 276 não registrada porque `onFault` leu além do topo da pilha.
- `State::stackTop` (quadro de `runAsGuest`) limita a varredura da pilha em `onFault`. contentVersion 01.000.076.

## Título 0.78 — guarda depois de blocos grandes

- Teste do 0.77 com Start (`console-reports/2026-10-29-title-0.77-start-b`, com klog): corrupção por varredura de registros de 10 bytes (contador de 16 bits crescente) atravessando as arenas, logo após as divisões do heap gráfico; cabeçalhos de blocos vivos do host atingidos.
- HostHeap: blocos grandes (memória direta e mmap) terminam numa guarda de 64 KiB sem acesso (`placeAtEnd`); estouro vira SIGSEGV no código que escreve. contentVersion 01.000.077.

## Título 0.79 — memória do host longe da varredura

- Teste do 0.78 com Start (`console-reports/2026-10-29-title-0.78-start`, com klog): nenhuma guarda atingida; a corrupção é uma varredura linear de registros de 10 bytes (índice de 16 bits) sobre `0x208000000`-`0x20c000000`, compatível com uma tabela de ~52 MB das páginas do heap gráfico de 20 GiB.
- HostHeap: arenas mapeadas com dica em `0x6000000000` e blocos grandes em `0x6800000000`; log do endereço de cada arena. contentVersion 01.000.078.

## Título 0.80 — guarda no fim de cada arena

- Teste do 0.79 com Start (`console-reports/2026-10-29-title-0.79-start`, com klog): a varredura de 10 bytes seguiu as arenas para `0x6004...`; logo, parte de um bloco pequeno entregue pelo host e vai dezenas de MB além. Falhas em cascata no host e fim da execução ("Fim do titulo: codigo=0").
- HostHeap: os últimos 64 KiB de cada arena viram guarda sem acesso, para a varredura falhar no código do motor que escreve. contentVersion 01.000.079.

## Título 0.81 — tratador de falhas sem recursão

- Teste do 0.80 com Start (`console-reports/2026-10-29-title-0.80-start-b`, klog): o kernel registrou SIGSEGV com `rsp` em `0x6003fffe20` e pilha de `onFault` recursivo; o tratador falhava ao alocar na arena corrompida antes de marcar `inFault`, até esgotar a pilha alternativa (alocada na própria arena).
- GuestThreads: `inFault` checado e marcado primeiro; registro imediato por `write`, sem alocação; pilha alternativa por `mmap`. HostHeap: falha na guarda da arena registrada. contentVersion 01.000.080.

## Título 0.82 — o autor da varredura é o código do jogo

- Teste do 0.81 com Start (`console-reports/2026-10-29-title-0.81-start`): SIGSEGV da thread 256 ao escrever na guarda da arena 2 (`0x6007ff0002`), instrução em `0x20645efda` = `forza_durango_release_final.exe` RVA `0x125efda`. A função `0x14125ef40` inicializa um pool (`HeapAlloc(GetProcessHeap(), 0, count*10)` da `kernelx` emulada pelo motor) e grava a lista livre; o bloco recebido era menor que `count*10`.
- Registro imediato da falha inclui os registradores e 16 palavras em `rdi`; `HeapAlloc` ≥ 1 MiB registrado com resultado e endereços de código na pilha. contentVersion 01.000.081.

## Título 0.83 — reservas fixas do Xbox realocadas

- Teste do 0.82 com Start (`console-reports/2026-10-29-title-0.82-start`): pool do jogo com total 0 e contagem 0; o laço de lista livre (`0x14125ef40`) corre sem fim. Origem: o gerenciador de memória do jogo (`0x14125f220` → `0x14125fcb0`) reserva regiões em `0x60000000000` e `0x800000000`, recusadas no console por estarem fora da área do título.
- Memory.cpp (console): reserva com endereço fixo fora de `0x1000000000`-`0xFC00000000` é realocada dentro da área, mantendo o alinhamento do endereço (até 4 GiB), e o endereço novo é devolvido. contentVersion 01.000.082.

## Título 0.84 — o jogo roda; funções que ele procura

- Teste do 0.83 com Start (`console-reports/2026-10-29-title-0.83-start`): reservas do Xbox realocadas, sem corrupção; o código do jogo rodou até `combase.dll!CoCreateFreeThreadedMarshaler` (`game+0x216dc7`). A reserva de 59 MB da imagem em `0x140000000` também tinha sido realocada (alinhamento 1 GiB) e falhado.
- Realocação restrita a reservas ≥ 1 GiB. Novos: `CoCreateFreeThreadedMarshaler` (objeto IUnknown, em `guest_abi.c`), `CoCreateGuid`, `BCryptGenRandom`, `RtlGetVersion`, `GetTempPath2W`, `LCIDToLocaleName`, portas de conclusão (`CreateIoCompletionPort`, `PostQueuedCompletionStatus`, `GetQueuedCompletionStatus` real); streams COM com `E_NOTIMPL`. contentVersion 01.000.083.

## Título 0.85 — limite da pilha nos logs; eventos de OVERLAPPED

- Teste do 0.84 com Start (`console-reports/2026-10-29-title-0.84-start`): falha (sinal 11) em `engineCallers()` (Memory.cpp), chamada pelo log de `VirtualAlloc2`, lendo `0x7eca04000` acima do topo da pilha de uma thread criada pelo jogo com 64/128 KiB.
- `engineCallers()` e `codeCallers()` (Kernel32.cpp) param no topo da pilha da thread (`currentStackTop()`, GuestThreads). `ReadFile`/`WriteFile` com OVERLAPPED sinalizam o evento da estrutura. contentVersion 01.000.084.

## Título 0.86 — MEM_WRITE_WATCH

- Teste do 0.85 com Start (`console-reports/2026-10-29-title-0.85-start`): sem falhas; motor parado na thread 348 por `VirtualAlloc` com tipo `0x203000` (`MEM_WRITE_WATCH`).
- Memory.cpp aceita `MEM_WRITE_WATCH`; `GetWriteWatch` (Misc.cpp) devolve todas as páginas do intervalo, de 4 KiB; novo `ResetWriteWatch`. contentVersion 01.000.085.

## Título 0.87 — relatório de esperas longas

- Teste do 0.86 com Start (`console-reports/2026-10-29-title-0.86-start`): sem falha; thread principal 256 e thread 368 presas em `WaitForSingleObject`, threads 344–364 girando em `SwitchToThread`, 360 em `WaitOnAddress`.
- Threads.cpp: espera com mais de 10 s é registrada uma vez (até 60 no total) com a descrição de cada objeto, quem o criou e os endereços de código de quem espera. `codeCallers()` exportada por Win32.h. contentVersion 01.000.086.

## Título 0.88 — relatório de giro

- Teste do 0.87 com Start (`console-reports/2026-10-29-title-0.87-start`): jogo em `0x205200000`. Thread 256 esperando o evento global do jogo `0x14354f7f8` (sinalizado só em `0x140f64f61`, na inicialização); 368 esperando evento criado pela 344; 344 girando em `SwitchToThread`.
- Threads.cpp: `SwitchToThread` e `Sleep(0)` contam por thread e registram os endereços de código a 100 000 chamadas e a cada 10× (até 40 relatórios). contentVersion 01.000.087.

## Título 0.89 — DebugInfo das seções críticas

- Teste do 0.88 com Start (`console-reports/2026-10-29-title-0.88-start`): threads 344–364 girando em `0x1413d0780` (trava com `TryEnterCriticalSection`, `SwitchToThread` e tempo limite), que só tenta entrar se `DebugInfo` da seção for diferente de zero.
- Kernel32.cpp: `InitializeCriticalSectionEx` grava `DebugInfo = -1`. contentVersion 01.000.088.

## Título 0.90 — WASAPI no host

- Teste do 0.89 com Start (`console-reports/2026-10-29-title-0.89-start`): inicialização destravada; falha na thread 344 em `0x140ff5d00` (escrita no endereço 0) depois de `CoCreateInstance(MMDeviceEnumerator)` recusado. O motor embrulha o enumerador real (`0x1401444b0`) para a interface de áudio do Xbox `8b557adc-…` e usa `GetDefaultAudioEndpoint`, `Activate(IAudioClient)` e `GetMixFormat`.
- Novos `win32/Audio.cpp` e `win32/audio_abi.c` (Makefile e `title/build.sh`), com enumerador, dispositivo, propriedades, `IAudioClient2`, render client, relógio e volumes. Thread de saída com mixagem para 48 kHz estéreo e `sceAudioOut` (porta 0xff, 256 quadros, S16 estéreo). `CoCreateInstance` cria o enumerador e registra os CLSIDs recusados. `libSceAudioOut.so` no link do título. contentVersion 01.000.089.

## Título 0.91 — D3DCompile com shaders pré-compilados

- Teste do 0.90 com Start (`console-reports/2026-10-29-title-0.90-start`): WASAPI criado; dispositivo D3D12 criado; parada em `D3DCOMPILER_47.dll!D3DCompile` (`engine+0x10d283`, shader `detile`).
- Os `D3DCompile` do motor (`engine+0x10d27d`, `+0xa150d`, `+0x329cf`, `+0x329ee`, `+0x17e2f2`) compilam cinco shaders fixos. Eles foram pré-compilados com o `d3dcompiler_47.dll` do Windows (`host/tools/precompile_shaders.py` gera `win32/CompiledShaders.inc`). Novo `win32/D3dCompiler.cpp`: o export procura o shader pelo SHA-256 da entrada e devolve um `ID3DBlob` constante (`guestConstantBlob` em `guest_abi.c`). contentVersion 01.000.090.

## Título 0.92 — exceções de nome de thread

- Teste do 0.91 com Start (`console-reports/2026-10-29-title-0.91-start`): shaders pré-compilados aceitos; parada na thread 416 por `RaiseException(0x406D1388)`, o nome de thread do Visual C++.
- Exceptions.cpp: `RaiseException` retorna para `0x406D1388` (registra o nome da thread) e para `0x40010006`/`0x4001000A`. contentVersion 01.000.091.

## Título 0.93 — swap chain DXGI e tela pelo WSI do RADV

- Teste do 0.92 com Start (`console-reports/2026-10-29-title-0.92-start`): nomes de thread aceitos; o motor parou em `CreateSwapChainForHwnd` (método 15 da fábrica DXGI, `E_NOTIMPL`) e encerrou com `TerminateProcess`.
- Novo `gpu/SwapChain.cpp`, com vtables em `dxgi_abi.c`. `CreateSwapChain`/`CreateSwapChainForHwnd` usam o `IDXGIVkSwapChainFactory` da fila (vkd3d-proton 3.0.1, GUID `e7d6c3ca-…`) e embrulham o `IDXGIVkSwapChain` num `IDXGISwapChain4`. O `IDXGIVkSurfaceFactory` do host fecha a porta VideoOut do launcher (`video::release`) e cria a superfície (`hostVulkanPresentSurface` em `Vulkan.cpp`): plano de tela se houver uma tela "VideoOut", senão headless. A instância Vulkan ganha `VK_KHR_display` e `VK_EXT_headless_surface` quando o driver os oferece. Novo `ntdll!NtDelayExecution`. A verificação D3D12 no Linux cria e apresenta um swap chain. contentVersion 01.000.092.

## Título 0.94 — divisão de placeholder em páginas

- Teste do 0.93 com Start (`console-reports/2026-10-29-title-0.93-start`): superfície de plano de tela na "PS5 VideoOut" (3840x2160) e swap chain de 2560x1440 criado. Parada em "graphics placeholder split error=0x57" (`azimuth+0x7e970`): `VirtualFree` de 41072 KiB com `MEM_PRESERVE_PLACEHOLDER`, recusado por não ser múltiplo de 64 KiB.
- Memory.cpp: divisão de placeholder alinhada à página (4 KiB), como no Windows. contentVersion 01.000.093.

## Título 0.95 — WIC (windowscodecs) no host

- Teste do 0.94 com Start (`console-reports/2026-10-29-title-0.94-start`): divisão de placeholder aceita; parada em "Cannot load desktop dependency windowscodecs.dll: 0x7e". O jogo (`0x14112dce0`) carrega `WindowsCodecs.dll` para `WICCreateImagingFactory_Proxy` e decodifica imagens da memória sem conferir se a fábrica é nula.
- Novos `win32/Wic.cpp` e `win32/wic_abi.c` (Makefile e `title/build.sh`), com fábrica, stream, decodificador, quadro e conversor. Novo `third_party/stb/stb_image.h` v2.30 (PNG, JPEG, BMP, TGA e GIF). Novo export `windowscodecs!WICCreateImagingFactory_Proxy`. Novo `tests/WicTest.cpp` (`make test-wic`). contentVersion 01.000.094.

## Título 0.96 — módulo do host por endereço

- Teste do 0.95 com Start (`console-reports/2026-10-29-title-0.95-start`): "GetProcAddress unsupported export=WICCreateImagingFactory_Proxy". O motor devolve ao jogo o módulo que contém o identificador carregado (`GetModuleHandleExW` com `FROM_ADDRESS`, `0x14006d5f0`), e o host não reconhecia os identificadores das próprias bibliotecas.
- Kernel32.cpp: `GetModuleHandleExW(FROM_ADDRESS)` devolve o identificador de uma biblioteca do host; `GetModuleFileNameW` dá a ela um caminho em System32. `tests/WicTest.cpp` cobre a sequência do motor. contentVersion 01.000.095.

## Título 0.97 — primeira tela do jogo; VirtualQuery em qualquer endereço

- Teste do 0.96 com Start (`console-reports/2026-10-30-title-0.96-start`): a sequência do motor para o WIC passou, o swap chain apresentou o primeiro quadro e **a primeira tela do jogo apareceu na TV**. Depois, parada na thread 400 ("Azimuth worker") por `VirtualQuery(0x213338000)` num bloco recém-liberado.
- Memory.cpp: `VirtualQuery` fora das regiões do `VirtualAlloc` responde `MEM_FREE` até a próxima região, `MEM_IMAGE` no motor e nos DLLs, e `MEM_PRIVATE` no heap do host. `tests/MemoryTest.cpp` cobre a divisão em 16 KiB e o `VirtualQuery` de memória liberada. contentVersion 01.000.096.

## Título 0.98 — XAudio2 no host

- Teste do 0.97 com Start (`console-reports/2026-10-30-title-0.97-start`): primeira tela de novo; parada em "Cannot load desktop dependency xaudio2_9.dll". O FMOD carrega o XAudio2; o motor atende `CreateXAudio2Object` com o `XAudio2Create` do Windows (`0x140141720`) e limita as vozes a PCM/float sem envios nem efeitos.
- Novos `win32/XAudio2.cpp` e `win32/xaudio2_abi.c` (Makefile e `title/build.sh`), export `xaudio2_9!XAudio2Create`. Thread do mixer criada com `CreateThread` (callbacks do FMOD), passos de 10 ms, saída por `hostAudioStreamOpen`/`Write` (`Audio.cpp`). A verificação D3D12 no Linux testa o XAudio2. contentVersion 01.000.097.

## Título 0.99 — relatório de exceção C++/CX; GetCurrentPackageId

- Teste do 0.98 com Start (`console-reports/2026-10-30-title-0.98-start`): XAudio2 do FMOD funcionando (voz principal 2x48000 e cinco vozes PCM) e porta `sceAudioOut` aberta. Depois, `Platform::COMException` na thread 344 e tela congelada. Antes disso, o jogo procurou `GetCurrentPackageId`.
- Exceptions.cpp: a exceção C++ registra quem a lançou e os bytes do objeto. Kernel32.cpp: `GetCurrentPackageId`/`FullName`/`FamilyName` devolvem `APPMODEL_ERROR_NO_PACKAGE`. contentVersion 01.000.098.

## Título 1.00 — tratamento de exceções do Windows

- Teste do 0.99 com Start (`console-reports/2026-10-30-title-0.99-start`): `COMException` 0x8007048F (`ERROR_DEVICE_NOT_CONNECTED`) do `SpeechRecognizerFactory` (Kinect) emulado pelo motor. O jogo a captura no Windows; o host não despachava exceções.
- `guest_abi.c`: entradas em assembly para `RaiseException` e `RtlUnwindEx` (com captura de `CONTEXT`), trampolins `guestCallHandler`/`guestCallConsolidate` e `guestRestoreContext`. `Exceptions.cpp`: `RtlVirtualUnwind`, despacho pelos manipuladores de linguagem, `RtlUnwindEx` com quadros colididos, consolidação (`catch`) e `longjmp`; `RtlPcToFileHeader` pelas tabelas registradas. `Image.cpp`: DLL sem ponto de entrada. Novo `tests/seh/` (`sehtest.dll`), verificado pelo `host --d3d12`: 31 de 31. contentVersion 01.000.099.

## Título 1.01 — reservas pequenas em memória direta

- Teste do 1.00 com Start (`console-reports/2026-10-30-title-1.00-start`): **o jogo iniciou**, com as telas de abertura e a `COMException` do Kinect capturada. Depois, tela preta e "XMemAlloc alloc failed". As reservas pequenas por `mmap` esgotaram a janela do título (64 falhas com faixa `0x100000000-0xffffffffff`), o motor recorreu a blocos de 1 a 8 GiB e a memória direta acabou.
- Memory.cpp: no console, toda reserva é só endereço (`sceKernelReserveVirtualRange`) com memória direta nos commits; um bloco que precise ser executável e seja recusado passa para memória flexível no mesmo endereço (`toFlexible`). contentVersion 01.000.100.

## Título 1.02 — faixas de endereço fora da área do título

- Teste do 1.01 com Start (`console-reports/2026-10-30-title-1.01-start`): imagem do jogo em memória direta executando normalmente. 46 reservas de 128 KiB com faixa `0x20000000000-0x3ffffffffff` recusadas pelo kernel (`0x8002000c`); ponteiro nulo antes das telas de abertura.
- Memory.cpp (`reserveAddresses`): a faixa é limitada à área do título e ignorada quando fica toda fora dela. contentVersion 01.000.101.

## Título 1.03 — FFmpeg (SHAPE) e CRT Universal

- Teste do 1.02 com Start (`console-reports/2026-10-30-title-1.02-start`): memória estável; parada em "SHAPE graph: Pinned local FFmpeg DLL load failed error=126".
- Modules.cpp: `avutil-60` e `avcodec-62` carregáveis; dependências carregáveis carregadas antes; imports entre DLLs pelo nome do arquivo. Kernel32.cpp/Modules.cpp: `api-ms-win-crt-*` vai para o msvcrt. Msvcrt.cpp: 69 funções da CRT Universal. Kernel32.cpp: funções de console. A verificação D3D12 no Linux carrega o FFmpeg e acha o decodificador xma2. contentVersion 01.000.102.

## Título 1.04 — DLL lida sem o malloc do título

- Teste do 1.03 com Start (`console-reports/2026-10-30-title-1.03-start`): "avutil-60.dll nao carregado: arquivo vazio ou ilegivel". O `fopen` do carregador falhou, porque o malloc do título já estava cheio quando o motor carregou o FFmpeg.
- Modules.cpp: `readFile` com `open`/`read` e o `errno` no log. contentVersion 01.000.103.

## Título 1.05 — descritores de arquivo

- Teste do 1.04 com Start (`console-reports/2026-10-30-title-1.04-start`): o `open` do `avutil-60.dll` falhou com `errno 24` (`EMFILE`).
- main.cpp: `RLIMIT_NOFILE` registrado e elevado ao máximo. Files.cpp: arquivos abertos por `CreateFileW` contados por caminho, com relatório no primeiro `EMFILE` (`reportOpenDescriptors`, também chamado pelo carregador de módulos). contentVersion 01.000.104.

## Título 1.06 — cache de descritores de arquivo

- Teste do 1.05 com Start (`console-reports/2026-10-30-title-1.05-start`): `EMFILE` com 239 arquivos abertos pelo `CreateFileW` (165 em `media/cars`). O limite real fica perto de 256, apesar do `RLIMIT_NOFILE` de 13952, e o motor parava por não conseguir abrir o FFmpeg.
- Files.cpp: `File` guarda o caminho, as flags de reabertura e a posição. As leituras e escritas usam `pread`/`pwrite`. Os descritores ficam num cache LRU de 96, com `Descriptor` fixando o descritor durante cada operação. `SetFilePointerEx`/`SetEndOfFile` usam a posição do handle, e os mapeamentos de arquivo leem pelo cache.
- tests/FilesTest.cpp (`make test-files`). contentVersion 01.000.105.

## Título 1.07 — XInput (controle no jogo)

- Teste do 1.06 com Start (`console-reports/2026-10-30-title-1.06-start`): o jogo chegou ao menu, mas sem controle. O motor procurou `xinput1_4.dll` e registrou "desktop XInput unavailable; no controller exposed".
- win32/Xinput.cpp: XInput do desktop sobre o ScePad, com o usuário 0 sendo o controle do PS5. Input.cpp: `vibrate` usa `scePadSetVibration`. Exports `xinput1_4` em exports.inc. contentVersion 01.000.106.

## Título 1.08 — sem aperto duplo no launcher

- Teste do 1.07 (`console-reports/2026-10-30-title-1.07-start`): o launcher via cada aperto duas vezes, pelo XInput e pelas teclas que o host enviava.
- Input.cpp: as teclas descem só se não houve `XInputGetState` no último segundo (`polledByXInput`), e uma tecla que desceu sempre sobe. contentVersion 01.000.107.

## Título 1.09 — espaço em disco sem statvfs

- Teste do 1.08 (`console-reports/2026-10-30-title-1.08-start`): o launcher funcionou, mas ao iniciar o jogo pelo menu deu SIGBUS (trap 9) no meio de uma instrução do jogo, logo depois da primeira chamada a `GetDiskFreeSpaceExW` (cota de save do motor).
- Files.cpp: no console, `GetDiskFreeSpaceExW` não usa `statvfs` (a estrutura do console é maior que a dos cabeçalhos e a chamada corrompia a pilha) e responde 256 GiB livres de 1 TiB. contentVersion 01.000.108.

## Título 1.10 — ConcRT (agendador do msvcr110) e pool de threads

- Teste do 1.09 (`console-reports/2026-10-30-title-1.09-start`): `scheduler_resource_allocation_error` não tratada depois de `GetProcAddress` falhar para `SetThreadGroupAffinity` e `GetThreadGroupAffinity`.
- Threads.cpp: afinidade por grupo, `GetCurrentProcessorNumberEx`, `GetLogicalProcessorInformationEx`, `CreateRemoteThreadEx`, listas de atributos de thread, UMS sem suporte, esperas e timers do pool de threads (inclusive `RegisterWaitForSingleObject` e a fila de timers), ETW sem rastreio. guest_abi.c: `guestPoolThreadStart`. Misc.cpp: `combase!RoInitialize`/`RoUninitialize`.
- D3d12Check.cpp: verificação do ConcRT (8 tarefas) e do pool de threads (timer e espera). contentVersion 01.000.109.

## Título 1.11 — FreeLibraryWhenCallbackReturns

- Teste do 1.10 (`console-reports/2026-10-30-title-1.10-start`): ConcRT rodando. Depois, SIGSEGV em `hostMissingImport`, cuja varredura da pilha passou do topo numa thread do pool e escondeu o import ausente (`FreeLibraryWhenCallbackReturns`, chamado pelo callback de espera do ConcRT).
- Threads.cpp: `FreeLibraryWhenCallbackReturns`, `CallbackMayRunLong`, `DisassociateCurrentThreadFromCallback`. GuestThreads.cpp: `hostMissingImport` limitado ao topo da pilha. D3d12Check.cpp: tarefa agendada por uma thread externa que termina. contentVersion 01.000.110.

## Título 1.12 — fibras

- Teste do 1.11 (`console-reports/2026-10-30-title-1.11-start`): o motor parou em `kernelx.dll!ConvertThreadToFiber` (game+0x108736a), porque as fibras do Windows não existiam no host.
- win32/Fibers.cpp: fibras do Windows com troca de pilha (`fiberSwitch`/`fiberStart` em guest_abi.c), o TEB acompanhando a fibra atual (FiberData, StackBase/StackLimit, HasFiberData) e `setFiberStackTop` em GuestThreads para os relatórios e a amostragem. D3d12Check.cpp: verificação das fibras (trocas, xmm, GetFiberData, exceções numa fibra). contentVersion 01.000.111.

## Título 1.13 — seções críticas com dono e relatório

- Teste do 1.12 (`console-reports/2026-10-30-title-1.12-start`): fibras funcionando. O jogo travou carregando o mundo aberto (processo vivo, memória folgada), com as threads 344, 352 e 380 paradas em `EnterCriticalSection`.
- Kernel32.cpp: `CRITICAL_SECTION` com dono e recursão próprios (qualquer thread pode liberar, como no Windows; antes, o `pthread_mutex` recursivo recusava a liberação por outra thread) e relatório de espera de 10 s com a thread dona. D3d12Check.cpp: verificação das seções críticas. contentVersion 01.000.112.

## Título 1.14 — travas sem objetos do kernel

- Teste do 1.13 (`console-reports/2026-10-30-title-1.13-start`): mais longe no carregamento do mundo aberto. Depois, SIGABRT na libkernel em duas threads dentro de `EnterCriticalSection` ("SceKernelInternalMemory"): o mutex e a condição por seção crítica esgotaram o pool interno da libkernel.
- Kernel32.cpp: `CRITICAL_SECTION` e `SRWLOCK` com o estado na própria estrutura (operações atômicas) e espera em 64 pares mutex/condição compartilhados. Threads.cpp: `CONDITION_VARIABLE` como contador de geração nos mesmos moldes. Removido `soon()`. contentVersion 01.000.113.

## Título 1.15 — diagnóstico das fibras

- Teste do 1.14 (`console-reports/2026-10-30-title-1.14-start`): sem abort da libkernel. SIGSEGV em `States::StopTimeOfDay` (0x14093ca20), com o objeto em `+0x480`, que `ForzaSimulation` cria, ainda nulo.
- Fibers.cpp: log das trocas de fibra (as 40 primeiras e uma a cada 1000), de `DeleteFiber` e do retorno da rotina. contentVersion 01.000.114.

## Títulos 1.16 e 1.17 — mundo aberto: portas de conclusão, log assíncrono, cache de stat

- Teste do 1.15: uma execução chegou ao mundo aberto (carro, HUD e tráfego, sem cenário, câmera lenta); as outras caíram em `StopTimeOfDay`. Teste do 1.16 (`console-reports/2026-10-30-title-1.16-start`): mesma queda; ~30 quadros/s com o motor em 60 e ritmo fixo; 172 mil `GetFileAttributesW`/s na thread que roda a fibra de carregamento.
- 1.16 — Files.cpp/Threads.cpp: arquivos ligados a portas de conclusão (pacote na conclusão, `ERROR_IO_PENDING`), `SetFileCompletionNotificationModes`, `GetOverlappedResultEx` com fim de arquivo. main.cpp: log por fila e thread própria, `fsync` a cada 500 ms, linha de desempenho (quadros/s e threads que mais chamam o host). Memory.cpp: logs repetitivos limitados. tests/FilesTest.cpp: porta de conclusão.
- 1.17 — Files.cpp: cache de `stat` da pasta `Mount` do jogo (inclusive ausências). SwapChain.cpp: tempo dentro do `Present`, `SyncInterval` e flags na linha de desempenho. main.cpp: `host-anterior.txt`. contentVersion 01.000.116.

## Título 1.18 — fibras no relatório de parada

- Teste do 1.17 (`console-reports/2026-10-30-title-1.17-start`): cache de `stat` funcionando; mesma queda em `StopTimeOfDay`. `Present` com ~0,45 ms e `SyncInterval` 1. A câmera lenta vinha do `frame_rate=60` do `azimuth.ini` com o jogo a ~30 quadros/s (ritmo fixo); com `frame_rate=30`, o jogo entrega 29–30.
- Fibers.cpp: registro das fibras (rodando/suspensa, thread) e `describeFibers`, com os endereços de código na pilha de cada fibra suspensa; main.cpp registra isso na parada. contentVersion 01.000.117.

## Título 1.19 — despertar direcionado nas esperas

- Teste do 1.18 (`console-reports/2026-10-30-title-1.18-start`): a fibra de carregamento estava suspensa no "esperar job" do jogo (`0x1413d9bb0`), chamado pela inicialização do `ForzaSimulation`. O job demorava vários quadros porque cada `SetEvent`/`WakeByAddress` acordava todas as threads em espera do processo.
- Threads.cpp: cada thread em espera tem a própria condição e registra os objetos; os sinais (`SetEvent`, semáforo, mutex, fim de thread, timer, porta de conclusão) acordam só quem espera por aquele objeto. `WaitOnAddress`/`WakeByAddress*` em 64 baldes por endereço. contentVersion 01.000.118.

## Título 1.20 — tempo no host por função e por thread

- Teste do 1.19 (`console-reports/2026-10-30-title-1.19-start`): **mundo aberto com cenário completo**, sem a queda. Câmera lenta: 11–12 quadros/s no mundo aberto (30 no menu) com ritmo fixo de 30. Mais de 3,5 milhões de chamadas ao host por segundo, 1,2 milhão numa só thread.
- guest_abi.c: as entradas `EXPORT` marcam a saída (`hostLeave`). GuestThreads.cpp: ciclos por função e por thread (sem as esperas) e `hostTimeReport`, na linha "Tempo no host" a cada 10 s. contentVersion 01.000.119.

## Título 1.21 — cache de leitura dos arquivos do jogo; QueryPerformanceCounter por rdtsc

- Teste do 1.20 (`console-reports/2026-10-30-title-1.20-start`): a queda em `StopTimeOfDay` voltou (corrida ainda possível). Tempo no host: `ReadFile` ~1 ms por chamada, com a thread de arquivos saturada; `QueryPerformanceCounter` ~930 ns.
- Files.cpp: cache LRU de blocos de 128 KiB (até 256 MiB) para leituras de arquivos da pasta `Mount` abertos só para leitura; leituras ≥ 512 KiB vão direto. Kernel32.cpp: `QueryPerformanceCounter` por `rdtsc`, calibrado uma vez. Sem testes no Linux, a pedido do usuário. contentVersion 01.000.120.

## Título 1.22 — cache para todos os arquivos do jogo; log das leituras

- Teste do 1.21 (`console-reports/2026-10-30-title-1.21-start`): mesma queda; `ReadFile` ainda ~1 ms (o cache não pegou essas leituras); a thread 348 é o carregador de arquivos do jogo.
- Files.cpp: o cache vale para qualquer arquivo da pasta `Mount` (escrita descarta o cache); log das 40 primeiras leituras fora do cache e de toda leitura ≥ 5 ms. contentVersion 01.000.121.

## Título 1.23 — contorno da queda em StopTimeOfDay

- Teste do 1.22 (`console-reports/2026-10-30-title-1.22-start`): o carregador lê ~64 KiB em sequência a ~38 MB/s (limite do pendrive); a queda segue sendo a corrida entre o script de estados e o carregamento.
- GuestThreads.cpp: o tratador de falhas reconhece a instrução do `States::StopTimeOfDay` (assinatura de 9 bytes, `rax` nulo, acesso em `0x27`) e pula a marcação, continuando a thread. contentVersion 01.000.122.

## Título 1.24 — jogo no SSD interno

- Teste do 1.23 (`console-reports/2026-10-30-title-1.23-start`): o contorno do `StopTimeOfDay` funcionou, mas outro passo que depende da simulação caiu logo depois (`0x141981bb7`). A causa de fundo é o carregamento a ~38 MB/s pelo pendrive.
- main.cpp: `/data/PC/FH2/azimuth.exe` é o primeiro candidato. Files.cpp: resolução de maiúsculas/minúsculas por listagens guardadas para montagens em `/data`. contentVersion 01.000.123.

## Título 1.25 — fontes sem diferença de caixa

- Teste do 1.24: com o jogo em `/data/PC/FH2`, o launcher não achou `arial.ttf` (os arquivos estão como `ARIAL.TTF` em `/data/azimuth-ps5/fonts`).
- Gdi.cpp: `loadFontFile` procura o arquivo na pasta ignorando maiúsculas e minúsculas quando o nome exato não existe. contentVersion 01.000.124.

## Resultado do 1.25 — jogo no SSD interno

- Com o jogo em `/data/PC/FH2` e as fontes renomeadas corretamente (`arial.ttf`, `arialbd.ttf`), **o jogo abriu normalmente e entrou no mundo aberto**, sem a queda no carregamento. **Continua em câmera lenta** (no 1.19 o mundo aberto rodava a 11–12 quadros/s com `frame_rate=30` e ritmo fixo).
- Base de conhecimento (`docs/BASE_DE_CONHECIMENTO_PS5.md`) atualizada até o 1.25.

## Título 1.26 — relógio por rdtsc em todo o host; wake sem dorminhoco

- Medição do 1.25 no mundo aberto (`console-reports/2026-10-30-title-1.25-mundo`): 10–12 quadros/s, `Present` 0,01 ms. Thread 400 do motor com 34% do tempo no host: `GetTickCount64` 953 ns (syscall), `AcquireSRWLockExclusive` 469 ns, `WakeAllConditionVariable` 6,5 µs.
- Kernel32.cpp: `fastMonotonicNs()` (rdtsc com âncora reajustada ao `CLOCK_MONOTONIC` a cada segundo, sob contador de sequência) para `GetTickCount`/`GetTickCount64`/`QueryPerformanceCounter`. Threads.cpp: `Clock::now()` usa o mesmo relógio, mantendo os time_points do `steady_clock`. Variáveis de condição contam quem dorme, e o wake sem ninguém dormindo não pega a trava. contentVersion 01.000.125.

## Título 1.27 — dependências dentro do pacote

- Teste do 1.26 (`console-reports/2026-10-30-title-1.26-abertura`): "RADV: libvulkan.prx nao encontrado", depois "no DXGI adapter". O Playden se atualizou para a 0.8.0 e removeu `/data/homebrew/FAKE78001`, de onde o host carregava o driver.
- Pedido: não depender de nenhum outro programa. `title/vendor/radv/libvulkan.prx` (SHA-256 db21b394…) e `title/vendor/dll/d3d12.dll`/`d3d12core.dll` (vkd3d-proton, 08e44b05…/5b4257b3…) entram no pacote em `radv/` e `dll/`, com conferência de hash no `package.py`. Radv.cpp e Modules.cpp procuram só na pasta do título e em `/data/azimuth-ps5`. Licenças MIT (Mesa) e LGPL-2.1 (vkd3d-proton) em `licenses/`. contentVersion 01.000.126.


## 30/10/2026 — título 1.28: esperas e WriteWatch

Consultas imediatas de WaitForSingleObject/Ex sem vetores; registro dos objetos adiado até precisar dormir; wake de WaitOnAddress evita buckets vazios. Contadores periódicos de consultas, bytes, páginas e resets de GetWriteWatch, ainda conservador. Teste dedicado de sincronização com 200 disputas entre wait e wake. Detalhes em title/README.md. Ainda não validado no console. Captura da 1.27: console-reports/2026-10-30-title-1.27-watch-212046/ANALISE.md.


## 30/10/2026 — título 1.29: rastreamento real de escrita

WriteWatch.cpp substitui a resposta sempre suja por detecção via proteção de memória e SIGSEGV, integrada ao ciclo de vida das reservas e proteções. Marca conservadoramente até quatro páginas Windows por bloco de 16 KiB no PS5. Reset e faults sincronizados por bloco; handler sem a trava de regiões. Testes dedicados com blocos de 4/16 KiB e escritas concorrentes. Detalhes e limites em title/README.md. Ainda não validado no console.


## 30/10/2026 — título 1.30: VEH para memória selada e diagnóstico de falha

A 1.29 expôs o uso, pelo motor, de AddVectoredExceptionHandler para reabrir memória selada: o host ainda aceitava o registro sem chamar o handler. Implementados registro/remoção e despacho de SIGSEGV de acesso ao callback Windows, com tradução de CONTEXT e retomada apenas mediante retorno -1. Removidas leituras inseguras de RDI/pilha no diagnóstico fatal. Testes reproduzem MOVNTI em memória selada e a falha com RDI inválido, com blocos de 4/16 KiB. Detalhes em title/README.md. Console pendente.


## 30/10/2026 — 1.30 entra no mundo; captura das engasgadas

Usuário confirmou jogo funcionando na 1.30. Captura de 301,5 s: sessão principal PID 418 em 2160p/AA off/limite 30 teve 25 amostras novas, média 29,036 FPS, duas amostras mínimas de 26,6; sem FALHA FATAL no host nem GPU_FAULT no klog capturado. As quedas coincidiram com maior tempo agregado de leitura/alocação/liberação e SRW; é hipótese de streaming/gestão de memória, ainda sem causalidade por frame. Ao final houve nova execução PID 419 com limite 60/AA 4x, excluída da comparação. Relatório: console-reports/2026-10-30-title-1.30-stutter-225142/ANALISE.md. Próxima medição: distribuição dos intervalos entre frames e eventos lentos de I/O/memória durante toda a sessão.


## 30/10/2026 — título 1.31: proteção agrupada e diagnóstico de stutter

Commit agrupa mprotect em faixas uniformes sem WriteWatch, preservando fallback por bloco. WriteWatch evita reaplicar proteções idênticas, invalidando o estado quando backing/commit pode mudar. Acrescentados máximo/p95/p99 e contagem de intervalos de Present acima de 50/100 ms; leituras lentas amostradas ao longo de toda a sessão com thread/arquivo. Testes de faixas mistas, histogramas e regressão WriteWatch/VEH. Impacto no PS5 pendente. Detalhes em title/README.md.


## 30/10/2026 — captura da 1.31 a 60 FPS

PID 420, 2160p/MSAA 4x/limite 60. Trinta janelas: 57,73 FPS de média, 53–60,1; 127 intervalos acima de 50 ms (36 acima de 100), máximo 198,55 ms. Sem falha fatal/GPU_FAULT registrada. Picos de WriteWatch/proteção e leituras de GeoChunk2/3 na thread 348, até 53,622 ms. O buffer intermediário de 8 KiB da leitura para memória rastreada é um alvo concreto para reduzir fragmentação; causalidade por pausa ainda não estabelecida. Evidências: console-reports/2026-10-30-title-1.31-stutter-231051/ANALISE.md.


## 30/10/2026 — título 1.32: buffer reutilizável de I/O rastreado

Buffers de 256 KiB do heap substituem o intermediário de 8 KiB em pread/read/fread para memória rastreada. Reservas exclusivas por operação suportam reentrada por VEH, até quatro buffers retidos por thread e fallback de 8 KiB. Nova telemetria separa leitura e cópia, incluindo faults no tempo de cópia. Testes de arquivo >4 MiB, EOF/erro, reset concorrente e I/O reentrante no VEH. Detalhes em title/README.md. Console pendente.


## 30/10/2026 — captura da 1.32 em 60 FPS

PID 421, 2160p/MSAA 4x/60. Vinte e quatro janelas novas: média 57,05 FPS, 120 intervalos >50 ms (35 >100), máximo 198,26 ms. Sem falha fatal registrada; encerramento do processo pelo sistema no fim. Ponte I/O: 6.640 operações, 1.616.941.056 bytes, leitura agregada 1.828,12 ms e cópia 4.493,03 ms; fallback 8 KiB não usado. Buffer maior confirmado, engasgadas persistem. WriteWatch somou 943.622 mprotect, ainda sem medição específica de tempo desses calls/faults. Relatório: console-reports/2026-10-30-title-1.32-stutter-232923/ANALISE.md.


## 30/10/2026 — título 1.33: preparação e reset do WriteWatch em lotes

Cópias de I/O preparam páginas committed já graváveis em lotes, preservando VEH para páginas seladas e fault normal após reset concorrente. Consulta/reset agrupam reproteção antes de limpar histórico; buffer insuficiente mantém páginas não devolvidas. Guardas por endereço, máximo 32 blocos, sem tocar no guest enquanto guardadas; fallback individual se o kernel rejeita uma faixa. Medição de mprotect/fault/query/reset/preparo. Testes de concorrência, reentrada, conteúdo e recusa simulada de faixas; PS5 pendente. Detalhes em title/README.md.


## 31/10/2026 — título 1.34: VirtualProtect rastreado em lotes

Usuário reportou engasgadas na 1.33; sem captura nova porque o console estava desligado. Revisão identificou commit/VirtualProtect de regiões watch ainda aplicando uma proteção por bloco. A 1.34 agrupa até 32 blocos contíguos com mesma proteção efetiva, mantendo dirty, permissões, invalidação por backing e fallback individual. Testes ampliados para selagem/reabertura grande, histórico e bordas de lotes. Ganho no console ainda não medido. Detalhes em title/README.md.

Validação local 1.34: regressões WriteWatch, VEH e memória passaram em 4/16 KiB; build nativo e hashes do ZIP conferidos. Selagem/reabertura de ~4 MiB com blocos de 16 KiB usou 9 chamadas em vez de 257. Pacote em title/dist/Azimuth-PS5-title-1.34.zip; console pendente.


## 31/10/2026 — captura da 1.34: melhora com pausas residuais

PID 661, 2160p/MSAA 4x/high/60/fixed/vsync. Cinco minutos, 30 janelas novas: média 59,15 FPS (55,4–60,1), 36 intervalos >50 ms e três >100, máximo 138,39 ms. Sem fatal/GPU fault registrado. Cópia rastreada somou 532,02 ms para 1,857 GB, máximo 0,51 ms; leitura 3.811,64 ms. 384.166 mprotect e 341.165 faults ainda somam custo, mas nenhum evento individual medido explica a maior pausa. 30.053 avisos repetidos XMA em stderr são pista secundária, sem causalidade estabelecida. Comparação observacional com 1.31/1.32 indica menos pausas; não há A/B controlado nem captura da 1.33. Relatório: console-reports/2026-10-31-title-1.34-stutter-233452/ANALISE.md. Captura encerrada.


## 31/10/2026 — título 1.35: escritas grandes rastreadas e avisos XMA

Preparação em lotes nas chamadas importadas memcpy/memmove/memset >=16 KiB com destino rastreado, preservando permissões, VEH, sobreposição e reset concorrente. Métricas por função mostram o uso real. Compactação apenas do aviso repetido XMA de timestamps, preservando primeira ocorrência por janela e contagem no watchdog. Não altera o decodificador nem motor/INI. Testes ampliados e console pendente; detalhes em title/README.md.

Validação local 1.35 concluída: áudio/contagens concorrentes e regressões WriteWatch/CRT, VEH e memória em 4/16 KiB passaram; build nativo e hashes do ZIP conferidos. Pacote title/dist/Azimuth-PS5-title-1.35.zip; efeito no PS5 pendente.


## 01/11/2026 — título 1.36: contenção e diagnóstico por evento

Consulta da 1.35 com jogo fechado: zero amostras novas; histórico CRT zerado em 25 relatórios, filtro XMA ativo. Tabela de handles dividida em 64 partições com tempo de disputa medido; sinalização de objetos passa a notificar sob a mesma aquisição de waitLock. Preserva lifetime de referências, consumo e wait-all; mutex recursivo só notifica ao liberar o último nível. Eventos de memória >2 ms são guardados sem log/alocação no produtor; quadros >50 ms recebem timestamps amostrados para correlação. Testes concorrentes de handles e sincronização ampliados. Detalhes e limites em title/README.md; PS5 pendente.

Validação local 1.36 concluída: handles, sincronização, arquivos, WriteWatch/CRT/VEH, memória 4/16 KiB e histograma passaram. Runtime/ELF/SELF e hashes do ZIP conferidos. Pacote em title/dist/Azimuth-PS5-title-1.36.zip; console pendente.


## 01/11/2026 — capturas 1.36 e título 1.37 com FPS a 100 ms

Travadas confirmadas por intervalos, não pelo mínimo médio de dez segundos. Gameplay de dois minutos teve 12 intervalos >50 ms, máximo 73,75 ms; observação de overlay a 47 FPS preservada com snapshot separado. Liberações de memória são a principal pista residual entre eventos registrados, mas sem causa por quadro provada e com perdas de eventos. Relatórios em console-reports/2026-11-01-title-1.36-*/ANALISE.md.

1.37 acrescenta amostrador independente de 100 ms com contagem/tempo real da janela, max intervalo e idade do último quadro, incluindo janelas de zero quadros durante travamento. Mantém diagnóstico de dez segundos independente e logger assíncrono, sem sync por linha. Testes de FPS conhecidos, stall e atraso do amostrador. Mudança diagnóstica solicitada, não promessa de ganho de performance. Detalhes em title/README.md.

Validação local 1.37 concluída: janelas de FPS e histograma passaram; build nativo e hashes conferidos. Pacote title/dist/Azimuth-PS5-title-1.37.zip. Ambas as capturas 1.36 concluídas; a amostragem de 100 ms requer instalar a 1.37.


## 01/11/2026 — captura 1.37: quedas curtas agora mensuradas

Um minuto de gameplay, PID 904, 2160p/MSAA 4x/high/60/fixed/vsync: 600 janelas de ~100 ms, média ponderada 59,05 FPS, mínimo 10 FPS em uma janela de um quadro/100,001 ms; 19 janelas com até quatro quadros. Isso não significa 10 FPS sustentados. Maior intervalo 85,169 ms, amostrador regular (99,933–100,076 ms), sem janela de zero quadros. Agregados antigos mostravam mínimo médio de 57,9: confirmado que ocultavam as quedas curtas. Sem fatal/GPU fault registrado. Eventos lentos de memória continuam dominados por VirtualFree, com 277 descartados por lotação e sem causalidade por frame provada. Relatório: console-reports/2026-11-01-title-1.37-gameplay-1min-140105/ANALISE.md. Captura encerrada.


## 01/11/2026 — título 1.38: liberação direta agrupada

unback passa a agrupar backing virtual/físico contíguo da mesma alocação, preservando lacunas e backing flexível. MEM_RELEASE remove a reserva inteira uma vez e não reserva cada bloco novamente; decommit mantém reservas. Fallback por bloco para faixas recusadas, sem ignorar falha individual. Novo contador de liberações e buffer de eventos lentos de 1.024 slots; FPS de 100 ms mantido. Testes do algoritmo compartilhado com kernel simulado e regressões de memória/WriteWatch/VEH. Console pendente; detalhes em title/README.md.

Validação local 1.38 concluída: algoritmo de liberação com kernel simulado, regressões de memória/WriteWatch/CRT/VEH 4/16 KiB e FPS 100 ms passaram. Pacote nativo e hashes conferidos; console pendente.


## 01/11/2026 — título 1.39: nome e símbolo Azimuth Launcher

Usuário confirmou bom funcionamento da 1.38 e pediu novo nome/ícone. Metadados pt-BR/en-US e notificações passam a Azimuth Launcher; ícone passa a reproduzir o símbolo Azimuth em branco sobre fundo escuro, com fonte SVG editável e geração RGB 512 x 512. titleId e comportamento da 1.38 mantidos. Pacote Azimuth-Launcher-1.39.zip; detalhes em title/README.md.


## 01/11/2026 — título 1.40: commit de memória direta em pedaços

Crash da 1.39 após ~10 min em 4K/MSAA 4x/60 (PID 932): "Commit: memoria direta esgotada", `VirtualAlloc` NULL e SIGSEGV de escrita em 0 na thread 380. Maior trecho contíguo livre de 12 GB → 3 MiB com o host estável em ~5,2 GB: fragmentação. `back()` mantém o pedido contíguo como caminho normal; na falha, preenche com os maiores pedaços livres (identidade de alocação por pedaço). Nova métrica "memoria direta em uso". Link corrigido para o trabalho do motor 1.0.2 deixado pela metade (stub duplicado de DWriteCreateFactory removido; dwrite_abi.c no build). Relatório: console-reports/2026-11-01-title-1.39-crash/ANALISE.md. Console pendente.


## 01/11/2026 — título 1.41: imports restantes do motor Azimuth 1.0.2

Fechado o suporte do host ao motor 1.0.2, que o Codex deixou pela metade. O SHA-256 do 1.0.2 já era aceito na Etapa 1 junto com o do 0.9.7. Dos 287 imports, faltavam 11, e agora todos têm implementação. Thread snapshot, `NtQueryInformationThread` (TEB real, registrado por thread em `GuestThreads.cpp`) e `SuspendThread`/`GetThreadContext` que só contam, para o motor copiar o template de TLS estático dos módulos do jogo em todas as threads. `CreateThreadpoolIo` e família sobre uma porta de conclusão própria. `K32EnumProcessModules`, `K32GetMappedFileNameW`, `QueryDosDeviceW` e `GetSystemWindowsDirectoryW` para o relatório de módulos. `CreateProcessW` recusado (`ERROR_NOT_SUPPORTED`): o título não reinicia o motor. `OpenThread` agora também abre a thread principal. Os caminhos quentes de memória, WriteWatch, esperas e travas não mudaram. contentVersion 01.000.141. Detalhes em title/README.md. Sem teste no Linux; console pendente para o 1.0.2 e para a regressão do 0.9.7.


## 01/11/2026 — título 1.42: WIC por CoCreateInstance e por arquivo (motor 1.0.2)

Primeira execução do 1.0.2 (1.41) parou em "Montserrat-300.ttf did not load": o console tinha o data\bin do 0.9.7. A pasta foi trocada pela do 1.0.2 (a antiga ficou em data\bin-0.9.7). Segunda execução parou em "azimuth-mark.png did not load ... HRESULT=0x80040154": o motor 1.0.2 cria o WIC com `CoCreateInstance(CLSID_WICImagingFactory2)` e lê o PNG com `CreateDecoderFromFilename` (slot 3 da fábrica). `CoCreateInstance` agora entrega a fábrica do host para CLSID_WICImagingFactory e CLSID_WICImagingFactory2, e `CreateDecoderFromFilename` lê o arquivo inteiro pelo caminho do host e decodifica pelo stb_image como antes. Só o WIC mudou; memória, WriteWatch, esperas e travas intactos. contentVersion 01.000.142. Logs: console-reports/2026-11-01-title-1.41-start e -start-b.


## 01/11/2026 — título 1.43: espaçamento de linha no DirectWrite (motor 1.0.2)

A 1.42 passou do WIC (logo 1920x1080 e azimuth-mark.png 840x440 decodificados) e parou em "text: SetLineSpacing HRESULT=0x80004001". `IDWriteTextFormat`/`IDWriteTextLayout` ganham `SetLineSpacing`/`GetLineSpacing` (padrão, uniforme e proporcional, aplicados na altura e na baseline de cada linha), `SetIncrementalTabStop`/`GetIncrementalTabStop` e `SetTrimming`/`GetTrimming` (aceito, mas o texto não é cortado). Só o DirectWrite mudou. contentVersion 01.000.143. Logs: console-reports/2026-11-01-title-1.42-start.


## 01/11/2026 — título 1.44: ComputeGlyphOrigins no DirectWrite (motor 1.0.2)

A 1.43 passou do layout do texto e parou em "text: Draw HRESULT=0x80004001": o renderizador de texto do motor (engine+0x5a8d70) chama `IDWriteFactory4::ComputeGlyphOrigins` (slot 42: run, measuring mode, origem da baseline, transformação, origens). Os slots 42 e 41 (a variante sem measuring mode) agora devolvem a posição de cada glifo a partir da origem, somando os avanços (da direita para a esquerda em bidi ímpar) e aplicando os offsets do run. Só o DirectWrite mudou. contentVersion 01.000.144. Logs: console-reports/2026-11-01-title-1.43-start.


## 01/11/2026 — título 1.45: restante do DirectWrite do launcher 1.0.2

A 1.44 passou do desenho e parou em "text: DetermineMinWidth HRESULT=0x80004001" (layout, slot 63). Para não ir um método por rodada, a lista de nomes que o motor confere ("text: <método>", em .rdata perto de 0x814a78) foi cruzada com as vtables do host. Faltavam três, e agora os três têm implementação: `IDWriteTextLayout::DetermineMinWidth` (largura da palavra mais larga), `SetLocaleName` (slot 41) e `IDWriteFactory::CreateEllipsisTrimmingSign` (slot 20, um objeto inline que não desenha nada, já que o trimming não corta). Só o DirectWrite mudou. contentVersion 01.000.145. Logs: console-reports/2026-11-01-title-1.44-start.


## 01/11/2026 — título 1.46: GetSystemMetrics do cursor e TLS de DLL protegido

A 1.45 passou do DirectWrite e teve dois problemas. (1) A parada final: `GetSystemMetrics(13)` (SM_CXCURSOR, engine+0xe74ec) sem resposta; 13 e 14 agora devolvem 32. (2) Antes disso, SIGSEGV no host em `giveTlsBlock` ao carregar o d3d12core.dll (TLS índice 2): a escrita do bloco de TLS foi para a tabela de TLS de uma thread cuja página já não existia (0x200b46010, não mapeada). Agora toda saída de thread do motor, inclusive a parada, tira a thread da lista de TLS (`forgetThreadBlock`). Ao carregar uma DLL com TLS, o host confere se a página existe antes de escrever (write(2) para /dev/null) e, se não existir, pula a thread e registra o id dela no log. Caminhos quentes intactos: o custo é um lock por fim de thread e uma verificação por thread a cada DLL com TLS carregada. contentVersion 01.000.146. Logs: console-reports/2026-11-01-title-1.45-start.


## 01/11/2026 — título 1.47: shaders do launcher 1.0.2 pré-compilados

A 1.46 passou pelo DirectWrite, pelo cursor e pela criação do device D3D12 e do swap chain. O launcher parou em "the launcher's surface: its shader corner does not compile HRESULT=0x80004005" ("Azimuth hit a graphics error"). O D3DCompile do host só responde com shaders pré-compilados, e a tabela tinha só os do 0.9.7. O `host/tools/precompile_shaders.py` agora mantém as entradas da tabela (o exe 0.9.7 não está mais em disco), reconhece o motor pelo SHA-256 e acrescenta os shaders do 1.0.2 que dá para montar lendo o executável. São eles: a superfície do launcher (engine+0x62f510, três trechos concatenados, corner vs e picture ps) e o overlay (engine+0x5a12c0, place, paint, corner, halve e spread). O detile e o vs do 1.0.2 têm a mesma chave dos do 0.9.7. A tabela passa de 5 para 12 entradas, com as 5 antigas idênticas byte a byte. O 1.0.2 monta a maior parte do HLSL do jogo em tempo de execução, por isso o D3DCompile agora grava todo pedido sem resposta em <pasta do relatório>/shaders/<chave>.hlsl e .txt, e `precompile_shaders.py --dumps <pasta>` os compila. contentVersion 01.000.147. Logs: console-reports/2026-11-01-title-1.46-start.


## 01/11/2026 — título 1.48: shader do jogo e shader provisório

Com a 1.47 o launcher 1.0.2 abriu no PS5. As imagens do jogo só apareceram depois que `splash.jpg`, `tile.png` e `AppxManifest.xml` foram copiados do PC para data\titles\1A96545B\art\<hash>\, porque o host ainda não tem o codificador do WIC. Ao abrir o jogo, parou em "STOPPED shade shader ps HRESULT=0x80004005" (engine+0x2bb7ef): um pixel shader de 93 bytes vindo do jogo, que o host gravou em shaders/. Ele foi compilado com `--dumps` (a chave confere com o nome do arquivo). Os shaders de apresentação do 1.0.2 (`hdr_*`, `game`, `curved`, `page_hdr10`) são montados com valores calculados em tempo de execução e não dá para reconstruí-los lendo o executável. Por isso, um pixel shader sem entrada agora recebe um shader provisório que desenha preto (`placeholder ps_5_0`): o jogo continua e todos os que faltam são gravados numa única execução. Os blobs constantes de shader passaram de 16 para 256. contentVersion 01.000.148. Logs: console-reports/2026-11-01-title-1.47-launcher e -jogo.

## 01/11/2026 — título 1.49: VirtualProtect na imagem do motor

Com a 1.48 o jogo compilou os pipelines e parou ao iniciar com "VirtualProtect fora da memoria do VirtualAlloc: 0x14084cb20". Antes de carregar o módulo do jogo, o 1.0.2 (engine+0x6728c0) torna gravável um buffer de 0x800 bytes da própria .rdata (engine+0x84cb20). Nesse buffer ele zera e copia o template de TLS do jogo e depois devolve a proteção antiga. O VirtualProtect do host só conhecia a memória do VirtualAlloc. Agora, quando o endereço está na imagem do motor ou de uma DLL carregada, o host aplica a proteção com mprotect por bloco do kernel. Cada bloco mantém os direitos das seções com que foi mapeado e ganha os que foram pedidos. Um bloco só fica gravável e executável se já era. A proteção pedida para cada página fica guardada para responder a proteção antiga na chamada seguinte. O caminho do VirtualAlloc não mudou: o novo ramo só roda onde antes o host parava. contentVersion 01.000.149. Logs: console-reports/2026-11-01-title-1.48-jogo.

## 01/11/2026 — título 1.50: shaders de apresentação do jogo

Com a 1.49 o jogo iniciou e rodou a 60 fps, mas a tela ficou preta. Os três pixel shaders de apresentação do 1.0.2 (`plain`, `game` e `curved`) não tinham entrada pré-compilada e receberam o shader provisório, que desenha preto. O host gravou os três em shaders/. Eles foram compilados com `precompile_shaders.py --dumps`, e a tabela passou de 14 para 17 entradas. O código desses shaders começa com constantes calculadas a partir das configurações (`signal_low`, `signal_gain`, `signal_srgb`). Com outra configuração (HDR ligado, por exemplo), o código muda, a chave muda e volta o preto até o novo despejo ser compilado. contentVersion 01.000.150. Logs: console-reports/2026-11-01-title-1.49-jogo.

## 01/11/2026 — título 1.51: controle pelo Windows.Gaming.Input

Com a 1.50 o jogo apareceu na tela, mas não respondia ao controle. O motor 1.0.2 não carrega mais o XInput (a 0.9.7 carregava, e o host o implementa desde a 1.07). Agora ele lê os controles pelo WinRT `Windows.Gaming.Input.Gamepad`, que o host não registrava, e o log dele dizia "pads: Windows.Gaming.Input unavailable, no controller exposed: HRESULT=0x80040154". Só chegavam ao jogo as teclas que o host posta para o launcher (Cruz = Enter). Pela desmontagem (engine+0x5ca560 e +0x5ca140), o motor usa IGamepadStatics (add_GamepadAdded, add_GamepadRemoved e get_Gamepads), IVectorView (get_Size e GetAt), IGamepad::GetCurrentReading e put_Vibration, e o QueryInterface de IUnknown. O `RoGetActivationFactory` agora entrega esses objetos para "Windows.Gaming.Input.Gamepad". Eles ficam em win32/Xinput.cpp, sobre a mesma leitura do ScePad que o XInput usa, com as vtables em win32/gamepad_abi.c (novo, incluído no Makefile e no title/build.sh). A lista sempre tem o controle do PS5; desconectado, ele lê neutro. Mapeamento: Cruz = A, Círculo = B, Quadrado = X, Triângulo = Y, Options = Menu, touchpad = View, L1/R1 = ombros, L3/R3 = analógicos, L2/R2 = gatilhos (0 a 1), analógicos de -1 a 1 com Y para cima. A vibração vai para `scePadSetVibration`. Enquanto o jogo lê o controle, os botões deixam de ser postados como teclas, como já acontecia com o XInput. contentVersion 01.000.151. Logs: console-reports/2026-11-01-title-1.50-jogo.

## 02/11/2026 — resultado da 1.51: jogo perfeito no motor 1.0.2

O usuário confirmou que a 1.51 ficou perfeita: o jogo roda no motor Azimuth 1.0.2 com o controle do PS5 funcionando pelo `Windows.Gaming.Input`, em 4K, MSAA 4x e 60 FPS, sem engasgos. Documentação de estado atualizada (topo do title/README.md, seções 9 e 11 da base de conhecimento). Código enviado para o repositório privado https://github.com/GabrielSilla/Azimuth-PS5.

## 02/11/2026 — título 1.52: Playmuth

O título passa a se chamar **Playmuth**, com ícone próprio (anel com triângulo de "play"). O logo e o símbolo do Azimuth saíram do projeto: são marca da Almost There Games, e o usuário não tem autorização para usá-los. Sem o arquivo `title-mode`, o título agora roda o jogo (`host`). Como o eboot linka código GPL-3.0 (boilerplate e libc do ps5-payload-sdk), o projeto ganha um `LICENSE` GPL-3.0 e cada release pública leva o código-fonte. README do repositório público em release/README.md. contentVersion 01.000.152.

## 02/11/2026 — título 1.53: ícone e banner do Playmuth

Ícone gerado pelo usuário com o GPT ("P" ciano com linhas de velocidade, triângulo de play e agulha de bússola) convertido para 512 x 512 RGB em title/assets/playmuth-icon.png. O make_icon.py passa a conferir e copiar esse arquivo. Banner de 1280 x 640 em release/playmuth-banner.png, no README público. Host igual ao da 1.52. contentVersion 01.000.153.


## 02/11/2026 — repositório público

Criado https://github.com/GabrielSilla/Playmuth (público), só com `README.md` e `playmuth-banner.png` (cópias de `release/`) e o `LICENSE` GPL-3.0. As versões vão em Releases, cada uma com `Playmuth-<versão>.zip` e `Playmuth-<versão>-source.zip` (gerado com `git archive --prefix=Playmuth-<versão>-source/ HEAD -- . ":!console-reports"`). O código do dia a dia continua no repositório privado Azimuth-PS5. Ao mudar o `release/README.md`, copiar para o repositório público.

## 02/11/2026 — projeto inteiro no repositório público Playmuth

A pedido do usuário, todo o projeto passou para https://github.com/GabrielSilla/Playmuth (público). O repositório privado Azimuth-PS5 vai ser apagado pelo usuário. O README público (antes `release/README.md`) virou o `README.md` da raiz, com o banner em `playmuth-banner.png`. O README antigo do projeto foi para `docs/PROJETO.md`, com os links corrigidos. O histórico foi reescrito com o autor `gabriel.silla2@hotmail.com`, no lugar do e-mail corporativo. Os shaders do Azimuth compilados (`host/src/win32/CompiledShaders.inc`) foram publicados por decisão do usuário: são código da Almost There Games, e já iam dentro do eboot das releases.
