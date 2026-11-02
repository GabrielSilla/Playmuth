# Base de conhecimento — programar para o PS5 (Azimuth_PS5)

Consolidação, em 30/10/2026 (atualizada até o título 1.25), de tudo o que o projeto Azimuth_PS5 aprendeu ao levar o motor **Azimuth 0.9.7** (um executável Windows x64 sem modificação) e o **Forza Horizon 2** (Xbox One) a rodar como título nativo do PS5 (`FAKE78002`), com D3D12 traduzido para Vulkan e executado pelo RADV do console. O objetivo é servir de base para os próximos projetos com Vulkan no PS5.

Documentos relacionados:
- [`APIS_IMPLEMENTADAS.md`](APIS_IMPLEMENTADAS.md): a lista **completa** de funções do Windows e de métodos COM implementados, gerada do código por `host/tools/list_apis.py`.
- [`../HISTORICO_DE_ALTERACOES.md`](../HISTORICO_DE_ALTERACOES.md): a cronologia de cada versão, com o sintoma, a causa e a correção.
- [`../title/README.md`](../title/README.md): o diário de testes do título, versão por versão.
- [`fase3-d3d12.md`](fase3-d3d12.md) e [`radv-runtime-options.md`](radv-runtime-options.md): a análise do caminho D3D12 → Vulkan e das formas de carregar o RADV.

Convenção: **[PS5]** marca fatos medidos no console, que não aparecem no Linux nem no código; **[Win]** marca semânticas do Windows que o jogo exige e que não são óbvias.

---

## 1. O que funcionou (a arquitetura final)

```
eboot.bin (título nativo FAKE78002, ELF → SELF)
 └─ host (C++17, System V)
     ├─ loader PE (motor azimuth.exe em 0x140000000; DLLs do Windows de arquivo)
     ├─ TEB/PEB do Windows por thread, apontados pelo gs (sysarch importado)
     ├─ fronteira de ABI: entradas ms_abi (guest_abi.c, compilado p/ x86_64-pc-freebsd)
     ├─ camada Win32: 623 funções em 21 DLLs (exports.inc) + COM (DXGI, WASAPI, WIC, XAudio2)
     ├─ vulkan-1.dll virtual (763 comandos com adaptador ms_abi → System V)
     ├─ DXGI próprio (fábrica, adaptador, swap chain sobre o vkd3d-proton)
     ├─ alocador próprio (HostHeap) sobre memória direta
     └─ diagnóstico: anéis de chamadas, esperas longas, vigia de memória, tratador de falhas
 ├─ vkd3d-proton 3.0.1 (d3d12.dll + d3d12core.dll do Windows, carregados pelo host)
 └─ RADV (libvulkan.prx, Mesa 26.2.0, no pacote do título desde o 1.27) carregado com sceKernelLoadStartModule
```

Marcos alcançados no console:
- O motor original carregou, validou o SHA-256, desenhou o launcher em GDI e o apresentou no VideoOut (título 0.30).
- O vkd3d-proton criou o `ID3D12Device` sobre o RADV (0.23) e o motor criou o seu (0.28).
- O swap chain DXGI apresentou o primeiro quadro do jogo pelo WSI do RADV (0.96).
- O jogo mostrou logos e menu, com som pelo FMOD → XAudio2 → `sceAudioOut` (0.98–1.06), controle no jogo por XInput (1.07), exceções C++/SEH (1.00), ConcRT (1.10) e fibras (1.12).
- Situação no 1.15: o jogo sai do menu e começa a carregar o mundo aberto. Lá ele cai por uma condição de corrida, descrita na seção 11.

Princípios que se mostraram certos:
1. **Não modificar o binário.** Tudo é feito pela camada de sistema. Isso permite comparar com o comportamento no Windows.
2. **Nunca simular um objeto funcional que não existe.** Uma API sem implementação vira uma *parada de diagnóstico* com o nome da DLL e da função. Uma API que pode faltar no Windows responde como o Windows responde quando o recurso falta (`REGDB_E_CLASSNOTREG`, `ERROR_DEVICE_NOT_CONNECTED`, `E_NOTIMPL`, `APPMODEL_ERROR_NO_PACKAGE`...).
3. **Reproduzir no Linux antes de enviar.** O mesmo host compila para Linux (`azimuth-ps5-host-linux`), carrega o motor real e as DLLs (vkd3d-proton sobre o lavapipe) e roda verificações de regressão. Cada ida ao console custa minutos, e o Linux pega a maioria dos erros de lógica.
4. **Cada versão com um log que explique a próxima falha.** Quase toda versão acrescentou um diagnóstico além da correção.

---

## 2. Ambiente, build e ferramentas

| Item | Detalhe |
|---|---|
| SDK | ps5-payload-sdk v0.45 em `/opt/ps5-payload-sdk` (WSL Ubuntu), clang/lld 21.1.8 |
| Base do título | `mpereiraesaa/ps5-native-app-boilerplate` @ `9c0b994a` (startup, conversor ELF→SELF, assinador, `libc.prx` acompanhante) |
| Build | `title/build.sh` no WSL. Compila todo `host/src/**/*.cpp` (o `main.cpp` do host vira `azimuthHostMain`) e os `*_abi.c` com `-target x86_64-pc-freebsd12`. Linka `libScePad`, `libSceUserService`, `libSceAudioOut`, `libSceVideoOut`, `libSceSystemService`. Verifica o ELF (`check_runtime.py`: nada de syscall direta, `mmap`/`mprotect`/`sysarch` importados) e gera `dist/FAKE78002/eboot.bin` e o ZIP |
| Versão | A cada build: `title/main.cpp` (linha do `title.txt`), `title/package.py` e `contentVersion` em `title/sce_sys/param.json` |
| Linux | `make azimuth-ps5-host-linux test-files test-memory test-wic` em `host/`; verificação D3D12/XAudio2/SEH/ConcRT/fibras/FFmpeg com `AZIMUTH_DLL_DIR=pe:pe/data/bin ../../azimuth-ps5-host-linux --d3d12` (em `host/build/d3d12`) |
| Envio | Basta trocar o `eboot.bin` (Prospero Manager) |
| Logs do console | API HTTP do Prospero Manager na porta 7070 (passo a passo na seção 8.1). **O IP do console muda: perguntar sempre antes de baixar** |
| klog | klogsrv na porta 3232. Capturar **em laço com reconexão** durante todo teste: `while ...; do cat < /dev/tcp/<ip>/3232 >> klog.txt; sleep 2; done`. O klog mostra mortes de processo, falhas de GPU, `FMEM` por processo e o ciclo de vida do app |
| Desmontagem | `llvm-objdump` do motor e do jogo (`azimuth.dis`, `forza.dis`); scripts de apoio para achar a função pelo `.pdata` (`func.py`), o nome da classe pelo RTTI da vtable (`rtti.py`) e cadeias UTF-16/ASCII (`strs.py`) |

Armadilhas do ambiente de desenvolvimento:
- O Python do Windows escreve CRLF, e um `build.sh` com CRLF quebra (`$'bash\r'`). Gravar com `newline='\n'`.
- Barras invertidas dentro de heredoc do bash são mexidas. Escrever os scripts em arquivo.
- `$VAR` dentro de `wsl bash -lc '...'` chamado pelo Git Bash pode chegar vazio. Usar um script em arquivo.
- As ferramentas do Git Bash escondem o CRLF: o `grep`/`od` mostram LF, mas o Python vê `\r\n`. Normalizar antes de substituir texto.

Endereços para ler um crash:
- **[PS5]** O eboot carrega **0x400000 acima** do endereço do ELF: o `title.txt` imprime `main=0x1004001a0`, e no `llvm-pie.elf` o `main` está em `0x1000001a0`. Subtrair o deslocamento e desmontar o `title/build/llvm-pie.elf`.
- `0x1xxxxxxxx` (9 dígitos hexa) é o título; `0x8000xxxxx`, a libkernel/libc do sistema; `0x1000xxxxxx` (10 dígitos), a imagem do jogo em memória direta (VA preferido = endereço − `0x1000010000` + `0x140000000`); `0x140000000`, o motor; `0x18xxxxxxx`, as DLLs carregadas pelo host.

---

## 3. O runtime do título no PS5: regras medidas

### 3.1 Ciclo de vida do processo
| Regra | O que aconteceu | Solução |
|---|---|---|
| **[PS5] Nunca retornar do `main` nem chamar `exit`/`_exit`** | Depois do `exit(0)`, a libc executou instrução privilegiada (SIGILL, "App Crash"); `_exit` gera SIGSYS no título, e um tratador que chama `_exit` entra em laço ("FPU not available") | O `main` grava "Fim do titulo" e dorme; o usuário fecha pelo menu (`KillApp`/`ProcessTerm` limpos no klog) |
| Encerramento do host | O host 0.1 chamava `_exit` com o gs apontando para o TEB e **desligou o console** | Voltar ao quadro do host por `longjmp`, restaurar o gs e só então registrar e terminar |
| Destrutores TLS | 0/64 destrutores `thread_local` rodavam | `__cxa_thread_atexit_impl = 0` (fallback pthread do libc++abi) e uma rodada extra de destruição no `emutls.c` |
| Módulos com `atexit` | O RADV registra handlers de `atexit`; descarregá-lo antes do fim faz o `exit` cair | Nunca descarregar o RADV |

### 3.2 Memória — o tema que mais custou
| Fato **[PS5]** | Consequência e solução |
|---|---|
| **Memória flexível: 448 MiB.** Esgotar mata o título **sem sinal** | Todo bloco grande do host vem da **memória direta** (12 GB). A vigia mede `sceKernelAvailableFlexibleMemorySize` a cada 100 ms e registra variações de 8 MiB |
| **O `malloc` do título é pequeno e fixo.** Esgotado, `pthread_create` (que aloca por dentro) nunca retorna e tudo trava | O host não usa o `malloc` do título: `HostHeap` com arenas de 64 MiB em memória direta, distribuídas em 16 shards com trava própria, 68 classes de tamanho, blocos ≥ 1 MiB direto da memória direta com guarda de 64 KiB no fim. `operator new`, `HeapAlloc`, o `msvcrt` das DLLs e o bloco de FLS passam por ele |
| `fopen`/`ifstream`/`std::calloc` também usam o `malloc` do título | Arquivos lidos com `open`/`read` (o `fopen` do carregador falhou com o jogo já rodando). Pendente: o `fopen` do `msvcrt` do guest |
| Exceção C++ não atravessa um quadro `ms_abi` | `std::bad_alloc` dentro de uma chamada do guest vira `terminate`/`abort`. O host não deve lançar exceções através da fronteira |
| **Área de mapeamento do título: `0x1000000000`–`0xFC00000000`** | `sceKernelReserveVirtualRange` com `MAP_FIXED` fora dela devolveu EINVAL e **o console desligou** logo depois. Reservas fixas fora da área falham no host sem chamar o kernel; as do Xbox (`0x60000000000`, `0x800000000`) são realocadas para dentro, mantendo o alinhamento (até 4 GiB). Faixas de `MEM_ADDRESS_REQUIREMENTS` são cortadas à área |
| `mmap` anônimo sem endereço vem de uma **janela pequena** (acabou perto de `0x229000000`) | Toda reserva do `VirtualAlloc` passa a ser só de endereços (`sceKernelReserveVirtualRange`), com memória direta mapeada no commit |
| **Granularidade de proteção: 16 KiB** (em cada bloco vale a última proteção aplicada) | Seções de um PE que dividem um bloco recebem a união das proteções; o console aceita **um** bloco RWX por DLL. A reserva arredonda o mapeamento a 16 KiB, senão o corte do excesso desmapeia o fim dela |
| **A memória direta pode ser executável** | A imagem do jogo roda de memória direta |
| Commit no console é memória real e zerada (no Windows é só contabilidade) | Um jogo que superdimensiona commits esgota a memória direta |
| **Cada mutex, variável de condição ou rwlock do pthread é um objeto de um pool interno pequeno da libkernel ("SceKernelInternalMemory")** | Um `std::mutex` + `std::condition_variable` por `CRITICAL_SECTION` esgotou o pool no carregamento do mundo aberto: SIGABRT dentro da libkernel e travamento geral. **Nunca criar um objeto de sincronização do kernel por objeto do guest.** Usar o estado dentro da própria estrutura do Windows (atômicos) e um conjunto fixo de pares mutex/condição compartilhados por hash do endereço (seção 5.2) |
| O código da libkernel não pode ser lido | Ler os bytes antes de um `rip` dentro da libkernel derrubou o título |

### 3.3 Arquivos e sistema
| Fato **[PS5]** | Solução |
|---|---|
| `access(2)` responde errado | Testar leitura com `fopen`/`open` |
| `opendir` dá EPERM em pastas do pendrive | Plano B com `sceKernelGetdents` sobre o descritor da pasta (formato `dirent` do PS4/PS5) |
| `getrlimit(RLIMIT_NOFILE)` diz 13952, mas o `open` falha com EMFILE perto de **256**; contar descritores com `fcntl(F_GETFD)` dá 0 | Um handle de arquivo do Windows não é um descritor: cada `File` guarda caminho, flags de reabertura e posição, e os descritores ficam num **cache LRU de 96**; leitura e escrita com `pread`/`pwrite` |
| `statvfs` escreve uma estrutura maior que a dos cabeçalhos e **corrompe a pilha** | `GetDiskFreeSpaceExW` com valor fixo. Desconfiar de chamadas da libc que preenchem estruturas cujo layout mudou entre FreeBSD 11 e 12 |
| `dup` falha em silêncio | Guardar `shared_ptr` do objeto em vez de duplicar descritores |
| `kinfo_proc` com layout diferente | Não usar para achar processos |
| `cpuset_getaffinity` devolve errno 34 | Não depender de afinidade |
| O título lê `/mnt/usb1` normalmente | O pendrive foi o lugar do jogo e dos logs do motor até o 1.23 |
| **O pendrive entrega ~38 MB/s** em leituras sequenciais de 64 KiB (~1 ms por leitura, medido no 1.22) | O jogo foi feito para o disco do Xbox; a essa velocidade o carregamento perde a corrida com o script de estados do jogo (seção 11). Desde o 1.24 o jogo pode ficar no **SSD interno**, em `/data/PC/FH2` (primeiro lugar onde o host procura) |
| **O `/data` (SSD interno) diferencia maiúsculas e minúsculas**; o exFAT do pendrive e o Windows não | Abaixo de uma montagem em `/data`, o host resolve cada nome do caminho pela grafia real na pasta, com as listagens guardadas (1.24); as fontes também são procuradas ignorando a caixa (1.25). Cuidado ao renomear arquivos pelo Prospero: no 1.25 as fontes viraram `arial.tff` e o launcher não abria |
| O cache de `stat` e de leitura só vale para conteúdo que nada escreve | A pasta `Mount` do jogo: `stat` guardado por caminho (inclusive "não existe") e blocos de leitura de 128 KiB (até 256 MiB). Ajuda nas consultas repetidas, não em leituras sequenciais únicas |

### 3.4 Threads, agendamento e sinais
| Fato **[PS5]** | Solução |
|---|---|
| As threads nascem `SCHED_FIFO` prioridade 700 (número menor = mais prioridade): uma thread nova podia nunca rodar | `platform::shareCpu`: SCHED_RR para todas; thread principal do motor, laço do host, vigia e controle em 600 |
| Contexto de sinal: `ucontext+0x40`, `rip` em `+0xe0`, `rsp` em `+0xf8` (diferente do cabeçalho do SDK) | Usar o layout medido |
| SIGTRAP/SIGSYS não tratados matam em silêncio | Tratador para SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP, SIGABRT e SIGSYS numa pilha alternativa de 256 KiB **mapeada à parte** (`mmap`, fora do heap); marcar `inFault` primeiro; registro imediato por `write`, sem alocar; numa thread do host, estacionar em vez de `_exit` |
| Mortes sem sinal: memória flexível, falha de GPU (o sistema mata de forma assíncrona), `_exit` | Olhar o klog: `GPU_FAULT_*`, `exit_value`, `KillApp`, `FMEM`. **Antes de concluir que morreu**, conferir se o `host.txt` ainda cresce e se o klog registrou a saída do pid (no 1.12 o "fechou" era um travamento) |
| Amostrar threads por sinal (SIGUSR2) falhou duas vezes no console e quase nunca respondia | Substituído por anéis de últimas chamadas ao host (seção 8) |
| `clock_gettime` é uma chamada ao sistema (~930–950 ns) | Todo relógio do host pelo `rdtsc`: `QueryPerformanceCounter` (1.21) e, no 1.26, `GetTickCount`/`GetTickCount64` e o `Clock::now()` de todas as esperas, com a âncora reajustada ao `CLOCK_MONOTONIC` a cada segundo. O ritmo de quadros chama o QPC até 900 mil vezes por segundo; uma thread do motor chama `GetTickCount64` 140 mil vezes por segundo no mundo aberto |
| `write` + `fsync` por linha de log, na thread que loga, trava o jogo quando o streaming loga muito | Log por fila e thread própria, `fsync` a cada 500 ms (1.16). A fila é criada no primeiro uso e nunca destruída: o alocador loga antes do `main` (inicialização estática) e a thread de escrita roda até o fim do processo |
| Editar o contexto do sinal (`ucontext+0x40`) e voltar do tratador funciona | Usado no GPU probe 0.6 e no contorno do `StopTimeOfDay` (1.23) |

---

## 4. Vulkan e GPU no PS5 (RADV)

O que vale para qualquer projeto Vulkan no PS5:

1. **Carregar o RADV.** O `libvulkan.prx` (tirado do kit wine do Playden e, desde o 1.27, dentro do pacote do título em `radv/`) carrega com `sceKernelLoadStartModule`, depois de garantir as dependências pelo Sysmodule: `libSceAgcDriver` `0x80000080`, `libSceAgc` `0x80000094`, `libSceVideoOut` `0x80000022` (só descarregar o que o próprio título carregou). O mapeamento cru com entrada manual falhou; num *payload*, a primeira instrução do RADV nem executou. **GPU só num título nativo.**
2. **Os construtores (`INIT_ARRAY`) precisam rodar.** Sem eles, as tabelas de opcodes do ACO ficam zeradas e o compilador gera shaders com **todos os opcodes zero**. Na GPU isso vira `ILLEGAL_INST` → `GPU_FAULT_WAVEFRONT_ERROR_ASYNC`. Conferir a flag de construtores (`+0x17f3278` naquele build) e, se for zero, chamar a entrada (`+0x4ef0`). Os offsets valem para um build: conferir tamanho e SHA-256.
3. **Apagar o cache de shaders do RADV gerado antes da correção** (`/mnt/usb1/FAKE78002/radv-shader-cache`): ele reaproveita o shader quebrado.
4. **Manter o RADV carregado até o fim do processo** (handlers de `atexit`).
5. `RADV_DEBUG=metashaders,asm` derruba o RADV na libc ao imprimir; não usar.
6. `vkCmdFillBuffer` ≥ 4096 bytes numa GPU sem VRAM dedicada roda como **compute shader**. Um fill pequeno (CP DMA) passar não prova que shaders funcionam.
7. Os tamanhos dos heaps anunciados (8 GiB local, 4 GiB compartilhado) **não são o orçamento** do título. O tipo 3 (`DEVICE_LOCAL|HOST_VISIBLE|HOST_COHERENT`, heap 1) mapeia em `0x4000000000`.
8. Falhas de GPU aparecem **só no klog** (ondas, PC, `STATUS`, `TRAPSTS`), seguidas de coredump, reinício da GPU e morte do título.
9. Recursos detectados pelo vkd3d-proton: SM 6.8, DXR 1.1, `VK_EXT_descriptor_buffer`, `VK_EXT_mutable_descriptor_type`. `vkGetPhysicalDeviceImageFormatProperties2` devolvendo -11 é sondagem normal.

### Apresentação na tela
- **2D direto no VideoOut** (launcher em GDI): dois framebuffers de 16 MiB em memória direta, layout de tiles de 64 KiB (128×128, B8G8R8A8 SDR, `0x8000000000000000`), `clflush`, `sceVideoOutSubmitFlip` no vblank, `sceSystemServiceHideSplashScreen` na primeira vez. Receita do `PS5_Vulkan`.
- **Vulkan WSI do RADV**: `VK_KHR_display` com uma superfície de plano de tela no display "PS5 VideoOut" (3840×2160); `VK_EXT_headless_surface` como reserva. **Fechar a porta VideoOut própria antes**, senão a do RADV não abre. O swap chain de 2560×1440 apresentou no 0.96.

### Atravessar ABIs com Vulkan (para DLLs do Windows)
- O vkd3d-proton e o DXVK **não importam** `vulkan-1.dll`: usam `LoadLibrary` + `GetProcAddress("vkGetInstanceProcAddr")`. Um `vulkan-1.dll` virtual com um adaptador `ms_abi` por comando, gerado do `vulkan_core.h` (`host/tools/gen_vulkan_commands.py`), resolve isso.
- `vkGetInstanceProcAddr`/`vkGetDeviceProcAddr` devolvem o **adaptador** e guardam a função do driver.
- `VkAllocationCallbacks` do chamador vira nulo, porque os callbacks estão na convenção do Windows. Esconder `VK_EXT_debug_utils`, `VK_EXT_debug_report` e `VK_EXT_device_memory_report`, que passariam callbacks ao driver.
- Oferecer `VK_KHR_win32_surface` (o vkd3d-proton exige) e removê-la antes do `vkCreateInstance` do driver.

---

## 5. Emulação do Windows: o que o jogo exigiu

A lista completa está em [`APIS_IMPLEMENTADAS.md`](APIS_IMPLEMENTADAS.md). Aqui ficam as **semânticas não óbvias**, que custaram testes.

### 5.1 Loader, TEB e ABI
- O motor fica na base preferida `0x140000000`; o jogo foi relocado. TEB/PEB por thread, gs via `sysarch` importado (o SDK não deixa usar syscall direta).
- O **PEB precisa de `ProcessParameters`** (o UCRT lê os Flags ao iniciar threads); sem ele, acesso ao endereço 8.
- O alvo PS5 do SDK não aceita `ms_abi` em C++ diretamente: as entradas `ms_*` ficam em arquivos `.c` compilados com `-target x86_64-pc-freebsd12`. Funções `naked` com parâmetros nomeados quebram o `-Werror`: declará-las `(void)`.
- `exports.inc` é a tabela única: `EXPORT(dll, nome, retorno, (params), (args))` gera a declaração, a entrada `ms_` e o registro para `GetProcAddress`. Um nome que não está lá vira parada de diagnóstico.
- DLLs carregáveis de arquivo (lista `Loadable` em `Modules.cpp`): `d3d12`, `d3d12core`, `azimuth-tls`, `msvcr110`, `msvcp110`, `avutil-60`, `avcodec-62`, `sehtest`. As outras são bibliotecas do host (`xaudio2_9`, `xinput1_4`, `windowscodecs`, `combase`, `ws2_32`, `mfplat`...). As dependências de uma DLL carregam antes dela. `api-ms-win-core-*` → kernel32; `api-ms-win-crt-*` → msvcrt; `KernelBase` → kernel32.
- **[Win]** `GetModuleFileNameW` precisa devolver um caminho real: o motor calcula o hash das dependências pelo nome. Bibliotecas do host recebem `C:\Windows\System32\<nome>.dll`.
- **[Win]** `GetModuleHandleExW(FROM_ADDRESS)` com um endereço de uma biblioteca do host precisa devolver o handle dela (o motor repassa ao jogo o "módulo que contém" o handle).
- **[Win]** `RtlPcToFileHeader` precisa achar as DLLs, não só o motor (o `msvcr110` usa isso para lançar exceção C++).

### 5.2 Sincronização — onde estão os bugs sutis
- **[Win] `CRITICAL_SECTION.DebugInfo = -1`**: o jogo trata `DebugInfo == 0` como "não inicializada" e gira para sempre.
- **[Win] Qualquer thread pode chamar `LeaveCriticalSection`.** Um `pthread_mutex` recursivo recusa a liberação por outra thread (EPERM silencioso) e a seção fica presa.
- **[PS5]** Não usar um objeto do kernel por trava (pool interno, seção 3.2). Implementação final (1.14):
  - `CRITICAL_SECTION`: `OwningThread` tomado por compare-exchange, `RecursionCount`, número de esperando em `LockSemaphore`, `LockCount` 0/-1.
  - `SRWLOCK`: bit 0 = exclusivo, leitores contados de 2 em 2.
  - `CONDITION_VARIABLE`: contador de geração; um wake acorda todos do par, o que o Windows permite.
  - As três esperam em **64 pares mutex/condição compartilhados**, escolhidos pelo endereço.
- Toda espera bloqueante acorda a cada 100 ms para ver se a execução está parando; uma espera de 10 s gera relatório com o objeto, quem o criou, quem espera e, nas seções críticas, a thread dona.
- **Despertar direcionado (1.19).** Eventos, semáforos, mutexes, threads, timers e portas de conclusão compartilham uma trava, mas cada thread que espera tem **a própria condição** e registra os objetos pelos quais espera. Um sinal acorda só quem espera aquele objeto.
  - Antes, uma única condição para o processo inteiro acordava as ~100 threads em espera a cada sinal. Com ~130 mil `SetEvent`/`WakeByAddress` por segundo, os workers de job do jogo ficavam sem CPU, e o carregamento caía (seção 11).
  - É uma condição por **thread** (~200 objetos do kernel), não por objeto do jogo.
- `WaitOnAddress`/`WakeByAddress*` em 64 baldes por endereço (1.19). `InitOnce*` com callback na convenção do Windows, SList com trava global.
- **[Win] Portas de conclusão de E/S (1.16):** `CreateIoCompletionPort` com um arquivo liga o arquivo à porta. A conclusão de uma leitura ou escrita com `OVERLAPPED` vira um pacote na porta, com a chave, e a chamada responde `ERROR_IO_PENDING`. `SetFileCompletionNotificationModes` também. O Forza Horizon 2 não associa arquivos a portas (lê de forma síncrona numa thread de carregamento), mas o caminho ficou correto.
- Custos medidos no console (1.20): `EnterCriticalSection` 60–70 ns, `SetEvent` ~4 µs, `SwitchToThread` 3–8 µs, `QueryPerformanceCounter` ~930 ns antes do `rdtsc`. No mundo aberto (1.25): `GetTickCount64` 953 ns (syscall), `AcquireSRWLockExclusive` 469 ns com disputa, `ReleaseSRWLockExclusive` 140 ns, `WakeAllConditionVariable` 6,5 µs (trava do par + notify mesmo sem ninguém dormindo), `SetEvent` 13 µs.

### 5.3 Threads, pool, ConcRT e fibras
- **ConcRT do `msvcr110`** (PPL/`concurrency::task`): se **qualquer** função do Windows 7+ procurada por `GetProcAddress` faltar, lança `scheduler_resource_allocation_error`. Exigiu `Get/SetThreadGroupAffinity`, `GetCurrentProcessorNumberEx`, `GetLogicalProcessorInformationEx` (registros de núcleo, pacote, NUMA e grupo), `CreateRemoteThreadEx` com listas de atributos, as **13 funções de UMS presentes mas sem suporte** (`ERROR_NOT_SUPPORTED`), `combase!RoInitialize`, as funções de ETW (registram, nunca rastreiam) e `FreeLibraryWhenCallbackReturns` (chamada no callback que espera uma thread externa terminar).
- **Pool de threads**: esperas e timers (`CreateThreadpoolWait/Timer`, `Set*`, `WaitFor*Callbacks`, `Close*`, `IsThreadpoolTimerSet`) e as formas antigas (`RegisterWaitForSingleObject`, `UnregisterWait(Ex)`, fila de timers), cada objeto com uma thread do motor própria, criada por `CreateThread` com um ponto de entrada `ms_abi` do host. `TrySubmitThreadpoolCallback` roda numa thread própria.
- **[Win]** `RaiseException(0x406D1388)` é o nome de thread do Visual C++: deve retornar. `0x40010006`/`0x4001000A` (`OutputDebugString`) também.
- **Fibras** (sistema de jobs do jogo, via `kernelx` do motor):
  - Cada fibra tem uma pilha no HostHeap, com 256 KiB extras para os quadros do host.
  - A troca é em assembly (`fiberSwitch`): salva os registradores preservados do System V, o MXCSR e o controle do x87. Os preservados do Windows (rdi, rsi, xmm6–15) ficam salvos pela entrada `ms_abi` de `SwitchToFiber` na pilha de cada fibra.
  - O TEB acompanha a fibra: **`FiberData` em `gs:[0x20]`** (o jogo lê inline), StackBase/StackLimit e o bit `HasFiberData` em `+0x17ee`.
  - Os relatórios do host passam a ler só até o topo da pilha da fibra atual.
  - A rotina que retorna termina a thread, como no Windows.
  - Pendente: no Windows o FLS é por fibra; aqui é por thread. O jogo não usa FLS.
  - O relatório de parada lista cada fibra (rodando/suspensa, thread) e, para as suspensas, os endereços de código na pilha a partir de onde cederam a vez (1.18). Foi o que mostrou o "esperar job" do jogo (seção 11).
- **O "esperar job" do Forza Horizon 2** (`0x1413d9bb0`): enquanto o job não termina (status em `+0x34`), ele ajuda a executar jobs, chama `SwitchToThread` e, se a thread é a principal (índice 0 na tabela de 45 workers do sistema de jobs), **devolve a vez à fibra principal**, que continua desenhando quadros e avançando o script de estados. No Xbox o job termina quase na hora e essa cessão praticamente não acontece. Aqui, qualquer atraso dos jobs (workers famintos, disco lento) vira passos do script executados antes da hora.

### 5.4 Memória virtual do Windows
- `VirtualAlloc2`/placeholders (`MEM_RESERVE_PLACEHOLDER`, divisão e junção por `VirtualFree` com preserve/coalesce, **divisão em 4 KiB**, não 64 KiB), seções anônimas (`CreateFileMapping` sem arquivo → memória direta) e `MapViewOfFile3`/`UnmapViewOfFile2`.
- O motor reserva um **heap gráfico contíguo de 20 GiB** e reservas de até 6 TiB: só de endereços, com memória direta no commit.
- `MEM_WRITE_WATCH`: até a 1.28, `GetWriteWatch` devolvia todas as páginas. A 1.29 implementa rastreamento via proteção/SIGSEGV, integrado a commit/protect/decommit/free e reset concorrente. No PS5 marca conservadoramente até quatro páginas de 4 KiB por bloco físico de 16 KiB. Testes locais passam com blocos de 4/16 KiB; validação no console pendente. Detalhes e limites em `title/README.md`.
- **[Win]** `VirtualQuery` precisa responder em **qualquer** endereço: `MEM_FREE` até a próxima região, `MEM_IMAGE` nos módulos, `MEM_PRIVATE` no heap do host.
- O gerenciador de memória do jogo reserva regiões em endereços fixos do Xbox; se forem recusadas, os pools ficam com tamanho 0 e um laço de lista livre corrompe a memória. Solução: realocar (seção 3.2).

### 5.5 Arquivos
- Caminhos `E:\PC\FH2` e `E:\` mapeados para a pasta do motor (`/data/PC/FH2` no SSD interno, ou `/mnt/usbN/PC/FH2`) e a raiz acima dela; fora disso, recusa registrada.
- O motor emula o `CreateFile2` do Xbox por cima do `CreateFileW`; o carregador de arquivos do jogo (uma thread própria) faz, para cada leitura, `HeapAlloc`, `CreateEventW` e um `ReadFile` síncrono de ~64 KiB, em sequência.
- **[Win]** `ReadFile`/`WriteFile` com `OVERLAPPED` concluem na hora, mas **precisam sinalizar o evento da estrutura** (o handle com o bit 0 limpo).
- Mapeamento de arquivo só leitura, por cópia (`pread`) num bloco do host. O vkd3d-proton precisa disso para o `vkd3d-proton.cache` (sem ele, recompila tudo a cada abertura). Ele testa `CreateFileMappingA` contra `INVALID_HANDLE_VALUE`, não contra nulo.
- INI com UTF-16 e gravação por temporário + `rename`.

### 5.6 Áudio, entrada, imagem, rede, mídia
| Recurso | Implementação |
|---|---|
| **WASAPI** (`MMDeviceEnumerator`, `IAudioClient2`, render, relógio, volumes) | O motor embrulha o enumerador real para a interface de áudio do Xbox. Thread de saída com mixagem para 48 kHz estéreo → `sceAudioOutOpen(0xff, 0, 0, 256, 48000, S16 estéreo)` |
| **XAudio2 2.9** (o FMOD usa) | Motor do mixer numa thread criada por `CreateThread` (os callbacks do FMOD rodam nela), passos de 10 ms, vozes PCM/float sem efeitos |
| **SHAPE (áudio XMA do Xbox)** | O motor usa FFmpeg (`avutil-60`, `avcodec-62`, CRT Universal); o host mapeia `api-ms-win-crt-*` para o seu `msvcrt` (+69 funções UCRT) |
| **XInput** (`xinput1_4`) | O usuário 0 é o DualSense pelo ScePad; mapeamento Cruz=A, Círculo=B, Quadrado=X, Triângulo=Y, Options=Start, touchpad=Back, eixo Y invertido, vibração por `scePadSetVibration`. O launcher usa teclas: enquanto o motor lê o XInput, o host não envia teclas, senão cada aperto conta duas vezes |
| **WIC** (`windowscodecs`) | Fábrica, stream da memória, decodificador, quadro e conversor para BGRA, com `stb_image` 2.30 |
| **D3DCompile** | O motor compila cinco shaders fixos: pré-compilados no Windows com o `d3dcompiler_47` (`host/tools/precompile_shaders.py`) e devolvidos pelo SHA-256 da fonte |
| **Kinect** (`SpeechRecognizerFactory`) | O motor devolve `ERROR_DEVICE_NOT_CONNECTED`, que vira `COMException`, que o jogo captura. **Exige SEH real** |
| ws2_32, Media Foundation, COM | Respostas de "sem recurso": `WSAStartup` e conversões; Media Foundation sem decodificadores; `CoCreateInstance` só cria o enumerador de áudio (os outros CLSIDs são registrados e recusados com `REGDB_E_CLASSNOTREG`); `CoCreateFreeThreadedMarshaler` real |
| Pacote UWP | `GetCurrentPackageId`/`FullName`/`FamilyName` → `APPMODEL_ERROR_NO_PACKAGE` (15700) |

### 5.7 Exceções (SEH e C++)
Implementação Wine-like (`Exceptions.cpp` + assembly em `guest_abi.c`):
- `RaiseException` e `RtlUnwindEx` como entradas em assembly que capturam o `CONTEXT`.
- `RtlVirtualUnwind` sobre o `.pdata`/`UNWIND_INFO` (do motor, das DLLs e das tabelas de `RtlAddFunctionTable`) e despacho pelos manipuladores de linguagem.
- Desenrolamento com quadros colididos, `STATUS_UNWIND_CONSOLIDATE` (o `catch` do C++) e `longjmp`; trampolins `guestCallHandler`/`guestCallConsolidate` e `guestRestoreContext`.
- Verificação: `tests/seh` gera `sehtest.dll` com clang para o alvo MSVC (sobre o `msvcr110`): throw/catch, destrutores, rethrow, `catch(...)` e `__try/__except`. Passa 31/31, inclusive dentro de uma fibra.
- Antes disso, o relatório de exceção C++ (quem lançou, tipo pelo `ThrowInfo`, bytes do objeto) já identificava o `Platform::COMException`.

---

## 6. D3D12 → Vulkan: o que foi traduzido e por quem

O **D3D12 em si é traduzido pelo vkd3d-proton 3.0.1** (DLL do Windows, sem modificação), carregado pelo loader do host. O host fornece o resto:

| Peça | Implementação do host |
|---|---|
| `d3d12!D3D12CreateDevice` (ordinal 101 do motor) | Encaminha ao `d3d12.dll` do vkd3d-proton com o adaptador do DXGI do host |
| `d3d12!D3D12SerializeRootSignature` | Encaminha ao vkd3d-proton |
| `d3d12!D3D12GetDebugInterface` | `E_NOINTERFACE` |
| `dxgi!CreateDXGIFactory1/2` | Fábrica própria (`IDXGIFactory`…`6`): `EnumAdapters(1)`, `EnumAdapterByLuid`, `EnumAdapterByGpuPreference`, `CheckFeatureSupport`, `CreateSwapChain`, `CreateSwapChainForHwnd`, associação de janela |
| Adaptador (`IDXGIAdapter`…`4`) | Descreve o dispositivo Vulkan real (nome, vendor `0x1002`, memória): `GetDesc/1/2/3`, `QueryVideoMemoryInfo`, `CheckInterfaceSupport`; sem saídas |
| Swap chain (`IDXGISwapChain`…`4`) | Embrulha o `IDXGIVkSwapChain` criado pelo `IDXGIVkSwapChainFactory` da fila do vkd3d-proton (GUID `e7d6c3ca-23a0-4e08-9f2f-ea5231df6633`). A superfície vem do `IDXGIVkSurfaceFactory` do host (plano de tela do RADV). `Present/Present1`, `GetBuffer`, `ResizeBuffers(1)`, latência máxima, objeto de espera, espaço de cor, HDR |
| `d3dcompiler_47!D3DCompile` | Shaders pré-compilados (seção 5.6) |
| `vulkan-1.dll` | Virtual, 763 comandos (seção 4) |
| GUIDs | Conferidos contra os binários do DXVK e do vkd3d-proton |

Os métodos exatos de cada interface estão em [`APIS_IMPLEMENTADAS.md`](APIS_IMPLEMENTADAS.md#interfaces-com-implementadas-vtables-na-convenção-do-windows).

O que o vkd3d-proton precisou do host além disso:
- O `msvcrt` do mingw-w64, no layout do Windows (`long` de 32 bits, `wchar_t` de 16, `FILE*` opaco, **`printf` com o `va_list` do Windows**), com `stdout`/`stderr` indo para o `host.txt`.
- Alocações grandes fora do `malloc` do título (o `E_OUTOFMEMORY` do `D3D12CreateDevice` no 0.22).
- Mapeamento de arquivo para o cache de pipelines.
- Registro sem chaves e `D3DKMT*` sem suporte.

---

## 7. O que deu errado (e a lição)

| Erro | Lição |
|---|---|
| GPU num payload | Payload não executa o RADV; usar título nativo |
| Shaders com opcodes zerados | Rodar o `INIT_ARRAY` de bibliotecas carregadas à mão; apagar caches de disco gerados por um binário defeituoso |
| `exit`/`_exit` no título | Nunca sair; dormir e deixar o sistema fechar |
| Host no `malloc` do título | Alocador próprio sobre memória direta desde o início |
| Varredura de pilha além do topo (`onSample`, `onFault`, `engineCallers`, `hostMissingImport`): **quatro vezes** | Toda leitura de pilha para diagnóstico precisa de um limite conhecido (topo da thread **ou da fibra**). Threads do jogo têm pilhas de 32–128 KiB |
| Tratador de falhas que aloca ou recursa | Registrar com `write`, sem alocar; pilha alternativa fora do heap |
| Gravação de PNG no laço principal travou os despejos | Diagnóstico em thread própria, independente do laço |
| Ler nomes do anel de chamadas sem validar | Validar ponteiros antes de imprimir |
| `access`, `statvfs`, `dup`, `opendir`, `getrlimit` | No PS5, conferir cada chamada da libc com teste no console; preferir as APIs `sceKernel*` |
| Mutex por objeto do guest | Pool interno do kernel; estado na estrutura e esperas compartilhadas |
| Responder ao jogo com objetos "falsos" | Responder como o Windows sem o recurso, ou parar com diagnóstico |
| Teclas e XInput ao mesmo tempo | Uma única fonte de entrada por vez |
| Concluir "morreu" sem conferir | Ver se o log ainda cresce e se o klog registrou a saída |
| Laço de klog com uma conexão só | O klogsrv derruba a conexão; reconectar em laço |
| Uma condição global para todas as esperas | Thundering herd: com ~100 threads e ~130 mil sinais/s, os workers do jogo ficaram sem CPU. Acordar só quem espera o objeto |
| Atacar a queda no carregamento pela velocidade do log e do `stat` (1.16–1.17) | Ajudou o desempenho, mas não era a causa. Antes de corrigir, **localizar onde cada fibra/thread está** no momento da falha (1.18) |
| Cache de leitura só para abertura somente-leitura (1.21) | O motor abre os arquivos do jogo com outro acesso; e o jogo lê em sequência uma vez só, então o limite era a velocidade do pendrive, não o host. Medir antes (log de leituras lentas, 1.22) |
| Contornar uma falha do jogo passo a passo (`StopTimeOfDay`, 1.23) | O passo seguinte caiu pelo mesmo motivo. Contorno pontual não resolve uma corrida; atacar a causa (disco) |
| `frame_rate=60` com ritmo fixo e o jogo entregando ~30 | Câmera lenta proporcional (o jogo avança um passo fixo por quadro). O Forza Horizon 2 roda a 30 no Xbox One |
| Ler o driver e o vkd3d-proton da instalação do Playden (até o 1.26) | O Playden se atualizou sozinho (0.8.0), apagou `/data/homebrew/FAKE78001`, e o título passou a fechar na abertura com "no DXGI adapter". Tudo que o host carrega vai no pacote do próprio título (1.27) |
| Renomear arquivos à mão no console | Uma extensão digitada errado (`arial.tff`) derrubou o launcher; conferir com a listagem do Prospero |

---

## 8. Técnicas de diagnóstico que funcionaram

1. **Anel das últimas 64 chamadas ao host por thread** (`hostGateNamed` na entrada `ms_`), mantido durante toda a execução e listado em toda parada e na `MessageBoxW` do motor. Foi o que achou a causa da maioria dos travamentos.
2. **Despejo a cada 10 s** pela thread vigia: etapa do laço principal e anel de cada thread. Comparar dois despejos seguidos mostra quem progride.
3. **Relatório de espera longa** (10 s) com o objeto, o criador (thread e endereços), quem espera e, nas seções críticas, a thread dona.
4. **Relatório de giro**: `SwitchToThread`/`Sleep(0)` a 100 000 chamadas e a cada 10×, com os endereços.
5. **Vigia de memória** a cada 100 ms: flexível, blocos grandes do host e maior trecho livre da memória direta.
6. **Registro imediato de falha**: sinal, `rip`, endereço, `si_code`, trap, `err` (escrita/presente), registradores e 16 palavras em `[rdi]`, mais o backtrace aproximado dos endereços de código na pilha.
7. **Logs de primeira vez**: a primeira chamada de cada caminho novo (`FreeLibraryWhenCallbackReturns`, `GetDiskFreeSpaceExW`, `XInput`...) e as N primeiras ocorrências de cada evento raro.
8. **Ler o código do jogo pela classe**: o RTTI do MSVC fica logo antes da vtable, e um ponteiro de função dentro de uma vtable dá o nome da classe e o slot (ex.: `States::StopTimeOfDay`, `ForzaSimulation`). O `.pdata` dá início e fim de qualquer função.
9. **Linha de desempenho a cada 10 s** (1.16–1.20): quadros por segundo, tempo médio dentro do `Present`, `SyncInterval`, chamadas ao host por segundo e as threads que mais chamam; e o **tempo no host por função** (ms/s, chamadas/s, ns por chamada, pelo `rdtsc` na entrada e na saída de cada export) e a fração de trabalho no host por thread, sem as esperas.
10. **Fibras no relatório de parada** e **log de leituras lentas** (≥ 5 ms, com caminho, tamanho e acesso).
11. **Capturas de tela pelo próprio PS5:** botão Create; o arquivo fica em `/user/av_contents/photo/NPXS40087/FAKE78002/` em **JPEG XR** (`.jxr`, 3840×2160), que o Windows converte para PNG com o `BitmapDecoder` do WPF.
12. **Reproduzir no Linux** pelo `--d3d12`: carrega `msvcr110`, `sehtest`, FFmpeg e o vkd3d-proton, e agora testa XAudio2, seções críticas, ConcRT, pool de threads e fibras. Achou sozinho quatro faltas do ConcRT antes de ir ao console.


### 8.1 Como pegar os logs do console

**O IP do console pode mudar** (DHCP da rede local; já foi `192.168.15.7`). Antes de baixar qualquer coisa, **perguntar ao usuário o IP atual**. Se a API não responder (`curl` com código `000`), o mais provável é IP novo, console desligado ou Prospero Manager fechado. Nesse caso, perguntar de novo em vez de insistir.

O console precisa estar ligado com o **Prospero Manager** aberto. Ele expõe uma API HTTP na porta **7070**:

| Para | Chamada |
|---|---|
| Listar uma pasta | `GET http://<ip>:7070/api/ftp/list?path=<pasta>`: JSON com `entries` (`name`, `is_dir`, `size`, `mode`, `modified`), ou `{"error": ...}` |
| Baixar um arquivo | `GET http://<ip>:7070/api/ftp/download?path=<arquivo>` |

Arquivos que interessam:

| Arquivo | Conteúdo |
|---|---|
| `/user/data/azimuth-ps5/title.txt` | Versão do título, modo, endereço do `main`, pid e como o título terminou. É a primeira coisa a conferir: diz se o console rodou a versão certa |
| `/user/data/azimuth-ps5/host.txt` | Log do host: etapas do carregamento, threads criadas, falhas, despejos de estado e as linhas "Desempenho" e "Tempo no host" a cada 10 s |
| `/user/data/azimuth-ps5/host-anterior.txt` | O `host.txt` da execução anterior. Cada abertura do título renomeia o anterior, então **o usuário não deve reabrir o título antes do download** |
| `/user/data/azimuth-ps5/frame.png` | Último quadro capturado |
| `/data/PC/FH2/data/logs/azimuth.log` | Log do próprio motor Azimuth (`E:\PC\FH2\data\logs`). Com o jogo no pendrive, fica em `/mnt/usbN/PC/FH2/data/logs/azimuth.log` |
| `/user/av_contents/photo/NPXS40087/FAKE78002/` | Capturas de tela do botão Create (`.jxr`) |

(`/user/data/azimuth-ps5` e `/data/azimuth-ps5` são a mesma pasta.)

Exemplo (bash), guardando uma cópia por teste em `console-reports/<data>-title-<versão>-<assunto>/`:

```bash
IP=<perguntar ao usuário>
d=console-reports/2026-10-30-title-1.27-teste; mkdir -p $d
for f in title.txt host.txt host-anterior.txt; do
  curl -s -m 30 "http://$IP:7070/api/ftp/download?path=/user/data/azimuth-ps5/$f" -o $d/$f
done
curl -s -m 60 "http://$IP:7070/api/ftp/download?path=/data/PC/FH2/data/logs/azimuth.log" -o $d/azimuth.log
grep "Desempenho\|Tempo no host" $d/host.txt | tail -4
```

Para acompanhar um teste em andamento, baixar o `host.txt` de novo a cada 30 s. O host grava em lote, com `fsync` a cada 500 ms, então o arquivo cresce durante a execução. O log do kernel (klogsrv, porta 3232 no mesmo IP) é opcional e só vale para quedas sem sinal.
---

## 9. Linha do tempo resumida

| Faixa | Marco |
|---|---|
| Probe/host 0.1–0.7.1 | Loader PE, TEB/gs, Win32 básico; motor até `CreateDXGIFactory2` (224/256 imports) |
| Título 0.1–0.17 | Título nativo; RADV no título; compute shader roda (0.14); saída limpa (0.17) |
| 0.18–0.29 | `vulkan-1.dll` virtual, vkd3d-proton, DXGI próprio; `D3D12CreateDevice` do motor (0.28) |
| 0.30–0.34 | Launcher na TV pelo VideoOut; FLS fora do `malloc` do título |
| 0.35–0.55 | Controle no launcher; agendamento RR; memória flexível; HostHeap em memória direta; cache de pipelines |
| 0.56–0.83 | DLLs do motor (`msvcr110`, `xg_x`), placeholders, heap gráfico de 20 GiB, reservas do Xbox realocadas |
| 0.84–0.99 | O jogo roda; WASAPI, D3DCompile, swap chain (primeiro quadro, 0.96), WIC, XAudio2 |
| 1.00–1.06 | SEH; memória direta para tudo; FFmpeg/UCRT; cache de descritores → **menu do jogo** (1.06) |
| 1.07–1.15 | XInput, `GetDiskFreeSpaceExW`, ConcRT, pool de threads, fibras, travas sem objetos do kernel; **mundo aberto pela primeira vez** (1.15, sem cenário) |
| 1.16–1.20 | Portas de conclusão, log assíncrono, cache de `stat`, despertar direcionado → **mundo aberto com cenário completo** (1.19); medidores de desempenho |
| 1.21–1.25 | `QueryPerformanceCounter` por `rdtsc`, cache de leitura, contorno do `StopTimeOfDay`, **jogo no SSD interno** com nomes sem diferença de caixa: o jogo abre normalmente (1.25), ainda em câmera lenta |
| 1.26 | Primeira medição completa do mundo aberto (10–12 quadros/s); todo relógio do host por `rdtsc`; wake de condição sem dorminhoco sem trava |
| 1.27 | RADV e vkd3d-proton dentro do pacote do título; nenhum caminho de outro programa no host |
| 1.28–1.30 | **Fim da câmera lenta:** `GetWriteWatch` de verdade (antes devolvia "tudo modificado" e o motor reprocessava tudo) e despacho de SIGSEGV para o `AddVectoredExceptionHandler` do motor, que sela e reabre regiões → ~29 FPS em 2160p com limite 30 (1.30) |
| 1.31–1.38 | Engasgos curtos: proteção e WriteWatch em lotes, buffer de I/O de 256 KiB, handles em 64 partições, FPS em janelas de 100 ms, liberação de memória direta agrupada → **~59 FPS em 4K, MSAA 4x, 60** |
| 1.39 | Nome **Azimuth Launcher** e ícone novo |
| 1.40 | Commit de memória direta em pedaços: fim da queda após ~10 min por fragmentação |
| 1.41–1.51 | **Motor Azimuth 1.0.2:** 287 imports, WIC por `CoCreateInstance`, DirectWrite completo do launcher, TLS estático escrito pelo motor, shaders pré-compilados do 1.0.2 com despejo dos que faltam, `VirtualProtect` na imagem do motor, controle pelo `Windows.Gaming.Input` → **jogo funcionando perfeitamente no 1.0.2 (1.51)** |

---

## 10. Checklist para o próximo projeto Vulkan no PS5

1. Começar como **título nativo** (base pública + SDK), com o `main` que nunca retorna e um `title.txt` gravado com `fsync`.
2. Usar um **alocador próprio sobre memória direta**. Não usar o `malloc` do título para nada grande ou frequente, nem `fopen`/`ifstream` depois da partida.
3. Carregar o RADV com `sceKernelLoadStartModule`, dependências pelo Sysmodule, **conferir e rodar o `INIT_ARRAY`**, nunca descarregar, e apagar o cache de shaders ao trocar o driver.
4. Validar primeiro escritas pela CP, depois um **compute shader** (fill ≥ 4096 bytes), com a fence lida por polling e o klog capturado.
5. Apresentar pelo WSI do RADV (`VK_KHR_display`, plano "PS5 VideoOut") ou direto pelo VideoOut (tiles de 64 KiB).
6. Respeitar a área `0x1000000000`–`0xFC00000000`, a granularidade de 16 KiB e o limite de ~256 descritores.
7. Nunca criar um objeto de sincronização do kernel por objeto da aplicação.
8. Prioridades: SCHED_RR para tudo, e prioridade maior (número menor) para quem desenha e para o laço principal.
9. Tratador de falhas desde o primeiro dia (pilha alternativa mapeada, sem alocar, com `write`), mais uma thread vigia que mede memória e despeja estado.
10. Manter uma versão Linux do mesmo código com testes de regressão; capturar o klog em todo teste no console.
11. Esperas do Windows com despertar direcionado desde o início (uma condição por thread que espera, não uma global).
12. Dados no SSD interno (`/data`), com resolução de maiúsculas/minúsculas; o pendrive é lento demais para streaming de jogo.
13. **Nenhuma dependência de outro programa instalado.** Driver, DLLs e o que mais o host carrega vão dentro do pacote do título (o 1.26 parou de abrir quando o Playden se atualizou para a 0.8.0 e mudou de lugar o `libvulkan.prx` que o host lia da instalação dele). Conferir os binários por SHA-256 no empacotamento.
14. Medir o custo de cada chamada ao host cedo: jogos de console fazem milhões por segundo, e ~1 µs por chamada já consome núcleos inteiros.

---

## 11. Estado atual (título 1.51)

**O jogo roda perfeitamente** no PS5 com o motor Azimuth 1.0.2 (título 1.51, "Azimuth Launcher"): 4K, MSAA 4x, 60 FPS, sem engasgos, com o controle do PS5 pelo `Windows.Gaming.Input`. Confirmado pelo usuário em 02/11/2026. O 0.9.7 continua aceito pelo host.

Como se chegou aqui, em resumo:
- **Câmera lenta (até 1.27, 10–12 FPS):** a causa era o `GetWriteWatch` devolvendo todas as páginas como modificadas, o que fazia o motor reprocessar a memória inteira a cada quadro. Relógio, travas e alocador eram custos menores (1.26). O rastreamento real (1.29) exigiu o despacho do VEH do motor (1.30).
- **Engasgos curtos (1.31–1.38):** reduzidos atacando o custo de `mprotect`, faltas de página, handles e liberação de memória, sempre medindo com FPS em janelas de 100 ms (as médias de 10 s escondiam as quedas).
- **Queda após ~10 min (1.39):** fragmentação da memória direta. Corrigida na 1.40.
- **Motor 1.0.2 (1.41–1.51):** imports novos, DirectWrite, WIC, shaders e controle (subseções abaixo).

Pontos de atenção:
- Shaders de apresentação do 1.0.2 dependem das configurações (HDR etc.). Uma configuração nova pode gerar uma chave nova e tela preta: compilar o despejo de `shaders/` com `host/tools/precompile_shaders.py --dumps` (1.50).
- Fragmentação: acompanhar "Commit em N pedacos" e "memoria direta em uso" em sessões longas (1.40).
- O host ainda não tem o codificador WIC: a arte do jogo no launcher foi copiada do PC (1.48).

A queda no carregamento (1.12–1.23), em resumo:
- O carregamento roda numa fibra da thread principal; a inicialização do `ForzaSimulation` espera um job e, enquanto ele não termina, devolve a vez à fibra principal, que avança o script de estados (`stateflow.zip`).
- Passos como `States::StopTimeOfDay` (`0x14093ca20`) e outro em `0x141981bb7` usam objetos que a simulação só cria no fim (`+0x480`, o objeto da hora do dia). Se o script chega antes, o jogo cai com um ponteiro nulo.
- Duas causas fizeram os jobs demorarem: o despertar de todas as threads a cada sinal (corrigido no 1.19, quando o cenário passou a carregar) e o pendrive a ~38 MB/s (resolvido levando o jogo para o SSD interno no 1.24/1.25). O contorno do 1.23 (pular a marcação do `StopTimeOfDay` com o objeto nulo) continua no host, mas sozinho não bastava.

### 11.1 Relatório de quadros do mundo aberto (título 1.25, SSD interno)

Medido em 2026-10-30 (`console-reports/2026-10-30-title-1.25-mundo/host.txt`), pelas linhas "Desempenho" e "Tempo no host" que o host grava a cada 10 s. Configuração: `render height 1440`, `30 fps`, ritmo fixo, v-sync, anti-aliasing desligado.

**Quadros por segundo ao longo da execução:**

| Momento | Quadros/s | ms por `Present` | Chamadas ao host/s | Observação |
|---|---|---|---|---|
| Partida (0 s) | 0,0 | — | 284 mil | Jobs de inicialização em `WaitOnAddress` |
| Launcher/menu (+10 s) | 26,7 | 0,01 | 2,46 milhões | Thread 344 com 1,5 milhão/s de `WaitForSingleObject` |
| Menu (+20 s, +30 s) | 30,1 / 30,0 | 0,01 | 117–303 mil | Na meta; carregando (`ReadFile` 159 mil/s na 564) |
| Mundo aberto (+2 min a +5 min, 20 amostras) | **9,6–12,2** (média ~10,5) | 0,01–0,03 | 2,73–3,26 milhões | Câmera lenta: ~1/3 da velocidade |

Amostra típica do mundo aberto:
```
Desempenho: 10.0 quadros/s, 0.01 ms por Present (SyncInterval 1, flags 0x0); 2949297 chamadas ao host/s;
  thread 400: 1284852/s  thread 540: 649093/s  thread 564: 186383/s  thread 344: 155493/s
  thread 380: 130344/s   thread 392: 103841/s  thread 552: 97287/s   thread 544: 64083/s
Tempo no host: WaitForSingleObject 35495 ms/s (4401/s, 8,1 ms) SleepConditionVariableSRW 9125 ms/s (27256/s, 335 us)
  Sleep 3988 ms/s  WaitForMultipleObjects 2995 ms/s  WaitOnAddress 2118 ms/s (3619/s, 585 us)
  AcquireSRWLockExclusive 174 ms/s (362360/s, 480 ns)  GetTickCount64 137 ms/s (143919/s, 952 ns)
  ReleaseSRWLockExclusive 53 ms/s (375798/s, 140 ns)  WakeAllConditionVariable 51 ms/s (7851/s, 6,5 us)
  SetEvent 44 ms/s (3256/s, 13,4 us)
  | fração de trabalho no host por thread: 400 34%, 540 5%, 476/480/472/552 4%, 544 2-3%, demais ≤ 1%
```

**Leitura:**
- O `Present` leva 0,01–0,03 ms: o quadro não espera vídeo nem a fila de apresentação. O limite está antes, na CPU ou na GPU sem bloquear o `Present`.
- As esperas longas (`WaitForSingleObject` 35 s/s somando todas as threads) são threads ociosas; o que custa é o **trabalho** no host, e ele se concentra numa thread.
- **Thread 400** (criada pelo motor, rotina `engine+0x1e2940`, a partida genérica de `std::thread`): ~1,28 milhão de chamadas/s, **34% do tempo dentro do host**, sem nunca dormir. O anel mostra o laço `GetCurrentThreadId → AcquireSRWLockExclusive → ReleaseSRWLockExclusive → GetTickCount64 ×2 → HeapAlloc → HeapFree → Acquire/Release ×2 → memset ×3`, com `WakeAllConditionVariable` (que no motor é o `std::condition_variable::notify_all` do CRT estático, `0x1401dc73d`/`0x1401dc7a6`).
- **Thread 540** (mesma rotina): a parceira, ~650 mil chamadas/s, dorme em `SleepConditionVariableSRW` (~335 µs por espera) e libera memória (`HeapFree`, `_aligned_free`). Disputa a mesma trava SRW da 400, daí os 469 ns do `Acquire`.
- Dos 34% da thread 400 no host, ~137 ms/s eram `GetTickCount64` (a syscall de relógio), ~170 ms/s a trava SRW disputada e ~50 ms/s o wake sem ninguém dormindo.
- O jogo (thread principal 344 e trabalhadoras) passa ≤ 1% no host.

**Dependências (1.27):** o driver RADV (`radv/libvulkan.prx`) e o vkd3d-proton (`dll/d3d12.dll`, `dll/d3d12core.dll`) vão dentro do pacote `FAKE78002`, conferidos por SHA-256 no `package.py` a partir de `title/vendor/`. O host não lê nada de outro programa.

**O que o 1.26 atacou:** todo relógio do host pelo `rdtsc` (elimina os ~950 ns do `GetTickCount64` e as 2–3 syscalls de relógio de cada espera) e o wake de variável de condição sem dorminhoco sem trava nem notify. Teto teórico se a thread 400 for o caminho crítico: ~10 → ~15 quadros/s.

**Próximo passo:**
- Medir o 1.27 (que traz o 1.26) do mesmo jeito (mundo aberto, 30–60 s) e comparar a fração da thread 400 e os quadros/s.
- Se não chegar a 30, testar `render_height=720` no `azimuth.ini` sem novo build: se os quadros/s subirem muito, o gargalo é a GPU (1440p pelo vkd3d-proton no RADV); se não, é a CPU do próprio motor (a thread 400 em código do guest, 66% do tempo dela).
- Ainda falta medir o tempo de CPU por thread (não só o tempo no host) para saber quais threads estão saturadas.

Pendências conhecidas: `fopen` do `msvcrt` do guest ainda usa o `malloc` do título; o FLS é por thread, não por fibra; as quedas sem sinal precisam do klog para serem explicadas.


### Memória selada e VEH (1.30)

GetWriteWatch real permite ao motor selar regiões com PAGE_READONLY. Para aceitar escritas posteriores, o motor depende de seu callback AddVectoredExceptionHandler, que identifica a região, desfaz a selagem e pede retomada. O registro era um stub até a 1.29; a 1.30 despacha violações de acesso do sinal ao guest. Não liberar indiscriminadamente páginas somente leitura no handler de tracking. Diagnósticos fatais não devem dereferenciar RDI nem varrer pilha sem uma leitura segura. Ver title/README.md e o teste VectoredTest.cpp.


### WriteWatch em lote (1.33)

Preparar uma cópia não pode tornar gravável uma página selada pelo motor. Só preparar páginas committed com permissão de escrita nos metadados; manter o VEH para as demais. Guardas de blocos sempre em ordem de endereço, liberadas antes de qualquer acesso a buffers do guest. Reset reprotege antes de limpar dirty; após liberar a guarda, uma escrita já precisa pertencer à próxima época. Faixas recusadas pelo kernel retornam ao caminho por bloco. Etapas medidas são inclusivas e não devem ser somadas como CPU exclusiva. Ver testes WriteWatchTest.cpp/VectoredTest.cpp e title/README.md.


### Selagem e reabertura em lote (1.34)

O agrupamento da 1.33 não cobria commit/VirtualProtect de regiões rastreadas: esse caminho ainda chamava watchProtect por bloco. A 1.34 envia descritores contíguos para watchProtectRange, usando as mesmas guardas/fallback de Batch. A proteção efetiva depende da máscara dirty e writable; permissões iguais do Windows não bastam para agrupar blocos se o tracking pede proteções diferentes. Nunca incluir lacunas sem backing. Testar selagem/reabertura e preservação de histórico em regiões maiores que um lote, com blocos de 4/16 KiB. Sem captura da 1.33 até esta alteração; não há prova de que esse caminho explique cada engasgada.


### Escritas CRT e avisos repetidos (1.35)

A preparação usada pelo I/O também pode atender destinos das funções CRT importadas, sem tocar em páginas seladas. A 1.35 usa limiar de 16 KiB e guarda a ordem de memmove na biblioteca; reset posterior à preparação permanece rastreável pelo fault normal. Não há otimização para stores inline ou implementações internas das DLLs. Conferir `Escritas CRT rastreadas` para saber se o caminho é usado no jogo; `preparo` passa a agregar CRT e I/O. O aviso XMA exato de timestamps representa >90% das linhas da captura 1.34 e é compactado com primeira ocorrência e contagens preservadas; demais mensagens não são filtradas. Reduzir esse log não prova nem resolve um problema de decodificação. Testar no console antes de atribuir ganho.


### Handles, sinalização e correlação de pausas (1.36)

Histórico da 1.35 mostrou zero uso de memcpy/memmove/memset rastreados nas 25 linhas preservadas, enquanto o filtro XMA foi usado. Não atribuir melhora à ponte CRT sem contadores. A 1.36 divide a tabela em 64 travas e mantém shared_ptr após close; liberar objetos fora da trava é necessário para destrutores reentrantes. Modificação de estado e notify dos waitables continuam sob waitLock, com uma aquisição por sinalização; não mover a verificação de consumo para fora dela. Wait-all não pode consumir objetos parcialmente.

`Disputa de handles` mede apenas contenção das partições, não bloqueio do objeto. `Memoria lenta` tem início/fim monotônico produzidos na API, embora a linha seja escrita depois pelo watchdog. Buffer de 64 slots tem contagem de descartes, sem alocação/log no produtor (VirtualProtect pode estar no VEH). `Quadro lento` amostra intervalos >50 ms, no máximo uma linha/s por cadeia, e inclui durações do Present anterior/atual; o atual vem depois do intervalo. Não inferir espera de GPU apenas pela duração do Present, nem causalidade apenas por sobreposição de threads.


### FPS de 100 ms e médias longas (1.37)

Um mínimo de 57 FPS no relatório da 1.36 era mínimo entre MÉDIAS de dez segundos, não mínimo instantâneo. O usuário viu 47 no overlay e solicitou resolução de 100 ms. A 1.37 mede contagem de Present sem TEST em thread independente e divide pela janela real, incluindo zero quadros enquanto a apresentação está parada. Grava também max intervalo e idade do último quadro. Se a thread de amostragem atrasa, não inventar janelas de 100 ms retroativas; informar duração real. Quantização de um frame em 100 ms corresponde a ~10 FPS, por isso consultar intervalos além da contagem. Não chamar essa métrica de taxa de scanout físico.

As capturas da 1.36 mostraram VirtualFree predominando entre operações de memória lentas registradas, muitas em threads 348/352/356, sem pares de sobreposição na própria thread 392 de apresentação. Eventos perdidos por buffer cheio impedem excluir outros custos. A tabela de handles teve esperas individuais curtas; isso não mede espera em waitLock nem tarefas do motor/GPU. Não concluir causalidade pelo encontro temporal de threads.


### Liberação agrupada e identidade física (1.38)

unback fazia unmap/release/reserva por bloco de 16 KiB; em MEM_RELEASE ainda removia a reserva inteira ao final. A 1.38 remove a reserva inteira uma vez e libera backing por faixas contíguas da mesma identidade de alocação. Não agrupar só pela sequência dos endereços físicos: duas alocações adjacentes têm identidades diferentes. Recommit cria nova identidade; migração para backing flexível e lacunas quebram faixas. Para decommit, preservar apenas a operação nos blocos nativos completos e recriar reservas.

DirectMemoryRelease.h é usado pelo backend e pelos testes com kernel simulado. Os testes comprovam limites/ownership/fallback, não aceitação ou desempenho do kernel real. Falha individual não pode apagar metadados de memória física ainda possuída nem retornar sucesso sobre uma liberação parcial; o backend interrompe o guest. A métrica Liberacao direta mede o novo caminho, não todas as syscalls de memória do processo. FPS 100 ms mantido; buffer de eventos passou a 1.024 slots, com contagem de perdas preservada.

### Fragmentação da memória direta (1.40)

Pedir **um** trecho físico contíguo para cada commit funciona por minutos e depois falha. No mundo aberto, o jogo aloca e libera gigabytes a cada poucos segundos, e o maior trecho livre caiu de 12 GB para 3 MiB em ~10 minutos com o uso total estável. Um `VirtualAlloc` que falha num jogo de console vira escrita em ponteiro nulo, porque o jogo nunca confere. A regra:
- Tentar o trecho contíguo e, se falhar, montar a faixa com vários pedaços físicos, cada um com a sua identidade para a liberação.
- Medir o **total em uso** (`sceKernelDirectMemoryQuery` com `FIND_NEXT`), não só o maior trecho livre (`sceKernelAvailableDirectMemorySize`). Só o total separa fragmentação de vazamento.

### Motor 1.0.2: TLS estático escrito pelo próprio motor (1.41)

O Azimuth 1.0.2 não depende do loader para dar TLS estático aos módulos do jogo. Ele carrega `azimuth-tls-N.dll` só para ganhar um índice de TLS, escreve o template do módulo do jogo no template dessa DLL e copia o resultado para o bloco do slot em **cada thread viva**. As threads são listadas pelo Toolhelp, o TEB vem de `NtQueryInformationThread(ThreadBasicInformation)` e o bloco é `TEB+0x58[índice]`, com `SuspendThread`/`GetThreadContext`/`ResumeThread` em volta. Lições:
- O host precisa saber o TEB de qualquer thread pelo id, não só o da thread atual.
- Uma thread que ainda não tem bloco no slot (ponteiro nulo) é pulada pelo motor e recebe depois o template já atualizado, porque o loader copia o template vivo da imagem.
- Para descobrir o que um import novo faz, procure as chamadas pela IAT (`FF 15` com o deslocamento até o slot), ache a função pela `.pdata` e leia as strings que ela usa. Desmonte com `objdump -b binary -m i386:x86-64 --adjust-vma=0xc00`, já que o objdump do WSL não lê PE.


### Motor 1.0.2: WIC por CoCreateInstance (1.42)

O 1.0.2 não chama `WICCreateImagingFactory_Proxy`: usa `CoCreateInstance(CLSID_WICImagingFactory2 {317d06e8-5f24-433d-bdf7-79ce68d8abc2}, IID_IWICImagingFactory)` (helper em engine+0xe9270) e lê imagens de arquivo com `CreateDecoderFromFilename` (engine+0xea4c0), depois GetFrame, GetSize, CreateFormatConverter, Initialize e CopyPixels (engine+0xea190). Também usa `CreateStream` + `InitializeFromMemory` + `CreateDecoderFromStream` (engine+0xea5a0) e, para gravar, `InitializeFromFilename` + codificador (engine+0x846a0, 0x85220). Se a fábrica falhar, o motor mostra "An Azimuth file is missing or was blocked" e chama TerminateProcess(1). Cada versão do motor tem seu próprio `data\bin`; trocar só o exe não basta.
