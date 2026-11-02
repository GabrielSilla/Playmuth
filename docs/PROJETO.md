# Azimuth_PS5 — port nativo do motor Azimuth para o PS5

Histórico consolidado das alterações, testes e pendências: [HISTORICO_DE_ALTERACOES.md](../HISTORICO_DE_ALTERACOES.md), atualizado em 28/10/2026. Inclui a análise dos relatórios do título 0.10.

Base de conhecimento para programar no PS5 (o que funcionou, o que deu errado, regras do runtime, RADV/Vulkan, D3D12 → Vulkan e emulação do Windows): [docs/BASE_DE_CONHECIMENTO_PS5.md](BASE_DE_CONHECIMENTO_PS5.md). Lista completa das funções e interfaces COM implementadas, gerada do código: [docs/APIS_IMPLEMENTADAS.md](APIS_IMPLEMENTADAS.md) (`python3 host/tools/list_apis.py`).

Objetivo: rodar o motor original **Azimuth 0.9.7** (`azimuth.exe`, SHA-256 `5d3ce30b440dab2604fe42ff7cd129394f0b98418e22b2a44270f548199bc6a9`, mantido **inalterado**) diretamente no PS5, sem Wine, para executar o Forza Horizon 2. O host nativo carrega o PE do motor com um loader próprio e atende as chamadas que o motor faz ao Windows com as bibliotecas do PS5. É a mesma arquitetura do port para Xbox (`../AzimuthUwp`), com outra camada de sistema por baixo.

**Marco (28/10/2026): a GPU do PS5 executa compute shaders pelo RADV no título, e o título 0.17 roda o teste completo e fecha sem erro.** Isso exigiu rodar os construtores do RADV, apagar o cache de shaders gerado sem eles e não retornar do `main`. Próximo passo da fase 3: a fábrica DXGI e o vkd3d-proton no host.

**Estado (27/10/2026): fases 1 e 2 validadas no PS5 até o bloqueio de GPU; fase 3 em andamento.**
- O motor original roda no console com threads, arquivos e INI do host.
- No Linux, o host 0.7 levou o motor até a janela do launcher (USER32 e GDI portados, quadro gravado em PNG).
- O motor parou ao pedir a fábrica DXGI, que pertence à fase 3.
- O GPU probe 0.8 mostrou que o RADV mapeia, mas não executa dentro do payload. A decisão é seguir como título.
- **Título 0.2: smoke validado no PS5**, com seis verificações e saída normal. Construtores, exceções/unwind, TLS e destrutor passaram; reservas em `0x140000000` e `0x1000000000` ficaram nos endereços pedidos. Próximo teste: RADV no contexto de título. Ver [`title/`](../title/README.md).
- **Título 0.3: mapeamento do RADV validado no PS5.** O Sysmodule informou as três dependências já carregadas; o RADV foi mapeado, a parada planejada funcionou e o processo terminou. Próximo teste: executar a entrada do RADV (`gpu-stop=modulo`).
- **Título 0.4 preparado para diagnóstico:** a chamada da entrada no 0.3 falhou, mas o relatório não trouxe sinal/RIP. O 0.4 corrige a obtenção do descritor em C e preserva os registradores para gravá-los depois da recuperação. Repetir com `gpu-stop=modulo`.
- **Título 0.4 testado; 0.5 preparado:** a falha é SIGSEGV por leitura de `0x70`, em RIP `0x800041aeb`, fora do RADV. O 0.5 testa o carregamento/inicialização normal pelo runtime do título e acrescenta identificação do módulo do RIP; a causa ainda não está confirmada.
- **Título 0.5: RADV carregado e ICD validado no PS5.** O carregamento normal retornou entrada 0; os exports foram resolvidos, a interface ICD negociou versão 5 e o descarregamento retornou 0. Próximo teste: `gpu-stop=instancia` para criar a instância e enumerar o dispositivo.
- **Título 0.5: instância Vulkan e GPU enumerada no PS5.** Instância criada com retorno 0; um dispositivo "PlayStation 5 GPU (RADV NAVI21)", Vulkan 1.4.354, enumerado. Destruição e descarregamento sem erro. Próximo teste: remover `gpu-stop` para criar o dispositivo lógico e conferir comandos na GPU.
- **Teste completo do 0.5:** dispositivo lógico e buffer criados, mas o título fechou com erro após o registro anterior ao envio. Nenhuma execução de comandos foi confirmada. O título 0.6 separa submissão/espera no log, verifica retornos, captura SIGSYS e reforça o watchdog; aguarda novo teste completo.
- **Teste completo do 0.6:** `vkQueueSubmit` retornou 0 em 0,05 ms; o relatório parou dentro de `vkWaitForFences`, sem retorno nem heartbeat. Relatórios em `console-reports/2026-10-28-title-0.6-gpu-submit/`. O título 0.7 consulta `vkGetFenceStatus` com prazo de 2 s entre retornos, exige confirmação de início da thread do watchdog e evita espera/limpeza de recursos pendentes. Ainda falta confirmar a conclusão do comando e as 1024 palavras no PS5.
- **Teste completo do 0.7:** watchdog iniciou; envio retornou 0 e a primeira consulta à fence retornou `VK_NOT_READY`. O relatório terminou antes da próxima consulta. Evidência em `console-reports/2026-10-28-title-0.7-gpu-submit/`. O título 0.8 testa a pausa antes do RADV, separa submissão vazia de preenchimento e registra a primeira pausa entre consultas. A causa do fechamento e a execução do preenchimento ainda não foram confirmadas.
- **Teste completo do 0.8:** pausa inicial e envio sem command buffers passaram. Após o envio com preenchimento, a fence estava pendente e a primeira pausa retornou 0; não houve conclusão nem comparação do buffer. Relatórios em `console-reports/2026-10-28-title-0.8-gpu-submit/`. O título 0.9 testa um command buffer vazio antes do preenchimento e adiciona a barreira de memória de transferência para leitura pela CPU. A causa do fechamento continua em investigação.
- **Teste completo do 0.9:** command buffer vazio passou; preenchimento com barreira continuou sem conclusão registrada, usando memória tipo 2 (heap 0, flags 0x6). Evidência em `console-reports/2026-10-28-title-0.9-gpu-submit/`. O título 0.10 prioriza memória local da GPU também visível/coerente para a CPU (tipo 3, heap 1, flags 0x7 nesse dispositivo), com fallback. Essa hipótese ainda precisa de validação no console.
- **Teste completo do 0.10:** tipo 3 escolhido (heap 1, flags 0x7); mesma falha do 0.9 (fence `VK_NOT_READY`, relatório termina depois da primeira pausa, sem sinal). O tipo de memória não é a causa principal. No RADV, um preenchimento de 4096 bytes numa GPU integrada é um dispatch de compute shader, não CP DMA; o próximo teste separa escrita pela CP (preenchimento < 4096, `vkCmdUpdateBuffer`) de execução de shader. Evidência em `console-reports/2026-10-28-title-0.10-gpu-submit/`.
- **Título 0.11 preparado (GPU probe 0.17):** três escritas pela GPU em sequência: `vkCmdUpdateBuffer` de 64 bytes (CP WRITE_DATA), preenchimento de 256 bytes (CP DMA) e de 4096 bytes (compute). A primeira que não concluir encerra o teste. O polling registra cada consulta com tempo, e o watchdog não é mais reiniciado no laço. Passou no Linux; aguarda teste no PS5.
- **Teste completo do 0.11: a GPU executou comandos no PS5.** `vkCmdUpdateBuffer` (CP WRITE_DATA) e o preenchimento de 256 bytes (CP DMA) concluíram em cerca de 5,7 ms, com todas as palavras corretas. O preenchimento de 4096 bytes (compute shader) ficou pendente por 351 ms em 283 consultas, todas retornando normalmente, e o título foi encerrado entre consultas, sem sinal. O bloqueio está na execução de shaders. Como o mesmo RADV roda shaders no Playden, a suspeita é o ambiente do título (janela de endereços de 32 bits dos shaders, inicialização ou variáveis de ambiente do Playden). O próximo passo é capturar o klog da falha. Evidência em `console-reports/2026-10-28-title-0.11-gpu-submit/`.
- **Klog do 0.11:** o sistema encerrou o título com `GPU_FAULT_WAVEFRONT_ERROR_ASYNC`. Quatro ondas do compute shader pararam em **`ILLEGAL_INST` no PC `0x20004001C`** (byte `0x1C` de um código na janela de 32 bits dos shaders). Não é falha de memória nem travamento: a GPU encontrou uma instrução que não aceita. O próximo passo é ver o código do shader com `RADV_DEBUG=metashaders,asm`. Evidência em `console-reports/2026-10-28-title-0.11-gpu-klog/`.
- **Título 0.12 preparado (GPU probe 0.18):** define `RADV_DEBUG=metashaders,asm` antes da instância (o shader desmontado vai para o `gpu-stderr.txt`) e copia a janela dos shaders (`0x200000000` + 4 MiB) para `gpu-shader-window.bin` depois de gravar o preenchimento por compute, para decodificar a instrução no PC `0x20004001C`. Passou no Linux; aguarda teste no PS5.
- **Teste do 0.12:** o `RADV_DEBUG` chegou ao RADV, mas o dump do shader derrubou o RADV na CPU (SIGSEGV lendo o endereço 0 em `libSceLibcInternal+0x4953`) ao gravar o preenchimento por compute, antes da cópia da janela e de qualquer envio. O probe se recuperou e o título terminou normalmente. **Título 0.13 (GPU probe 0.19):** sem `RADV_DEBUG`, só com a cópia da janela dos shaders para `gpu-shader-window.bin`. Evidência em `console-reports/2026-10-28-title-0.12-gpu-shader/`.
- **Teste do 0.13 — causa encontrada:** a cópia da memória mostrou que o shader de preenchimento no PC `0x20004001C` tem **todos os opcodes zerados** (`s_endpgm` virou `s_nop 0`, o store virou opcode 0), com operandos corretos. No `libvulkan.prx`, o `init` registrado pelo sistema é `xor eax,eax; ret` (`DT_INIT` = 0), e o `INIT_ARRAY` só roda pela entrada `+0x4ef0`. O construtor `[4]` copia as tabelas de opcodes do ACO para a `.bss`. O `sceKernelLoadStartModule` do título não rodava os construtores, e o Playden roda. **Título 0.14 (GPU probe 0.20):** confere a flag dos construtores e chama a entrada do RADV quando eles não rodaram. Evidência em `console-reports/2026-10-28-title-0.13-gpu-shader/`.
- **Teste do 0.14:** os construtores rodaram (flag 0 → 1, tabelas de opcodes preenchidas), mas o compute falhou igual, com a mesma janela byte a byte. O RADV grava um cache de shaders em `/app0/radv-shader-cache`, e a entrada de `radv_builtin_shaders` criada no primeiro teste completo (27/10) guarda o shader com opcodes zerados, que é reaproveitado. Próximo passo: apagar esse cache e repetir o 0.14. Evidência, incluindo a entrada do cache, em `console-reports/2026-10-28-title-0.14-gpu-init/`.
- **0.14 com o cache apagado: [PASSOU].** As cinco etapas concluíram; o compute shader (4096 bytes) escreveu as 1024 palavras corretas, e o shader copiado é o preenchimento correto para gfx1030, terminando em `s_endpgm`. Na saída, depois de `codigo=0`, um handler de `atexit` falhou (provavelmente do RADV, que o probe descarrega antes do `exit`), e o título terminou com SIGILL. Evidência em `console-reports/2026-10-28-title-0.14-gpu-cache-limpo/`.
- **Título 0.15 (GPU probe 0.21):** o RADV iniciado fica carregado até o fim do processo. No PS5, o teste passou de novo, e `exit(0)` chegou ao kernel (os handlers de `atexit` passaram). Depois disso, a thread principal ainda recebeu um sinal, o tratador do probe chamou `_exit(3)` e o título terminou com SIGILL. Evidência em `console-reports/2026-10-28-title-0.15-gpu-exit/`.
- **Título 0.16 (GPU probe 0.22):** mantém o relatório aberto até o fim do processo. Um sinal recebido depois do fim do probe é registrado (sinal, `rip`, módulo, pid de origem e pilha) e devolvido ao comportamento padrão, sem `_exit(3)`. No PS5, o teste passou e o probe não recebeu sinal na saída. O SIGILL ocorre dentro do `exit()` da `libSceLibcInternal`, logo depois de `exit(0)`, provavelmente uma instrução privilegiada executada porque a saída voltou ao processo. Evidência em `console-reports/2026-10-28-title-0.16-gpu-exit/`.
- **Smoke com o 0.16:** a saída também falha sem RADV e sem GPU (SIGSYS e depois SIGILL depois de `exit(0)`). A base do título documenta que não se deve retornar do `main` nem chamar `exit`, e o nosso `main` retorna. O erro do fim do teste é, portanto, da saída do título, e não da GPU. Evidência em `console-reports/2026-10-28-title-0.16-smoke-exit/`.
- **Título 0.17: teste completo com GPU sem erro.** o `main` não retorna mais. Ao terminar, o título grava o resultado, avisa por notificação que pode ser fechado e espera ser fechado pelo menu do PS5. No PS5, em modo `gpu`, o probe passou (1024 palavras corretas pelo compute shader) e o título fechou pelo menu sem "App Crash"; o klog mostra só o encerramento normal pelo sistema. O smoke também fechou limpo. Evidência em `console-reports/2026-10-28-title-0.17-gpu-close/` e `2026-10-28-title-0.17-smoke-close/`.
- **Fase 3, passo 1 (host): `vulkan-1.dll` para módulos do Windows.** 763 adaptadores Windows x64 → System V gerados dos cabeçalhos Vulkan 1.4.354, com o RADV carregado pelas regras validadas no título. `make test-vulkan` passou no Linux (61 verificações, GPU escrevendo 4096 bytes pelos adaptadores). Título 0.18 inclui esse código; ainda não exercitado no console. Detalhes em [docs/fase3-d3d12.md](fase3-d3d12.md).
- **Fase 3, passo 2 (host): vkd3d-proton carregado pelo host.** O host carrega o `d3d12.dll`/`d3d12core.dll` do Playden com o próprio loader (TLS, `DllMain`, exports), atende o `msvcrt` do mingw e oferece um DXGI próprio (fábrica e adaptador com a GPU Vulkan real). No Linux, `host --d3d12` passou: dispositivo D3D12, fila, lista executada e fence sinalizada. Título 0.19 tem o modo `title-mode=d3d12`; No PS5 (0.19), o `d3d12.dll` mapeou, mas o `DllMain` caiu ao gravar na `.data` (`d3d12+0x11fb`, endereço `0x35e4f10b4`); o título 0.20 mostrou a causa: o PS5 protege memória em blocos de 16 KiB, e o `.data` ficou sem escrita. O título 0.21 aplica a proteção por bloco (1 bloco RWX por DLL, aceito pelo console): o `d3d12.dll` passou. O `d3d12core.dll` falhou com `std::bad_alloc` ao ler 5,9 MB; o 0.22 lê arquivos grandes com `mmap` e o `d3d12core.dll` carregou, com o RADV, a fábrica DXGI e a instância Vulkan do vkd3d-proton criados no PS5. O `D3D12CreateDevice` falhou com `E_OUTOFMEMORY`; o 0.23 manda alocações grandes ou recusadas do `msvcrt` para o `mmap` e loga erros do driver. **Resultado no PS5 (0.23): [PASSOU]** — o vkd3d-proton criou o dispositivo D3D12, a fila, a lista e a fence sobre o RADV do PS5 e executou a lista. O título 0.24 liga o `D3D12CreateDevice` do motor a esse vkd3d-proton: falta rodar o motor (`title-mode=host`) no PS5. **Resultado no PS5 (0.28): o motor original chegou ao launcher, criou o dispositivo D3D12 pelo vkd3d-proton e o host gravou `frame.png` com a tela do launcher (pasta do jogo detectada).** Próxima parada: `D3D12SerializeRootSignature` (título 0.29 a encaminha ao vkd3d-proton). **No 0.29 o motor passou da assinatura de raiz e seguiu rodando, mas a TV ficou preta: o host não apresentava nada. O título 0.30 apresentou o launcher na TV (VideoOut) e parou logo depois em `RtlCaptureStackBackTrace`; o 0.31 responde esse e mais 22 imports simples.**

## Por que nativo e não Playden

O Playden (Wine/prospero-win, vkd3d-proton 3.0.1, RADV) levou o motor bem longe no PS5. Ele criou o D3D12 na GPU do PS5, abriu o launcher e iniciou o jogo. Depois falhou:

```text
CRASHED exception 0xc0000005 at game+0x125efda writing 0x1007e80004 on thread 36
```

O destino `0x10_07E8_0004` fica na faixa dos 64 GiB, provavelmente na memória de título que o motor reserva em endereços fixos. Um host nativo controla o espaço de endereçamento, a memória e o TEB, que são as partes que o Wine faz de forma genérica. Os detalhes estão em [docs/evidencias.md](evidencias.md).

## Arquitetura

O motor e o jogo são código x86-64 (o PS5 é Zen 2) e rodam direto. O host fornece:

| Camada | Papel | Origem |
|---|---|---|
| Loader de PE | mapear e relocar `azimuth.exe`, unwind, TLS estático no slot 0 | porta de `PeImage`/`UwpImage` do AzimuthUwp |
| TEB/PEB | TEB falso por thread no `gs` (`gs:[0x30]`, `gs:[0x58]`) | novo (no Windows o sistema fornece) |
| ABI | adaptadores e callbacks em `__attribute__((ms_abi))`, em arquivos próprios compilados para `x86_64-pc-freebsd` (o alvo `x86_64-sie-ps5` do SDK só aceita a convenção C) e que chamam só código do projeto | novo; ver `probe/msabi.c` |
| Win32 | kernel32 (threads, sincronização, arquivos, memória), janelas virtuais, GDI de software para o launcher, INI | porta de `GuestThreads`, `GuestWindows`, `GuestGdi`, `IniProfile` |
| Gráficos | D3D12 do motor → vkd3d-proton (biblioteca) → Vulkan RADV do PS5 (PS5_Vulkan/PS5_Mesa) | vkd3d-proton e RADV, que já funcionaram no PS5 pelo Playden |
| Plataforma | ScePad (XInput), SceAudioOut, VideoOut, armazenamento | novo |
| Dependências do jogo | runtime VC11 (`msvcr110`/`msvcp110`, PE do Windows) pelo loader próprio; Winsock → sockets; Media Foundation/áudio | novo |

## Fases

0. **Viabilidade (validada para CPU):** o payload [`probe/`](../probe/) testa no console os fatos de plataforma de que tudo depende:
   - memória executável;
   - chamadas `ms_abi` a partir de código gerado;
   - TEB pelo `gs` em várias threads;
   - reservas em endereços fixos, incluindo a faixa dos 64 GiB;
   - memória disponível para os ~5 GiB de título.

   O teste do Vulkan/RADV segue na fase 3, agora no contexto de título.
1. **Loader:** mapear o motor, aplicar o TLS e chamar o entry point original até a primeira parada de diagnóstico (import sem adaptação), como na 0.2.1 do Xbox.
2. **Sistema:** threads, sincronização, arquivos e INI, para o launcher rodar com o GDI de software sendo apresentado na tela.
3. **Gráficos:** D3D12 pelo vkd3d-proton, com cache de pipelines persistente pelo Vulkan.
4. **Início do jogo:** dependências de desktop e memória de título, passando do ponto em que o Playden parou.
5. **Entrada, áudio e apresentação.**

## Política

- O motor original nunca é alterado. O jogo e o firmware não fazem parte do projeto.
- Nada de sucesso artificial: chamada sem implementação para com diagnóstico (DLL/função e endereço), nunca devolve "ok" falso.
- Cada fase registra o que foi testado no console e o que só foi testado no PC.

## Compilar o probe

Precisa do [ps5-payload-sdk](https://github.com/ps5-payload-dev/sdk) (Linux/WSL) e de um ELF loader no console.

```bash
cd probe
make                                  # payload do PS5 (PS5_PAYLOAD_SDK definido)
make send PS5_HOST=<ip-do-ps5>        # envia ao ELF loader (porta 9021)
make linux && ./azimuth-ps5-probe-linux   # mesma lógica no Linux x86-64, para conferir o probe
```

O resultado vai para a saída do payload, para `/data/azimuth-ps5/probe.txt` e para uma notificação na tela.

Toolchain usado (26/10/2026): Ubuntu 26.04 no WSL, clang/lld/llvm 21.1.8 e ps5-payload-sdk v0.45 em `/opt/ps5-payload-sdk`. O zip tinha o SHA-256 `5fac6409…291a2c`, igual ao publicado na release.

## Resultados

| Data | Onde | Resultado |
|---|---|---|
| 26/10/2026 | Linux x86-64 (WSL, 12 CPUs) | 6/6: RW→RX, `ms_abi` (91), TEB pelo `gs` em 4 threads sem divergência, reservas em `0x140000000` e `0x1000000000`, 6 GiB comprometidos |
| 26/10/2026 | PS5 (compilação) | `azimuth-ps5-probe.elf` gerado; conversão `ms_abi`→System V conferida na desmontagem de `msabi.o` |
| 26/10/2026 | PS5, probe 0.1 pelo ELF loader (16 CPUs online) | **6/6**: RW→RX (RWX direto também aceito), `ms_abi` (91), TEB pelo `gs` em 4 threads sem divergência, reservas em `0x140000000` e `0x1000000000`, 6 GiB comprometidos. O teste de memória levou 68,9 s e **travou a interface do console** durante esse tempo: o payload roda dentro de um processo do sistema e tirou memória do próprio sistema. Relatório em `console-reports/2026-10-26-probe-0.1/probe.txt`. |
| 26/10/2026 | probe 0.2 | teste de memória só roda se existir `/data/azimuth-ps5/memory-test`. O orçamento de memória real deve ser medido como título, com o `param.json` do bloco, e não como payload. |

## Fase 1 — loader do motor (`host/`)

O host mapeia o `azimuth.exe` inalterado e o valida antes:
- SHA-256 calculado pelo próprio host;
- layout do 0.9.7;
- load config, com as mesmas regras do AzimuthUwp.

Em seguida ele monta TEB e PEB do Windows para a thread do motor, pelo `gs`, com o TLS estático no slot 0. Depois roda os callbacks TLS e chama o **entry point original**. Ainda nenhuma API do Windows está implementada: os 256 imports são paradas de diagnóstico, que registram a função e o endereço do motor que a chamou.

Partes do código:
- `PeImage`: copiado sem mudanças do AzimuthUwp;
- `Image`: o mapeador, sobre `mmap`/`mprotect`, sem página gravável e executável ao mesmo tempo;
- `Teb`: TEB e PEB do Windows;
- `guest_abi.c`: a fronteira `ms_abi`, compilada para FreeBSD;
- `Sha256`: o hash.

O host procura o motor em `/data/azimuth-ps5/azimuth.exe` ou em `/mnt/usbN/PC/FH2/azimuth.exe`. O relatório vai para `/data/azimuth-ps5/host.txt`.

| Data | Onde | Resultado |
|---|---|---|
| 26/10/2026 | Linux x86-64 (WSL) | Motor mapeado na base preferida `0x140000000`; callbacks TLS 0 e 1 executados; entry point original `engine+0x1dc6ec` executou até o primeiro import: `KERNEL32.dll!GetSystemTimeAsFileTime`, chamado de `engine+0x1dd3e9` (inicialização do cookie de segurança do CRT) |
| 26/10/2026 | PS5, host 0.1 (enviado e executado pelo Prospero Manager) | **o console desligou.** O 0.1 registrava, notificava e encerrava o processo (`_exit`) de dentro da chamada do motor, com o `gs` ainda no TEB, e usava uma pilha própria (`pthread_attr_setstack`). Nada disso tinha sido testado pelo probe |
| 26/10/2026 | host 0.2 | Mudanças: uma parada só registra onde ocorreu e volta (`longjmp`) para a thread do host; o `gs` original é restaurado antes de qualquer log, notificação ou chamada ao sistema; a thread usa a pilha do sistema; o processo termina com retorno normal do `main`, como o probe; cada linha do `host.txt` vai ao disco (`fsync`) antes do passo seguinte. Também é possível rodar por etapas: um arquivo `host-stop` com `mapa`, `thread`, `gs` ou `callbacks` para a execução depois dessa etapa. No Linux, todas as etapas e a execução completa chegaram ao mesmo `GetSystemTimeAsFileTime` |
| 26/10/2026 | PS5, host 0.2 (executado pelo usuário, sem `host-stop`) | **Fase 1 concluída no console.** O host encontrou o motor em `/mnt/usb1/PC/FH2/azimuth.exe` e validou SHA-256, layout e load config. Mapeou o motor na base preferida `0x140000000`. O `gs` original da thread era `0x0`, e o TEB ficou em `0x2000f4000`. O template TLS foi copiado e os 2 callbacks TLS rodaram. O entry point original executou até `KERNEL32.dll!GetSystemTimeAsFileTime` (`engine+0x1dd3e9`), como no Linux. O host terminou normalmente e o console seguiu ligado. Relatório em `console-reports/2026-10-26-host-0.2/host.txt` |

## Fase 2 — APIs do Windows (`host/src/win32`)

`win32/exports.inc` lista, numa linha cada, as funções do Windows que o host implementa. A partir dela, o `guest_abi.c`, compilado para FreeBSD, gera:
- um ponto de entrada na convenção do Windows (`ms_abi`) para cada função, que repassa a chamada a `win_<nome>`;
- a tabela que o resolvedor de imports e o `GetProcAddress` usam.

As implementações `win_<nome>` são C++, compiladas no alvo do PS5, e declaradas a partir da mesma linha: se a assinatura divergir, o código não compila. O objeto FreeBSD continua sem chamar nenhuma biblioteca do sistema (`nm -u`: só `win_*` e `hostMissingImport`).

| Arquivo | Conteúdo |
|---|---|
| `Kernel32.cpp` | Hora (`FILETIME`, contador de 10 MHz); IDs, último erro e `IsProcessorFeaturePresent` (Zen 2), lidos no TEB. Seções críticas sobre mutex recursivo; SRW sobre rwlock criado no primeiro uso. FLS e TLS dinâmico no TEB. Heap com cabeçalho de 16 bytes. Startup info, linha de comando e ambiente de processo GUI. Code pages 1252/437/UTF-8. Módulos: o motor é a única imagem, e cada DLL implementada pelo host tem um handle próprio para o `GetProcAddress` |
| `Locale.cpp` | Tipo de caractere, maiúsculas e minúsculas (latim, grego, cirílico), comparação ordinal e locale en-US (0x409) |
| `Exceptions.cpp` | `RtlPcToFileHeader`; `RaiseException` ainda só de diagnóstico: registra o tipo e a mensagem de uma exceção C++ e para |

As paradas de diagnóstico também registram os endereços do motor encontrados na pilha, como um backtrace aproximado.

| Data | Onde | Resultado |
|---|---|---|
| 26/10/2026 | Linux, host 0.3 | 76 de 256 imports implementados. O CRT do motor inicializou por completo, e o motor está no próprio código, montando caminhos. Parada em `KERNEL32.dll!GetFullPathNameW` (`engine+0x1db785`; pilha: `engine+0x5f836`, `+0x5fd0d`, `+0x560f`, …). O CRT procurou `GetTempPath2W` e as DLLs `api-ms-win-core-synch`/`fibers`/`localization`; essas buscas são opcionais e ele segue sem elas |
| 26/10/2026 | PS5, host 0.3 (executado pelo usuário) | Igual ao Linux, linha por linha. 76 imports implementados; o CRT inicializou no console, com os adaptadores fazendo chamadas reais ao sistema com o `gs` no TEB. A execução parou em `GetFullPathNameW`, com o mesmo backtrace, e o console seguiu ligado. Relatório em `console-reports/2026-10-26-host-0.3/host.txt` |

### Camada de arquivos (host 0.4)

| Arquivo | Conteúdo |
|---|---|
| `Handles.cpp` | Tabela única de handles (múltiplos de 4, como no Windows) e a conversão de `errno` para os erros do Win32 |
| `Files.cpp` | Caminhos do Windows resolvidos contra o diretório atual (`E:\PC\FH2`) e servidos pelas unidades montadas: `E:\PC\FH2` é a pasta do motor e `E:\` a raiz do disco que a contém (`/mnt/usbN` no PS5). Caminhos fora delas são recusados e registrados. `CreateFileW` (inclusive pastas e apagar ao fechar), leitura e escrita (`OVERLAPPED` concluída na hora), posição, tamanho, informações e renomeação por handle, caminho final, busca com curingas do Windows, criar e apagar, mover, copiar e espaço livre. Falhas de arquivo ficam no log (até 60) |
| `Profile.cpp` | INI do Windows: `IniProfile` do AzimuthUwp, com a mesma semântica, sobre E/S POSIX e UTF-16, gravando por arquivo temporário e `rename` |

| Data | Onde | Resultado |
|---|---|---|
| 26/10/2026 | Linux, host 0.4 | 106 de 256 imports. O motor resolveu os caminhos dele, aceitou a política de DLLs `2560`, conferiu e criou `E:\PC\FH2\data\logs\azimuth.log` e tentou a `dbghelp.dll`, na mesma sequência do Xbox. Parada em `KERNEL32.dll!WakeAllConditionVariable` (`engine+0x165f3`): começo das threads e da sincronização |
| 26/10/2026 | PS5, host 0.4 (executado pelo usuário) | Igual ao Linux. Unidades `E:\PC\FH2` e `E:` sobre `/mnt/usb1`; o `azimuth.log` do SSD foi encontrado. Parada em `WakeAllConditionVariable` |

### Threads e sincronização (host 0.5)

| Arquivo | Conteúdo |
|---|---|
| `GuestThreads.cpp` | `runAsGuest` torna uma thread do host uma thread do motor: TEB próprio pelo `gs`, cópia do template de TLS no slot 0, callbacks TLS (`DLL_PROCESS_ATTACH` na principal, `DLL_THREAD_ATTACH` e `DETACH` nas demais) e destrutores de FLS na saída. `ExitThread` volta ao quadro do host. A primeira parada da execução é registrada uma vez. Depois dela, toda thread do motor que entra no host (cada ponto de entrada chama `hostGate`) volta ao próprio quadro e termina normalmente, com o `gs` restaurado |
| `win32/Threads.cpp` | Threads (`CreateThread`, códigos de saída, IDs, `OpenThread`, `DuplicateHandle`, descrições), esperas sobre threads, eventos e timers (um lock e uma condição comuns), variáveis de condição sobre SRW, `InitOnce`, `WaitOnAddress`, `Sleep`, callbacks de thread pool numa thread própria, e informações de CPU e memória. Toda espera acorda pelo menos a cada 100 ms para checar a parada; seções críticas e SRW também |

As DLLs `api-ms-win-core-*` (API sets) são atendidas pela KERNEL32 do host, como no Windows.

| Data | Onde | Resultado |
|---|---|---|
| 26/10/2026 | Linux, host 0.5 | 144 de 256 imports. O motor criou as duas primeiras threads como no Xbox: `engine+0x149f0` com pilha de 1.048.576 bytes, chamada "Azimuth end", e `engine+0x1e2940`. A thread principal chegou à parte gráfica do launcher: parada em `USER32.dll!GetSystemMetrics` (`engine+0x2d4af`). As threads do motor terminaram nos quadros do host antes do fim |
| 26/10/2026 | PS5, host 0.5 (executado pelo usuário) | O log parou depois de `CreateThread ... thread 264`, sem parada de diagnóstico nem "Fim do host", e o processo não estava mais rodando. O host morreu calado: provavelmente uma falha de memória numa das threads, que agora rodavam em paralelo, sem tratamento de sinais. No Linux as threads novas nem começavam, porque o host encerrava antes de elas serem agendadas. Relatório em `console-reports/2026-10-26-host-0.5/host.txt` |
| 26/10/2026 | host 0.6 | Falhas fatais (`SIGSEGV`, `SIGBUS`, `SIGILL`, `SIGFPE`) numa thread do motor viram a parada da execução, com a instrução (`engine+…`), o endereço acessado e o backtrace aproximado. As threads criadas pelo motor são anunciadas antes de existir, o host espera por elas, e uma thread que começa depois da parada não entra no motor. O início de cada thread também fica no log. No Linux, as threads 260 e 264 agora começam antes da parada em `GetSystemMetrics` |
| 27/10/2026 | PS5, host 0.6 (executado pelo usuário) | Igual ao Linux. As threads 260 ("Azimuth end") e 264 começaram e rodaram em paralelo; a thread principal parou em `USER32.dll!GetSystemMetrics` (`engine+0x2d4af`); o host encerrou as threads e terminou normalmente. Relatório em `console-reports/2026-10-27-host-0.6/host.txt` |

### Janelas, GDI e quadro em imagem (host 0.7)

| Arquivo | Conteúdo |
|---|---|
| `win32/User32.cpp` | Porta do `GuestWindows`: as 45 funções de USER32 que o motor importa. Classes, janelas só de nível superior (cada uma da thread que a criou), filas de mensagens enviadas e postadas, timers, ativação e foco, `MsgWaitForMultipleObjects` sobre os mesmos objetos de espera do `Threads.cpp`. As estruturas (`MSG`, `WNDCLASSEXW`, `CREATESTRUCTW`, `WINDOWPOS`...) têm o layout x64 conferido por `static_assert`. `WNDPROC` e `TIMERPROC` são chamados pelo `guest_abi.c`. Monitor único de 1920x1080. Sem entrada ainda (`GetKeyState` devolve 0). `MessageBoxW` só com o botão OK: registra o texto e mostra uma notificação no console |
| `win32/Gdi.cpp` | Porta do `GuestGdi`: as 21 funções de GDI32 e `GetDC`/`ReleaseDC`/`DrawTextW`. DCs de memória, DIB de 32 bpp, `BitBlt`, `StretchDIBits` (vizinho e HALFTONE), `GdiAlphaBlend` pré-multiplicado, recorte com `SaveDC`/`RestoreDC`. O texto usa `gfx::Font` no lugar do DirectWrite |
| `gfx/Font.cpp` | Rasterizador TrueType próprio: tabelas `glyf`/`cmap`, glifos compostos, cobertura por área com sinal, métricas de célula do GDI (ascent/descent do Windows), avanços em pixels inteiros. Sem hinting nem ClearType |
| `gfx/Png.cpp` | PNG RGB com deflate "stored" (o SDK não tem zlib) |
| `win32/Memory.cpp` | `VirtualAlloc`/`VirtualFree`/`VirtualProtect`/`VirtualQuery` sobre `mmap`, com reservas alinhadas a 64 KiB e commit por página. Endereços fora dessa memória param com diagnóstico |
| `win32/D3d12.cpp` | Enquanto não há GPU: `D3D12GetDebugInterface` → `E_NOINTERFACE`, `D3D12CreateDevice` e `CreateDXGIFactory2` → `DXGI_ERROR_UNSUPPORTED`, as respostas de um Windows sem esses componentes. Nenhum objeto falso |

Também entraram:
- `RtlCaptureContext` em assembly no `guest_abi.c`, com o layout x64 de `CONTEXT`;
- `ProcessParameters` no PEB. O UCRT lê `Flags` (bit 31, processo seguro) ao iniciar cada thread de `_beginthreadex`; sem o bloco, a thread 264 lia o endereço 8.

As fontes são procuradas em `/data/azimuth-ps5/fonts`, depois em `azimuth-ps5/fonts` na raiz do disco do motor (`/mnt/usbN`) e, no Linux, em `/mnt/c/Windows/Fonts`. No console é preciso copiar `arial.ttf` e `arialbd.ttf` de `C:\Windows\Fonts` para uma dessas pastas. Sem elas, o GDI para com diagnóstico, dizendo onde copiar.

O host verifica uma vez por segundo se há um quadro novo e, no fim, grava a última imagem copiada para a janela em `/data/azimuth-ps5/frame.png`. A gravação é feita num arquivo temporário seguido de `rename`.

| Data | Onde | Resultado |
|---|---|---|
| 27/10/2026 | Linux, `make test-gdi` | 37 verificações de janelas e GDI passaram (as 35 do `GuestGdiTests` do AzimuthUwp, mais criar e destruir a janela), com TEB real pelo `gs` e `WNDPROC` em `ms_abi`. O quadro de teste de 1920x1080, desenhado com as mesmas chamadas do launcher e gravado pelo caminho do host (`latestFrame` + PNG), reproduz o launcher original (`engine-launcher-pc.png` do AzimuthUwp) |
| 27/10/2026 | Linux, host 0.7 | 216 de 256 imports. Passou de `GetSystemMetrics`. Registrou a classe `Azimuth.Game.Win32`, criou a janela "Azimuth" de 1920x1080 (estilo `0xcf0000`, como no Xbox), procurou o XInput e criou a thread "Azimuth game folder check". O motor exige uma fábrica DXGI antes de pintar o launcher. A mensagem dele: `Azimuth stopped: CreateDXGIFactory2 HRESULT=0x887a0004 at azimuth+0x3c7b3`, seguida de `TerminateProcess`. Nenhum quadro, porque o motor não chegou a copiar nada para a janela |

| 27/10/2026 | PS5, host 0.7 (executado pelo usuário) | Igual ao Linux até `CreateDXGIFactory2`. A fonte foi encontrada em `/mnt/usb1/azimuth-ps5/fonts/arial.ttf`. No console a pasta do jogo existe, então a thread "Azimuth game folder check" seguiu adiante e passou a calcular hashes dos arquivos, antes da mensagem do DXGI. Parada em `bcrypt.dll!BCryptOpenAlgorithmProvider` (`engine+0x58827`, thread 268) |
| 27/10/2026 | host 0.7.1 | `win32/Bcrypt.cpp` implementa as 6 funções de hash do bcrypt com SHA-256 e SHA-1 do próprio host (`Hash`, em blocos, conferido com os vetores do FIPS 180). O motor usa `FinishHash` de 32 bytes. Algoritmos e HMAC fora disso param com diagnóstico. 224 de 256 imports |
| 27/10/2026 | PS5, host 0.7.1 (executado pelo usuário) | **Mesmo ponto do Linux.** O motor criou a janela, recebeu `DXGI_ERROR_UNSUPPORTED` e mostrou a própria mensagem (`Azimuth stopped: CreateDXGIFactory2 HRESULT=0x887a0004 at azimuth+0x3c7b3`), registrada e notificada pelo host. Em seguida chamou `TerminateProcess`, e o host terminou normalmente. A thread "game folder check" usou o BCrypt sem nova parada. A fase 2 está completa até onde o motor vai sem GPU |

O launcher do motor só pinta com DXGI e D3D12: a fábrica, o adaptador e o dispositivo. Com o GDI pronto, o próximo bloco é a ponte DXGI/D3D12, ou seja, a fase 3 (vkd3d-proton sobre RADV). Quando a fábrica e o dispositivo existirem, o quadro do launcher sai em `frame.png`, sem mais mudanças no GDI. A apresentação na tela do PS5 (VideoOut) vem depois.

## Fase 3 — DXGI e D3D12 (`gpu-probe/`, depois `host/`)

### Como o Playden chega à GPU (instalação no console, analisada só de forma estática)

- O Playden roda como **título** (`FAKE78001`), não como payload, e carrega módulos `.prx` da pasta `app0/win/wine/lib/wine/x86_64-unix`.
- O RADV é o `libvulkan.prx`:
  - Mesa 26.2.0, um SELF fake-signed de 24,9 MB, SHA-256 `db21b394…0b94`, o mesmo do inventário do Playden;
  - importa `libSceAgc`, `libSceAgcDriver`, `libSceVideoOut`, `libScePosixForWebKit`, `libSceLibcInternal` e `libkernel`;
  - chega à GPU pelo driver do sistema (`sceAgcInit`, `sceAgcDriverSubmitDcb`) e tem WSI própria sobre a VideoOut (`PS5_VIDEOOUT_*`);
  - exporta a interface de ICD do Vulkan: `vkGetInstanceProcAddr`, `vk_icdNegotiateLoaderICDInterfaceVersion` e `vk_icdGetPhysicalDeviceProcAddr`.
- O `winevulkan.prx` é a ponte do Wine para esse RADV; o `wineps5.prx` cuida da apresentação.

### Caminho

1. **GPU probe (agora):** o payload `gpu-probe/` testa se o processo deste projeto inicializa a GPU por esse RADV. Ele faz quatro coisas:
   - carrega o módulo com `sceKernelLoadStartModule`;
   - cria a instância Vulkan e lista o dispositivo físico (nome, IDs, heaps, filas);
   - cria o dispositivo lógico (`sceAgcInit`);
   - faz a GPU preencher um buffer com `vkCmdFillBuffer` e confere o resultado na CPU.

   O `libvulkan.prx` é procurado primeiro em `/data/azimuth-ps5/radv/` e depois na instalação do Playden. O relatório fica em `/data/azimuth-ps5/gpu-probe.txt`, e as mensagens do próprio RADV em `gpu-stderr.txt`. Se `gpu-stop` contiver `dependencias`, `modulo` ou `instancia`, o probe para nessa etapa.
2. Se a GPU responder a um payload:
   - fábrica DXGI e adaptador no host, como objetos COM com vtables `ms_abi` no `guest_abi.c`, descrevendo o dispositivo Vulkan real;
   - depois, D3D12 pelo vkd3d-proton ligado a esse RADV. Falta verificar se a compilação nativa do vkd3d-proton já usa `ms_abi` nos métodos COM em x86-64.
3. Se a GPU só responder a um título: o host passa a rodar como título, como o `FAKE78001` do Playden, e o payload fica só para os testes sem GPU.

**Decisão (27/10/2026): seguir pelo caminho 3, o host como título.** O GPU probe 0.8 mostrou que, num payload, o código de um `.prx` fake-signed como o RADV é mapeado, mas não pode ser executado (falha de busca de instrução já na primeira instrução da entrada, mesmo com o segmento marcado executável). Os módulos do sistema rodam; o RADV, não. O Playden executa o RADV porque roda como título. A memória do jogo completo também pede o modelo de título, como já registrado na fase 0.

### Montar o título do Azimuth_PS5

Como o Playden monta o `FAKE78001` (analisado só de forma estática a partir da instalação no console):

- `eboot.bin` é um **SELF fake-signed no formato do PS4** (magic `4f 15 3d 1d`), com os segmentos **sem criptografia**. O `tools/unself.py` (agora aceita esse magic) remonta o ELF: tipo `ET_SCE_EXEC_ASLR` (`0xfe10`), base `0x100000000`, entrada em `+0x120`, 155 imports, todos com nome (libkernel, libc, Agc, AgcDriver, VideoOut, Net, Pad, AudioOut, Sysmodule, SystemService, UserService). O `param.json` fica em `sce_sys/` (titleId, contentId `UP9000-FAKE78001_00-PROSPEROWIN00001`, applicationDrmType `free`). A instalação ainda traz `lapy.elf` e manifestos próprios do Playden, que são do fluxo de empacotamento dele, não do título em si.

O SDK já tem as ferramentas para isso, em `samples/install_app`:

- `make_fself.py` converte um ELF num SELF fake-signed: `--ptype fake --paid <id> --auth-info <bytes> --app-version --fw-version`.
- `eboot.x` é o script de ligação do título: segmentos `PT_LOAD` mais o `ph_sce_procparam` (`0x61000001`).
- `payload.c` registra o título no sistema com `sceAppInstUtilAppInstallTitleDir(TITLE_ID, "/user/app/", 0)`.
- O `Makefile` mostra o fluxo inteiro: compilar o `eboot.elf`, converter para `eboot.bin`, enviar por FTP para `/system_ex/app/<TITLE_ID>/` e `/user/app/<TITLE_ID>/sce_sys/`, e rodar o payload de instalação.

Plano para o host:

1. **`title/`**: um diretório de título com `sce_sys/param.json` (titleId próprio no padrão `FAKExxxxx`, ou o que a regra de allow-list do console exigir), `icon0.png`, e um alvo de `make` que compila o host como `eboot.elf` (ligado com o script de título, base `0x100000000`) e o converte com `make_fself.py`.
2. **Memória e endereços**: o host reserva o motor em `0x140000000` e a faixa de 64 GiB; como título a base do eboot é `0x100000000`, abaixo disso, sem colisão. O orçamento real de memória pode enfim ser medido, o que era impossível no payload.
3. **Instalação**: reaproveitar o `payload.c` do SDK para registrar o título, ou enviar os arquivos para a pasta do título e iniciar pela interface, como o Playden faz.
4. **RADV**: dentro do título, carregar o `libvulkan.prx` do jeito do Playden (ainda confirmar se por `sceKernelLoadStartModule` normal ou com a entrada chamada à mão, como no probe). Depois a fábrica DXGI com vtables `ms_abi` e o vkd3d-proton.

**Implementação do título (27/10/2026):** [`title/`](../title/README.md)
gera `dist/FAKE78002/` e um ZIP, com eboot nativo, metadados e ícone. O build
usa o conversor ELF/SCE e o startup da base pública fixada pelo prospero-win
(`9c0b994a048521af6fb84c73ded364504fe250e9`), mantendo as bibliotecas C/C++
do SDK e a fronteira `ms_abi` do host. O eboot fica em `0x100000000`, com
metadados SCE e sem segmentos simultaneamente graváveis e executáveis.
O SELF passa pela inspeção de integridade. O título 0.1 foi instalado e
iniciou no console, conforme o relatório do usuário em
[`console-reports/2026-10-27-title-0.1/title.txt`](../console-reports/2026-10-27-title-0.1/title.txt).

O arquivo `/data/azimuth-ps5/title-mode` seleciona `smoke` (padrão), `host`
ou `gpu`. O smoke verifica construtores, exceção C++/unwind, pthread, TLS e
seu destrutor e reservas pequenas nos endereços do motor e do título Xbox;
não compromete vários GiB. No 0.1, construtores, exceções e isolamento de TLS
passaram; o destrutor não rodou e a execução parou ao entrar no teste de
memória, sem sinal registrado. O SDK C de compatibilidade estava fornecendo
`mmap`/`mprotect` com syscalls diretas. No 0.2, essas funções e `sysarch`
são imports da libkernel, e o build rejeita instruções de syscall direta.
O armazenamento de TLS emulado permanece vivo por uma rodada adicional de
limpeza, permitindo que a lista de destrutores da libc++abi seja processada.
Teste local: a ordem antiga reproduziu 0/64 destrutores; a corrigida passou
64/64, inclusive ordem e acesso ao TLS dentro do destrutor.
**Resultado no PS5, título 0.2 (executado pelo usuário): seis verificações
passaram e o título terminou com código 0.** As duas reservas de 64 KiB
ficaram exatamente em `0x140000000` e `0x1000000000`, sem commit, e foram
liberadas. Relatório em
[`console-reports/2026-10-27-title-0.2/title.txt`](../console-reports/2026-10-27-title-0.2/title.txt).
**Próxima validação: repetir o GPU probe como título**, começando com
`title-mode=gpu` e `gpu-stop=carregado`, antes de executar a entrada do RADV.
O smoke agora registra falhas fatais; orçamento de vários GiB, motor e GPU
no contexto de título ainda não foram validados.

**Resultado do mapeamento como título, 0.2 (executado pelo usuário):** as
duas calibrações confirmaram o mesmo contexto de sinal (`rip` em `+0xe0`).
O RADV foi encontrado no Playden e mapeado por `__sys_dynlib_load_prx`
(retorno 0, id 8402, base `0x80118000`, quatro segmentos). A parada antes da
entrada foi respeitada. As três dependências em `/system/common/lib/`
falharam com `0x80020002`. O resultado final "RADV nao carregou" era um
erro do probe 0.8: a parada `carregado` devolvia o mesmo resultado que uma
falha. O descarregamento do RADV retornou `0x80020003`, sem confirmação de
limpeza por essa chamada. Relatórios em
[`console-reports/2026-10-27-title-0.2-gpu-map/`](../console-reports/2026-10-27-title-0.2-gpu-map/).

**Título 0.3 / GPU probe 0.9:** no build de título, as dependências passam a
ser solicitadas por `sceSysmoduleLoadModuleInternal`, com os IDs do SDK
v0.45, e o probe confirma seu estado antes de mapear o RADV. A parada
planejada agora é identificada por `[PAROU]`. No Linux, a parada de
mapeamento e o teste Vulkan completo passaram; falta repetir no PS5 com
os mesmos arquivos de controle (`gpu` e `carregado`).

**Resultado do título 0.3 no PS5 (executado pelo usuário):** o Sysmodule
informou AgcDriver, Agc e VideoOut já carregados (retorno 0 nos três).
O RADV foi mapeado em `0x80118000` (id 8402), e a parada antes da entrada
terminou como `[PAROU]`, com fim normal do probe. O descarregamento ainda
retornou `0x80020003`, portanto essa chamada não confirma sua limpeza.
Relatórios em
[`console-reports/2026-10-27-title-0.3-gpu-map/`](../console-reports/2026-10-27-title-0.3-gpu-map/).
Próximo passo: `title-mode=gpu`, `gpu-stop=modulo`, para chamar a entrada
e consultar os exports do ICD, sem criar ainda a instância Vulkan.

**Resultado da chamada da entrada no título 0.3 (executado pelo usuário):**
com `gpu-stop=modulo`, o probe tentou chamar RADV+`0x4ef0` em `0x8011cef0`
e recuperou uma falha antes de qualquer retorno da entrada. O relatório não
continha sinal, RIP nem registradores, apesar da recuperação e do fim normal
do probe. Isso ainda não permite comparar a falha com a busca de instrução
vista no payload. Extrato em
[`console-reports/2026-10-27-title-0.3-gpu-entry/report-excerpt.txt`](../console-reports/2026-10-27-title-0.3-gpu-entry/report-excerpt.txt).

**Título 0.4 / GPU probe 0.10:** o código C passa a chamar a função
`(fileno)(report)`, evitando a macro FreeBSD do SDK que pode acessar o layout
privado de `FILE` antes da primeira thread. A thread principal preserva sinal,
endereço e 21 registradores e grava os detalhes no quadro recuperado do host.
No Linux, passaram os testes de escrita direta, recuperação e diagnóstico
nos caminhos de payload e título, além da parada planejada e do buffer
Vulkan. Próximo passo: repetir a chamada da entrada no PS5 com o eboot 0.4,
mantendo `title-mode=gpu` e `gpu-stop=modulo`.

**Resultado do título 0.4 no PS5 (executado pelo usuário):** a obtenção do
descritor do relatório pela função `fileno` funcionou (fd 12) e os detalhes
foram preservados. Na entrada manual do RADV, o probe capturou sinal 11,
código 1, RIP `0x800041aeb`, endereço `0x70`, RAX=0 e erro de página `0x4`
(leitura, usuário). Esse RIP está fora do RADV; a falha difere da busca de
instrução que impedia executar a entrada no payload. Ainda não se identificou
a função do sistema. O probe recuperou a falha e terminou normalmente;
o descarregamento do módulo continuou retornando `0x80020003`. Relatórios em
[`console-reports/2026-10-27-title-0.4-gpu-entry/`](../console-reports/2026-10-27-title-0.4-gpu-entry/).

**Título 0.5 / GPU probe 0.11:** nos testes de execução do módulo, o título
passa a usar `sceKernelLoadStartModule`, em vez de mapear por syscall e
chamar a entrada manualmente. A hipótese é que o caminho bruto não registra
o módulo no runtime de usuário, consistente com `ESRCH` nas APIs normais.
O build mantém o mapeamento puro para `gpu-stop=carregado` e a lógica antiga
para payloads. Registra âncoras de funções do runtime e consulta
`sceKernelGetModuleInfoFromAddr` depois de uma falha, para identificar
nome/base/offset do módulo do RIP. Compilação, integridade e testes Linux
passaram; próximo teste no PS5 com o mesmo `gpu-stop=modulo`.

| Data | Onde | Resultado |
|---|---|---|
| 27/10/2026 | Linux (WSL), GPU probe 0.1 com lavapipe | As 4 etapas passaram (instância Vulkan 1.4, dispositivo, `vkCmdFillBuffer` conferido em 1024 palavras). Isso valida a lógica do probe; a GPU real ainda não foi testada |
| 27/10/2026 | PS5, GPU probe 0.1 (executado pelo usuário) | O relatório para em "Etapa 1: carregar o RADV" (`libvulkan.prx` do Playden, 24.941.932 bytes). `sceKernelLoadStartModule` nunca retornou e o `gpu-stderr.txt` ficou vazio. O processo morreu calado ou travou dentro do carregamento: o 0.1 não tinha tratador de falhas nem watchdog |
| 27/10/2026 | GPU probe 0.2 | Mudanças: tratador de falhas (sinal, `rip`, endereço e etapa, gravados com `write`), watchdog que registra a cada 2 s a etapa em andamento, e as dependências do RADV (`libSceAgcDriver`, `libSceAgc`, `libSceVideoOut` de `/system/common/lib`) carregadas uma a uma antes dele. `gpu-stop` também aceita `dependencias` |
| 27/10/2026 | PS5, GPU probe 0.2 (executado pelo usuário) | As três dependências carregaram com start 0: `libSceAgcDriver` (id 8226), `libSceAgc` (94), `libSceVideoOut` (96). O RADV morreu dentro do `sceKernelLoadStartModule` 43 ms depois: SIGSEGV com `rip 0x0` e endereço `0x80052c000`. O par não fecha, porque um salto para 0 daria endereço 0. Pela análise estática, os 295 imports do RADV têm nome (libkernel, libc, Agc, VideoOut, só `fclose`/`fflush`/`malloc_usable_size` fracos). O início do módulo só percorre os 5 construtores do `INIT_ARRAY` (CPUID, estatísticas do LLVM, `__cxa_atexit`). Relatório em `console-reports/2026-10-27-gpu-probe-0.2/` |
| 27/10/2026 | GPU probe 0.3 | Na falha, grava todos os registradores (`trapno`, `err`, `addr`), o mapa de módulos com os segmentos (`sceKernelGetModuleList/Info`) e 64 posições da pilha, cada valor localizado em módulo e offset. Usa pilha alternativa para o tratador. Antes do RADV, faz duas falhas de calibração recuperadas (escrita em 0x10 e chamada de ponteiro nulo) para ver como o PS5 informa `rip` e endereço. `gpu-stop` aceita `falha` (falha de teste). Conferido no Linux |
| 27/10/2026 | PS5, GPU probe 0.3 (executado pelo usuário) | O relatório só tem o cabeçalho: o processo morreu na primeira calibração, a escrita em 0x10, antes do RADV. Diferenças para o 0.2, cujo tratador funcionou: tratador na pilha alternativa (`sigaltstack`) e recuperação com `siglongjmp`. Não dá para saber qual das duas causou. Relatório em `console-reports/2026-10-27-gpu-probe-0.3/` |
| 27/10/2026 | GPU probe 0.4 | A calibração é feita em passos marcados no relatório antes de cada um. Passos 1 (escrita em 0x10) e 2 (ponteiro nulo) usam o tratador na pilha normal, como no 0.2. A recuperação é feita editando o contexto salvo (`rip`/`rsp`), sem `siglongjmp`. O passo 3 repete a escrita com a pilha alternativa. O tratador escreve uma linha logo ao entrar. O watchdog começa antes da calibração. `gpu-stop` aceita `sem-calibracao`. Conferido no Linux |
| 27/10/2026 | PS5, GPU probe 0.4 (executado pelo usuário) | O tratador entra a cada falha (`sinal 11, codigo 1`), mas a edição de `mc_rip` não tem efeito: a mesma escrita falhou de novo sem parar, com mais de 28 mil entradas e o relatório crescendo até o processo ser parado. Junto com o `rip 0x0` do 0.2, isso mostra que no PS5 os registradores salvos não ficam onde o `ucontext_t` do SDK indica. O `rip`/endereço do 0.2 não são confiáveis. Resumo em `console-reports/2026-10-27-gpu-probe-0.4/` |
| 27/10/2026 | GPU probe 0.5 | Calibração 1 grava o contexto bruto do sinal (0x340 bytes) e procura nele o `rip` pelo valor (dentro de `calibrationStore`). Os outros registradores são lidos na ordem do `mcontext_t` do FreeBSD a partir dali, e o relatório da falha do RADV também usa essa posição. Proteção contra laço: na 3ª entrada do mesmo passo, o tratador sai por `siglongjmp` ou deixa o processo morrer. Conferido no Linux |
| 27/10/2026 | PS5, GPU probe 0.5 (executado pelo usuário) | **Layout do contexto de sinal resolvido.** O `mcontext_t` do kernel fica em `ucontext+0x40`, e não em `+0x10` como diz o cabeçalho do SDK. Por isso o `rip` fica em `+0xe0`. Dentro dele vale a ordem do FreeBSD, e o dump confere: `rax`=0x10 (alvo da escrita), `trapno` 0xc (falha de página), `addr` 0x10, `err` 6 (escrita, usuário), `rip` na instrução da escrita, `cs` 0x43, `rflags` 0x10246, `ss` 0x3b. A busca por valor do 0.5 achou primeiro o `r14` (que tinha o endereço da função), por isso a calibração errou. Ainda assim o `siglongjmp` funcionou na pilha normal, então a morte do 0.3 veio da pilha alternativa. O `rip 0x0` do 0.2 era o `r14`. A falha do RADV continua com endereço `0x80052c000` e `rip` desconhecido. O 0.4 continuava vivo e escrevendo no mesmo arquivo. Relatório (só a parte do 0.5) em `console-reports/2026-10-27-gpu-probe-0.5/` |
| 27/10/2026 | GPU probe 0.6 e host | O probe usa o `mcontext` em `+0x40` e confere na calibração 1. A pilha alternativa só entra com `gpu-stop` = `pilha-alternativa`. O relatório é apagado e recriado, para um probe antigo ainda vivo não se misturar nele. O tratador de falhas do host (`GuestThreads.cpp`) tinha o mesmo erro e passou a usar o `mcontext` em `+0x40`. Conferido no Linux. O host do PS5 compila; ainda sem nova versão nem execução no console |
| 27/10/2026 | PS5, GPU probe 0.6 (executado pelo usuário) | As duas calibrações voltaram certas (`rip` em `+0xe0`, `rsp` em `+0xf8`). **A falha do RADV é um salto para endereço não mapeado:** `rip` = endereço = `0x80052c000`, `err` 0x14 (busca de instrução, página ausente), `r12` com o mesmo valor (o alvo de um `call *%r12`). Quem chamou foi o código de sistema que inicia o módulo (retorno em `0x80003613f`), com `rax` = `0x80020003` (erro SCE). O RADV não tem `DT_INIT` (valor 0) e inicia pelo `e_entry` 0x4ef0, que percorre o `INIT_ARRAY`. Hipótese: a sequência de início do sistema calcula um endereço de init inválido para esse módulo. A lista de módulos do payload só mostra `eboot.bin`. Relatório em `console-reports/2026-10-27-gpu-probe-0.6/` |
| 27/10/2026 | GPU probe 0.7 | O RADV é carregado sem a sequência de início, por `__sys_dynlib_load_prx`. O probe registra os segmentos e as informações internas do módulo (init, fini, TLS e o bloco bruto), também das dependências, e depois chama a entrada do RADV diretamente (`base+0x4ef0`, só para o build conhecido do Playden). `gpu-stop` = `carregado` para antes da entrada. Conferido no Linux |
| 27/10/2026 | PS5, GPU probe 0.7 (1ª execução) | Calibrações certas. O `sceKernelLoadStartModule` de `libSceAgcDriver.sprx`, que levava 15 ms no 0.2 e no 0.6, **ficou travado** (watchdog passando de 20 s), antes de qualquer parte nova do 0.7. Hipótese: o processo do 0.6, que caiu com o driver da GPU carregado, deixou o driver preso até o console reiniciar. Relatório em `console-reports/2026-10-27-gpu-probe-0.7-travado/` |
| 27/10/2026 | GPU probe 0.8 — sem reinício entre execuções | O probe deixa de depender de reinício entre execuções. Mudanças:<br>• **Probe anterior:** `gpu-probe.pid` guarda o pid do probe em execução. A execução seguinte, antes de tocar na GPU, encerra com `SIGKILL` o anterior que ainda esteja vivo com o mesmo nome de processo (pelo `sysctl` `kern.proc`). Processos de probes antigos sem esse arquivo são encerrados se o nome contiver `azimuth`; com outro nome, só são listados.<br>• **Falha fatal:** não termina mais como queda do processo. Na thread principal, volta por `siglongjmp` para descarregar os módulos e sair normalmente. Numa segunda falha ou em outra thread, sai por `_exit`.<br>• **Limite de tempo:** uma etapa parada há 30 s encerra o processo com `_exit`.<br>• **Saída:** os módulos carregados (RADV e dependências) são sempre descarregados com `sceKernelStopUnloadModule`, do mais novo para o mais antigo.<br>Conferido no Linux: falha de teste recuperada com limpeza; probe anterior falso encerrado em 100 ms; pid reaproveitado por outro processo não tocado |
| 27/10/2026 | PS5, GPU probe 0.8 (executado pelo usuário) | Resultados:<br>• **Saída limpa funcionou.** As dependências carregaram em 15 ms. A falha foi recuperada e os quatro módulos foram descarregados (`libSceAgcDriver`, `libSceAgc` e `libSceVideoOut` com 0; o RADV com `0x80020003`). O processo terminou normalmente.<br>• **Proteção contra probe anterior desligada:** o `kinfo_proc` do PS5 não tem os 1088 bytes do cabeçalho do SDK.<br>• **Causa da falha do 0.6 confirmada:** o RADV carregou por `__sys_dynlib_load_prx` (id 97, base `0x80052c000`, mesmos segmentos do ELF). O sistema registra `init` e `fini` em `módulo+0x0`, porque o `DT_INIT` é 0. Era para lá que o `sceKernelLoadStartModule` saltava.<br>• **Nova falha:** a chamada direta da entrada (`módulo+0x4ef0`) falhou na própria primeira instrução, com `err` 0x14 (busca de instrução em página ausente), embora o seg0 apareça como executável (prot 0x4). Conclusão: neste processo de payload, o código de um SELF falso-assinado é mapeado, mas não pode ser executado. O código dos módulos do sistema roda.<br>Relatório em `console-reports/2026-10-27-gpu-probe-0.8/` |

**Conclusão das fases 1 e 2:** o motor original executa no PS5 dentro do host nativo, com TEB e TLS do Windows, na convenção de chamada do Windows. O host 0.7.1 atende o caminho do motor até o bloqueio em DXGI. O smoke do título 0.2 passou no console; o próximo passo é validar o RADV nesse contexto antes de integrar DXGI/vkd3d-proton.

**Conclusão da fase 0:** os quatro fatos de plataforma de que o host nativo depende funcionam no PS5: código gerado executável, chamadas na convenção do Windows, TEB do Windows pelo `gs` e reservas em endereço fixo, inclusive na faixa onde o jogo caiu sob o Wine. O Vulkan/RADV e o orçamento real de memória continuam pendentes de validação como título.
