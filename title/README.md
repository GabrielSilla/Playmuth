# Azimuth Launcher (Azimuth_PS5 como título) — 1.51

**Estado atual (02/11/2026):** o jogo roda perfeitamente no PS5 com o motor Azimuth 1.0.2: 4K, MSAA 4x, 60 FPS, sem engasgos, controle funcionando. Pacote `dist/Azimuth-Launcher-1.51.zip` (titleId FAKE78002).

As versões estão em ordem cronológica de baixo para cima até a 1.27, logo abaixo. A partir da 1.28 elas foram acrescentadas no **fim** deste arquivo (seção "Título 1.28" em diante). O resumo de cada versão está em `HISTORICO_DE_ALTERACOES.md`, e as lições em `docs/BASE_DE_CONHECIMENTO_PS5.md`.

---


**Teste do 1.26:** o título fechou na abertura com "no DXGI adapter" (`0x887a0002`). Não era o 1.26: o Playden se atualizou sozinho para a 0.8.0 às 00:41, apagou a instalação antiga (`/data/homebrew/FAKE78001`), e o host lia de lá o driver Vulkan (`libvulkan.prx`). O vkd3d-proton também vinha de uma pasta do kit do Playden.

O 1.27 (sem dependência de outros programas):
- **O driver RADV e o vkd3d-proton vão dentro do pacote:** `FAKE78002/radv/libvulkan.prx` (Mesa 26.2.0) e `FAKE78002/dll/d3d12.dll`/`d3d12core.dll`. São os mesmos binários, conferidos por SHA-256 no empacotamento (`title/vendor/`), com as licenças em `licenses/`.
- O host procura só na pasta do título (`/app0`, `/system_ex/app/FAKE78002`, `/data/homebrew/FAKE78002`) e em `/data/azimuth-ps5`. Nenhum caminho do Playden.
- Traz também as mudanças de desempenho do 1.26, ainda sem teste.

Instalar: copiar a pasta `FAKE78002` inteira do zip (agora ~35 MB, com `radv/` e `dll/`).

---


**Medição do 1.25 no mundo aberto (SSD interno):** 10–12 quadros/s, com o `Present` levando 0,01 ms (não é espera de vídeo). Duas threads do motor dominam. A 400 faz ~1,28 milhão de chamadas/s ao host e passa 34% do tempo nele, em `AcquireSRWLockExclusive` (469 ns), `GetTickCount64` (**953 ns, a syscall de relógio**, 142 mil/s) e `WakeAllConditionVariable` (6,5 µs). A 540 faz ~650 mil/s de `SleepConditionVariableSRW`.

O 1.26:
- **Relógio sem syscall:** `GetTickCount`, `GetTickCount64`, `QueryPerformanceCounter` e todas as esperas (prazos, `Sleep`, variáveis de condição, `WaitOnAddress`, timers, pool) leem o `rdtsc`. A âncora é corrigida contra o `CLOCK_MONOTONIC` uma vez por segundo, então o erro fica em microssegundos.
- **`WakeConditionVariable` sem ninguém dormindo** só muda a palavra da variável, sem trava nem notify.

Próximo teste: entrar no mundo aberto, dirigir 30–60 s e me avisar sem fechar o jogo.

---


**Teste do 1.24 (jogo no SSD interno):** erro de fontes. As fontes estão em `/data/azimuth-ps5/fonts` com nomes em maiúsculas (`ARIAL.TTF`), e o host procurava `arial.ttf`; no pendrive a diferença de caixa não contava.

O 1.25:
- **Fontes com qualquer grafia:** se o nome exato não existe na pasta de fontes, o host procura o arquivo ignorando maiúsculas e minúsculas.

---


**Teste do 1.23:** o contorno funcionou ("StopTimeOfDay sem o objeto da hora do dia ... marcacao pulada"), mas logo depois outro passo que também depende da simulação caiu (`0x141981bb7`, leitura em `+0x18` de um objeto ainda inexistente). Contornar passo a passo não resolve: o script de estados passa na frente do carregamento em vários pontos. A causa de fundo é o carregamento lento pelo pendrive (~38 MB/s).

O 1.24 (jogo no SSD interno):
- **O host procura o jogo primeiro em `/data/PC/FH2/azimuth.exe`**, antes de `/data/azimuth-ps5` e dos pendrives. A estrutura é a mesma do pendrive (`/data/PC/FH2` = `E:\PC\FH2`, `/data` = `E:`).
- **Nomes sem diferença de maiúsculas no disco interno:** o `/data` diferencia maiúsculas, mas o Windows e o exFAT do pendrive não (o jogo pede `Media\...` e a pasta é `media`). Abaixo de uma montagem em `/data`, cada nome do caminho assume a grafia que tem na pasta, a partir de listagens lidas uma vez e guardadas. Um nome que a pasta não tem (um arquivo a ser criado) mantém a grafia pedida. No pendrive nada muda.

Próximo teste: com o jogo copiado para `/data/PC/FH2`, `title-mode=host`, Start, iniciar o jogo.

---


**Teste do 1.22:** a mesma queda. O log das leituras explica: o cache pega os arquivos da pasta `Mount`, mas o carregador lê blocos de ~64 KiB **em sequência**, ~600 por segundo (~38 MB/s, o limite do pendrive), cada trecho uma única vez. O carregamento é limitado pela velocidade do pendrive, mais lento que o disco do Xbox, e o script de estados passa na frente da inicialização do `ForzaSimulation`.

O 1.23:
- **Contorno para o `StopTimeOfDay`:** o passo que caía só marca a hora do dia como parada no objeto que a simulação cria (`cmpb $0,0x27(%rax); jne +3; movb %bl,0x24(%rax)`). Quando esse objeto ainda é nulo, não há o que marcar. O tratador de falhas do host reconhece exatamente essa instrução (pelos 9 bytes, com `rax` nulo e o acesso em `0x27`), pula os 9 bytes e deixa a thread seguir, registrando o fato nas 5 primeiras vezes. Nenhum outro caso é tratado assim.

Próximo teste: `title-mode=host`, Start, iniciar o jogo e entrar no mundo aberto.

---


**Teste do 1.21:** a mesma queda no carregamento, com as fibras no mesmo lugar. O `ReadFile` continuou em ~1 ms por chamada (591/s), ou seja, o cache não pegou essas leituras. O despejo mostra que a thread 348 é o carregador de arquivos do jogo (para cada leitura: `HeapAlloc`, `CreateEventW`, `ReadFile`). Provável causa da falta de efeito: o cache só valia para arquivos abertos sem acesso de escrita, e o motor deve abrir os arquivos do jogo com algum acesso de escrita (o `CreateFile2` emulado).

O 1.22:
- **Cache para qualquer arquivo da pasta `Mount`**, qualquer que seja o acesso pedido na abertura. Uma escrita num arquivo do jogo (nunca vista) descarta o cache, para não ficar desatualizado.
- **Log das leituras:** as 40 primeiras leituras fora do cache e toda leitura que leve 5 ms ou mais, com caminho, tamanho, posição, tempo, acesso de abertura e se usou o cache.

Próximo teste: `title-mode=host`, Start, iniciar o jogo.

---


**Teste do 1.20:** voltou a queda no carregamento (`StopTimeOfDay`), com as fibras no mesmo lugar do 1.18: a fibra de carregamento esperando um job dentro do `ForzaSimulation`. O 1.19 removeu a maior causa, mas a corrida continua possível. O medidor de tempo no host mostrou:
- **`ReadFile`: ~0,8–1 ms por chamada**, ~1.200 leituras pequenas por segundo; a thread de arquivos do jogo passa quase 100% do tempo lendo do pendrive. É o que atrasa o job do carregamento e o streaming.
- `QueryPerformanceCounter`: ~930 ns por chamada (um `clock_gettime`, que no console é chamada ao sistema), com até 900 mil chamadas/s do ritmo de quadros do motor.
- `EnterCriticalSection` custa 60–70 ns (as travas do 1.14 estão boas); `SetEvent` ~4 µs, `SwitchToThread` 3–8 µs.

O 1.21 (sem testes no Linux, direto para o console):
- **Cache de leitura dos arquivos do jogo:** as leituras de arquivos da pasta `Mount` abertos só para leitura passam por um cache de blocos de 128 KiB (até 256 MiB, o menos usado sai). Uma leitura pequena vira, no máximo, uma leitura de 128 KiB do pendrive, e as seguintes saem da memória. Leituras de 512 KiB ou mais vão direto ao arquivo.
- **`QueryPerformanceCounter` pelo contador de ciclos do processador** (`rdtsc`), em 10 MHz como antes, com a taxa medida uma vez contra o relógio monotônico (20 ms na primeira chamada).

Próximo teste: `title-mode=host`, Start, entrar no mundo aberto e dirigir uns 30 s.

---


**Teste do 1.19:** **o jogo carregou o mundo aberto com o cenário completo**, sem a queda no carregamento: o despertar direcionado resolveu. Falta a velocidade: com `frame_rate=30` e ritmo fixo, o jogo continua em câmera lenta. A linha de desempenho mostra ~30 quadros/s no menu, mas **11–12 quadros/s no mundo aberto**, quase 3x abaixo do ritmo. O `Present` leva 0,02 ms. A thread 404 faz ~1,2 milhão de chamadas ao host por segundo (`HeapAlloc`/`HeapFree`, `AcquireSRWLockExclusive`, `WakeConditionVariable`), a 544 umas 650 mil (`SleepConditionVariableSRW`, `_aligned_free`), e o total passa de 3,5 milhões/s. No Linux, um `HeapAlloc` sai por ~400 ns e um `HeapFree` por ~450 ns, já contando a passagem pela fronteira. A suspeita é que o custo de cada chamada ao host, multiplicado por milhões, deixa a thread do quadro sem tempo.

O 1.20 (medição):
- **Tempo no host, a cada 10 s:** para cada função do host, o tempo total dentro dela (ms por segundo, pelo contador de ciclos do processador), as chamadas por segundo e o custo médio por chamada. Para cada thread, a fração do tempo gasta trabalhando no host, sem contar esperas e sleeps. Para isso, toda entrada do host passou a marcar também a saída (`hostLeave`), o que só as entradas em assembly faziam.

Próximo teste: `title-mode=host`, Start, entrar no mundo aberto e dirigir uns 30 s; klog sendo capturado.

---


**Teste do 1.18:** a mesma queda no carregamento, agora com as fibras no relatório. A queda é na fibra principal (0), e a **fibra de carregamento (1) estava suspensa dentro da inicialização do `ForzaSimulation`** (`0x1402e518e`), antes da linha que cria o objeto da hora do dia. A cadeia: `0x14197f060` → `0x14197e6e0` → `0x1413d57d0` → `0x1413d9bb0` → `0x141088420` → `SwitchToFiber`. `0x1413d9bb0` é o **"esperar job" do sistema de jobs do jogo**: enquanto o job não termina (status em `+0x34`), ele ajuda, chama `SwitchToThread` e, se a thread é a principal (índice 0 na tabela de workers), devolve a vez à fibra principal para ela continuar desenhando. No Xbox o job termina quase na hora e essa espera praticamente não acontece. Aqui o job levou vários quadros, e a fibra principal avançou o script de estados até o `StopTimeOfDay` antes de a simulação existir.

Por que o job demora: as esperas do host acordavam **todo mundo**. Uma única condição para o processo inteiro servia a eventos, semáforos, mutexes, threads e portas de conclusão, e uma única condição servia a `WaitOnAddress`. A linha de desempenho mostrou ~112 mil `WakeByAddressSingle`/s e ~15 mil `SetEvent`/s; cada um acordava as ~100 threads que esperavam por qualquer coisa, e elas checavam e voltavam a dormir. Eram milhões de despertares inúteis por segundo, disputando a CPU e uma trava global, e os workers de job ficavam famintos.

O 1.19:
- **Despertar direcionado:** cada thread que espera dorme numa condição própria e registra os objetos pelos quais espera; `SetEvent`, `ReleaseSemaphore`, `ReleaseMutex`, o fim de uma thread, `SetWaitableTimer` e `PostQueuedCompletionStatus` acordam só quem espera por aquele objeto. É uma condição por thread (~200 objetos do kernel), não por objeto do jogo.
- **`WaitOnAddress` em 64 baldes por endereço:** `WakeByAddress*` acorda só o balde daquele endereço.
- Verificação no Linux: todos os testes (inclusive ConcRT, pool de threads, fibras, seções críticas e D3D12) e o motor real até o launcher.

Próximo teste: `title-mode=host`, Start, iniciar o jogo (`frame_rate=30`); klog sendo capturado.

---


**Teste do 1.17:** o cache de `stat` funcionou (`GetFileAttributesW` saiu do topo), mas a queda em `States::StopTimeOfDay` continuou, ~7,7 s depois da fibra de carregamento. A velocidade do carregamento não era a causa.
- **Velocidade:** cada `Present` leva só ~0,45 ms (`SyncInterval` 1), então a apresentação não é o gargalo. O `azimuth.ini` está com `frame_rate=30` desde a sessão das 20:14. A execução que chegou ao mundo aberto estava com **60**, e o jogo entrega ~30: com ritmo fixo, isso é a câmera lenta. Em 30 (a taxa do Forza Horizon 2 no Xbox One), a 1.17 mediu 29–30 quadros/s, então a velocidade deve ficar normal.
- **Queda:** `StopTimeOfDay` é um nó do script de estados (`stateflow.zip`) que para a hora do dia no objeto criado no fim da inicialização do `ForzaSimulation` (slot 5). A hipótese é que essa inicialização roda na fibra de carregamento e cede a vez no meio (esperando algo), e a fibra principal executa o script até esse nó antes de a simulação ficar pronta.

O 1.18 (diagnóstico):
- **Fibras no relatório de parada:** cada fibra, se está rodando ou suspensa, em que thread e, para as suspensas, os endereços de código na pilha a partir de onde cederam a vez. Isso mostra onde a fibra de carregamento estava na queda e o que esperava.

Próximo teste: `title-mode=host`, Start, iniciar o jogo (manter `frame_rate=30`); klog sendo capturado.

---


**Testes do 1.15 e do 1.16:** uma execução do 1.15 **chegou ao mundo aberto**: carro, HUD, tráfego e céu aparecem, mas sem o cenário detalhado, e o jogo roda em câmera lenta (captura em `console-reports/2026-10-30-title-1.15-start/captura-170206.png`). As outras execuções (1.15 e 1.16) caíram em `States::StopTimeOfDay` antes do mundo aberto. A linha de desempenho do 1.16 mostrou:
- **~30 quadros/s com o motor em 60 e ritmo fixo**: o jogo avança um passo fixo por quadro, então a 30 tudo anda na metade da velocidade, que é a câmera lenta.
- Na thread principal do jogo (344), que também roda a fibra de carregamento, **172 mil `GetFileAttributesW` por segundo**: o jogo confere se cada recurso existe solto antes do `.zip`, e cada consulta era um `stat` no pendrive. A fibra de carregamento rende pouco por fatia, a máquina de estados chega ao `StopTimeOfDay` antes de o `ForzaSimulation` terminar de inicializar (as duas coisas alternam na mesma thread), e o jogo cai. Na execução que chegou ao mundo aberto, o carregamento ganhou essa corrida por pouco.
- A thread de ritmo do motor gira em `QueryPerformanceCounter` (876 mil/s), e a 344 consulta `WaitForSingleObject` com prazo zero (3,1 milhões/s).

O 1.16 (já testado) trouxe:
- **Portas de conclusão de E/S de verdade:** `CreateIoCompletionPort` com um arquivo agora liga o arquivo à porta; uma leitura ou escrita com `OVERLAPPED` nesse arquivo coloca o pacote na porta (com a chave) e responde `ERROR_IO_PENDING`, como o caminho assíncrono do Windows. Antes a ligação era descartada e nenhum pacote chegava, e o streaming do mundo, que usa `CreateIoCompletionPort`, `GetQueuedCompletionStatus` e `GetOverlappedResultEx`, nunca recebia as leituras. Também `SetFileCompletionNotificationModes`, e `GetOverlappedResultEx` informa o fim de arquivo.
- **Log assíncrono:** cada linha fazia `write` + `fsync` na thread do jogo que logou; agora vai para uma fila escrita por uma thread própria, com `fsync` a cada 500 ms e no fim. As chamadas de memória que se repetem no streaming (`MapViewOfFile3`, commits, reservas) registram as 300 primeiras e uma a cada 1000.
- **Linha de desempenho a cada 10 s:** quadros por segundo e as threads que mais chamam o host.

O 1.17:
- **Cache de `stat` do conteúdo do jogo** (pasta `Mount`, que nada escreve): `GetFileAttributesW`, `GetFileAttributesExW` e a checagem de existência do `CreateFileW` consultam o pendrive uma vez por caminho (inclusive para "não existe"); um arquivo do jogo sabidamente ausente falha sem tentar abrir. Deve acelerar muito o carregamento, o que ataca a queda no `StopTimeOfDay` e o streaming.
- **O `Present` medido:** a linha de desempenho traz o tempo médio dentro de cada `Present` e o `SyncInterval`/flags pedidos, para entender os 30 quadros/s.
- O `host.txt` da execução anterior passa a ser preservado como `host-anterior.txt`.

Próximo teste: `title-mode=host`, Start, iniciar o jogo e, se chegar ao mundo aberto, andar um pouco; klog sendo capturado.

---


**Teste do 1.14:** o abort da libkernel sumiu. Agora deu SIGSEGV no código do jogo (0x14093ca20) lendo o endereço 0x27, 3,5 s depois de a fibra de carregamento ser criada. O método é `States::StopTimeOfDay` (slot 4 da vtable, identificado pelo RTTI). Ele lê `gerente(0x1434aba60)->+0x48->+0x98->+0x480`, e esse campo é criado na inicialização de `ForzaSimulation` (slot 5, alocação de 0x410 bytes pelo alocador do jogo). O campo ainda estava nulo, então ou o estado rodou antes de a simulação terminar de inicializar na fibra de carregamento, ou a alocação falhou. As três últimas execuções pararam em pontos diferentes, o que sugere uma questão de tempo.

O 1.15 é de diagnóstico:
- **Fibras no log:** as 40 primeiras trocas (`SwitchToFiber`), depois uma a cada 1000, com a fibra de origem, a de destino e quem chamou; também `DeleteFiber` e o retorno da rotina de uma fibra. Isso mostra se a fibra de carregamento continuou rodando, parou ou terminou antes da falha.

Próximo teste: `title-mode=host`, Start, e iniciar o jogo pelo menu; klog sendo capturado.

---


**Teste do 1.13:** foi bem mais longe no carregamento do mundo aberto e depois congelou. As threads 352 e 380 morreram juntas com SIGABRT dentro da libkernel (0x8000b90c0), em `EnterCriticalSection`, e a estrutura em `rdi` cita o pool "SceKernelInternalMemory". No 1.13 cada seção crítica ganhou um `std::mutex` e uma `std::condition_variable`. No PS5, cada um deles é um objeto que a libkernel tira de um pool interno pequeno, e o jogo cria milhares de seções críticas (as threads 352 e 380 chamam `InitializeCriticalSectionAndSpinCount` sem parar). O pool acabou, a libkernel abortou, e as threads morreram segurando travas, o que congelou o resto. O mesmo problema existia, latente, nas travas SRW (um `pthread_rwlock` por trava) e nas variáveis de condição (um mutex e uma condição por variável). Nenhum dos dois era liberado nunca, porque o Windows não tem função para destruí-los.

O 1.14 tira os objetos do kernel dessas travas:
- **`CRITICAL_SECTION`:** o estado fica nos campos da própria estrutura. OwningThread é tomado com compare-exchange, RecursionCount guarda a recursão e LockSemaphore conta quem espera. O caminho rápido é uma operação atômica, e quem espera dorme num de 64 pares mutex/condição compartilhados, escolhido pelo endereço. Qualquer thread pode liberar, e a espera de 10 s com o nome da dona continua sendo registrada.
- **`SRWLOCK`:** a palavra da trava é o estado (bit 0 = exclusiva, leitores contados de 2 em 2), com os mesmos 64 pares para esperar.
- **`CONDITION_VARIABLE`:** a palavra é um contador de geração, e quem dorme espera num de 64 pares compartilhados. Um wake acorda todos daquele par, o que o Windows permite.
- Com isso, o número de objetos do kernel usados por travas fica fixo (cerca de 130), por mais travas que o jogo crie.
- Verificação no Linux: seções críticas, ConcRT, pool de threads, fibras, exceções, FFmpeg e D3D12 (vkd3d e msvcr110 usam muito SRW e variáveis de condição). Tudo passou.

Próximo teste: `title-mode=host`, Start, e iniciar o jogo pelo menu; klog sendo capturado.

---


**Teste do 1.12:** as fibras funcionaram (a primeira foi criada pela thread 344 com pilha de 384 KiB), o jogo saiu do menu e começou a carregar o mundo aberto, mas parou no carregamento. O processo **não morreu**: o `host.txt` continuou crescendo, e o klog não registrou saída do pid 283. A memória estava folgada (267 MiB livres de memória flexível, 8,6 GB de memória direta, sem variação). Comparando os despejos seguidos, só seis threads avançavam: a janela, o áudio e mais quatro. As threads do jogo 344, 352 e 380 ficaram paradas com `EnterCriticalSection` como última chamada. Isso tem cara de deadlock em seções críticas, e o host não avisava de esperas longas nelas (só em `WaitFor*`).

Havia também uma diferença de semântica. O host implementava a seção crítica com um `pthread_mutex` recursivo, que só pode ser liberado pela thread que o pegou. No Windows, qualquer thread pode chamar `LeaveCriticalSection`; um job retomado em outra thread faz isso. Com o pthread, essa liberação falhava em silêncio (EPERM) e a seção ficava presa para sempre.

O 1.13:
- **Seções críticas com a semântica do Windows:** o dono e a contagem de recursão são registros do próprio host (`std::mutex` + variável de condição), e qualquer thread pode liberar. As 10 primeiras liberações por uma thread que não é a dona ficam no log.
- **Espera longa em seção crítica:** depois de 10 s esperando, o host registra a seção, a thread que espera, **a thread dona**, a recursão, há quanto tempo ela segura a seção e os endereços de quem chamou. Se for um deadlock, o próximo log mostra a cadeia.
- Verificação no Linux: recursão, `TryEnter` com a seção ocupada, liberação por outra thread e espera até a dona sair. Tudo passou, e o restante (exceções, ConcRT, fibras, FFmpeg, D3D12) continua passando.

Próximo teste: `title-mode=host`, Start, e iniciar o jogo pelo menu; klog sendo capturado.

---


**Teste do 1.11:** `FreeLibraryWhenCallbackReturns` foi chamada e o ConcRT seguiu. Depois o motor mostrou "Azimuth stopped: kernelx.dll!ConvertThreadToFiber args=0x1351950078 at game+0x108736a" e chamou `TerminateProcess` (código 0xE0424833). O sistema de jobs do jogo usa **fibras**. A emulação do `kernelx` no motor repassa para as fibras do Windows, que procura com `GetProcAddress` na partida (`ConvertThreadToFiber`, `CreateFiber`, `SwitchToFiber`, `DeleteFiber`, `ConvertFiberToThread`), e o host não tinha nenhuma delas.

O 1.12 implementa as fibras do Windows (win32/Fibers.cpp):
- Uma fibra é uma pilha (no alocador do host, com 256 KiB extras para os quadros do host) mais um ponteiro de pilha salvo. `SwitchToFiber` troca a pilha com `fiberSwitch` (assembly em guest_abi.c), que salva os registradores preservados do System V, o MXCSR e o controle do x87. Os registradores preservados do Windows (rdi, rsi, xmm6-15) ficam salvos pela entrada ms_abi de `SwitchToFiber` na pilha de cada fibra.
- O TEB acompanha a fibra que está rodando: `FiberData` em gs:[0x20] (o `GetCurrentFiber`/`GetFiberData` do jogo lê direto), o StackBase/StackLimit e o bit `HasFiberData`. Os relatórios e a amostragem de threads leem só até o topo da pilha da fibra atual.
- `ConvertThreadToFiber(Ex)`, `ConvertFiberToThread`, `IsThreadAFiber`, `CreateFiber(Ex)`, `SwitchToFiber` e `DeleteFiber`. Quando a rotina de uma fibra retorna, ou quando a fibra em execução é apagada, a thread termina, como no Windows.
- Verificação no Linux, pelas mesmas entradas que o motor obtém: 2000 trocas entre duas fibras com um `double` preservado, `GetFiberData` lido pelo gs, os 31 casos de exceção rodando numa fibra e a thread restaurada no fim. Tudo passou.

Próximo teste: `title-mode=host`, Start, e iniciar o jogo pelo menu; klog sendo capturado.

---


**Teste do 1.10:** o ConcRT passou a funcionar (threads do pool, `SetThreadpoolWait`, `CloseThreadpoolWait`). Depois veio um SIGSEGV no próprio host, em `hostMissingImport` (endereço 0x10045518a; o título carregou com deslocamento de 0x400000, então no ELF fica em 0x10005518a). Uma thread do pool chamou um import sem implementação. Ao montar o relatório, o host varria 256 posições da pilha sem limite, passou do topo da pilha da thread (0x7e9f58000) e caiu, escondendo o nome do import. Pelo contexto (callback de espera do ConcRT logo depois de `CloseThreadpoolWait`) e pela lista de imports sem implementação do `msvcr110`, era `FreeLibraryWhenCallbackReturns`.

O 1.11:
- **`FreeLibraryWhenCallbackReturns`**, `CallbackMayRunLong` e `DisassociateCurrentThreadFromCallback` implementadas. Os módulos nunca são descarregados, e a primeira chamada fica no log.
- **`hostMissingImport` só varre até o topo da pilha da thread**, então o relatório de um import ausente volta a sair mesmo numa thread com pouca pilha usada.
- A verificação no Linux agora inclui uma thread de fora do agendador que agenda uma tarefa e termina. O ConcRT espera por ela no pool, e o callback chama `FreeLibraryWhenCallbackReturns`, o mesmo caminho do console. Passou.

Próximo teste: `title-mode=host`, Start, e iniciar o jogo pelo menu; klog sendo capturado.

---


**Teste do 1.09:** a verificação de espaço em disco passou ("GetDiskFreeSpaceExW: espaco fixo..."), e o jogo foi além ao iniciar pelo menu. Em seguida, o ConcRT do `msvcr110` (o agendador do PPL/`concurrency::task`) lançou `scheduler_resource_allocation_error`, que ninguém tratou. Na primeira tarefa, o gerenciador de recursos dele procura as funções do Windows 7+ com `GetProcAddress` e lança essa exceção se faltar qualquer uma. No console faltaram `SetThreadGroupAffinity` e `GetThreadGroupAffinity`. Reproduzindo no Linux, também faltavam `CreateRemoteThreadEx`, as funções de UMS e o `combase.dll` (`RoInitialize`).

O 1.10:
- **Topologia por grupos:** `Get/SetThreadGroupAffinity`, `GetCurrentProcessorNumberEx` e `GetLogicalProcessorInformationEx` (um núcleo por processador, um pacote, um nó NUMA e um grupo).
- **Threads do ConcRT:** `CreateRemoteThreadEx` (no próprio processo) e as listas de atributos (`Initialize/Update/DeleteProcThreadAttributeList`).
- **UMS presente, mas sem suporte:** as 13 funções existem e respondem `ERROR_NOT_SUPPORTED`, como num Windows sem UMS. O ConcRT só usa UMS quando um agendador pede.
- **Pool de threads:** esperas e timers (`CreateThreadpoolWait/Timer`, `Set...`, `WaitFor...Callbacks`, `Close...`, `IsThreadpoolTimerSet`), e as formas antigas `RegisterWaitForSingleObject`/`UnregisterWait(Ex)` e fila de timers (`CreateTimerQueue(Timer)`, `ChangeTimerQueueTimer`, `DeleteTimerQueueTimer`/`Ex`). Cada espera ou timer tem uma thread do motor própria.
- **`combase.dll`:** `RoInitialize`/`RoUninitialize`. **ETW:** `RegisterTraceGuidsW` e afins registram, mas nada é rastreado.
- A verificação D3D12 no Linux agora cria o agendador do ConcRT, executa 8 tarefas e testa um timer e uma espera do pool de threads. Tudo passou.

Próximo teste: `title-mode=host`, Start, e iniciar o jogo pelo menu; klog sendo capturado.

---


**Teste do 1.08:** ao iniciar o jogo pelo menu, falha fatal (SIGBUS, trap 9 = proteção geral) na thread 344, com o `rip` no meio de uma instrução do jogo (RVA 0x25cb26) e `rax` com um valor não canônico. A última chamada ao host foi `GetDiskFreeSpaceExW`, a primeira em todos os testes, feita pela verificação de cota de save do motor ("Local save quota or disk space exhausted", engine 0x14013ade0 → `std::filesystem::space`). O host respondia com `statvfs`. No console, a estrutura preenchida não bate com a dos cabeçalhos, a chamada escreveu além dela na pilha do host, e o retorno caiu num endereço qualquer do jogo.

O 1.09:
- **`GetDiskFreeSpaceExW` sem `statvfs` no console:** confere se o caminho existe e responde com um valor fixo, 256 GiB livres de 1 TiB, registrado uma vez no log. No Linux, continua com `statvfs`.

Próximo teste: `title-mode=host`, Start, e iniciar o jogo pelo menu; klog sendo capturado.

---


**Teste do 1.07:** no launcher, cada aperto valia por dois. Com o `xinput1_4.dll` disponível, o motor lê o controle pelo XInput desde a partida (o primeiro `XInputGetState` vem logo depois de "Controle conectado"), e o launcher recebia o mesmo botão pelo XInput e pelas teclas que o host continuava enviando.

O 1.08:
- **As teclas do controle param enquanto o motor lê o XInput:** se houve `XInputGetState` no último segundo, o host não envia mais `WM_KEYDOWN`. Uma tecla que já desceu sempre recebe o `WM_KEYUP`, para não ficar presa. Se o motor parar de ler o XInput, as teclas voltam.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 1.06 com Start: o jogo chegou ao menu.** O controle, porém, não funcionava no jogo. O log do motor repete "desktop XInput unavailable; no controller exposed", e o host registrou "LoadLibraryExW: xinput1_4.dll nao disponivel no host". O launcher funcionava porque o host converte o controle em teclas (Enter, setas, Esc) para a janela, mas o jogo (o `Windows.Xbox.Input` do motor) só enxerga controles pelo XInput do desktop.

O 1.07:
- **xinput1_4.dll no host** (win32/Xinput.cpp): `XInputGetState`, `XInputSetState`, `XInputGetCapabilities`, `XInputEnable` e `XInputGetBatteryInformation`. O usuário 0 é o controle do PS5 que o host já lê pelo ScePad; os usuários 1 a 3 aparecem desconectados. Mapeamento: Cross=A, Circle=B, Square=X, Triangle=Y, L1/R1=LB/RB, L2/R2=gatilhos, L3/R3, Options=Start, touchpad=Back, direcional e analógicos (eixo Y invertido, como no XInput).
- **Vibração:** `XInputSetState` vai para o `scePadSetVibration` (motor grande e pequeno).
- As teclas do launcher continuam como antes.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 1.05 com Start:** o relatório de `EMFILE` mostrou 239 arquivos abertos pelo `CreateFileW` (165 deles em `media/cars`). Os 13952 descritores que o console anuncia não existem de fato: o `open` falha perto de 256. O jogo abre os arquivos de carro e o motor, quando vai carregar o FFmpeg para o áudio, não consegue abrir a DLL e encerra o processo. Daí a tela preta com CPU em 0% e RAM parada em 2 GB.

O 1.06:
- **Um handle de arquivo do Windows não é mais um descritor do sistema.** Cada handle guarda o caminho, como reabrir o arquivo (sem `O_CREAT`/`O_TRUNC`) e a própria posição, e lê/escreve com `pread`/`pwrite`. Os descritores ficam num cache de no máximo 96. Quando falta espaço, ou se o `open` der `EMFILE`, o menos usado recentemente é fechado e reaberto na próxima vez que o handle for usado. Um descritor em uso por uma operação em outra thread nunca é fechado no meio.
- Teste novo `make test-files`: 300 arquivos abertos com limite de 200 descritores, posições preservadas depois de reabrir, `SetEndOfFile`, reabertura sem truncar e mapeamento de arquivo.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 1.04 com Start:** o carregador agora registra o erro: "Abrir .../avutil-60.dll falhou (errno 24)". 24 é `EMFILE`: o processo esgotou os descritores de arquivo, provavelmente a mesma causa do `fopen` do 1.03. Com o jogo rodando, ou o limite de descritores do título é baixo para tantos arquivos abertos (os `.zip` do jogo, por exemplo), ou algo no host não devolve descritores. O 1.05:
- **Limite de descritores:** na partida, o host registra o `RLIMIT_NOFILE` e sobe o limite atual até o máximo permitido. No Linux, foi de 10240 para 1048576.
- **Relatório de `EMFILE`:** na primeira vez que `CreateFileW` ou o carregador de módulos recebe `EMFILE`, o host registra quantos descritores o processo tem abertos, quantos arquivos o `CreateFileW` mantém e em quais pastas. Isso separa um vazamento de um limite pequeno.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 1.03 com Start:** mesma tela preta depois das logos, agora com a causa no log: "Modulo avutil-60.dll nao carregado: arquivo vazio ou ilegivel", logo depois de "Lendo .../data/bin/avutil-60.dll (971264 bytes)". O carregador lia DLLs com `fopen`, que tira o `FILE` e o buffer do malloc do título. Esse heap é pequeno e fixo. Na partida ainda havia espaço (o msvcr110, da mesma pasta, carregou), mas quando o motor carrega o FFmpeg, alguns minutos depois e com o jogo rodando, o `fopen` falha.

O 1.04:
- **O carregador de módulos lê com `open`/`read`**, que não alocam nada no malloc do título, e registra o `errno` se a abertura falhar.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 1.02 com Start:** as telas de abertura voltaram, e a memória se manteve (sem os blocos de 1 GiB, memória direta acima de 9 GB). Depois delas, o motor parou com "SHAPE graph: Pinned local FFmpeg DLL load failed error=126" (`azimuth+0x83909`). SHAPE é o hardware de áudio do Xbox One que decodifica o XMA do jogo; o motor o emula com o FFmpeg (`avcodec-62.dll` e `avutil-60.dll` em `data\bin`). O host não carregava esses DLLs, que são compilados para a CRT Universal do Windows (imports `api-ms-win-crt-*`). O 1.03:
- **FFmpeg carregado pelo host:** `avutil-60` e `avcodec-62` entram na lista de DLLs carregáveis.
- **Dependências automáticas:** um DLL carregável do qual outro importa é carregado antes (`avcodec-62` importa 141 funções do `avutil-60`). Os imports entre DLLs passam a ser achados pelo nome do arquivo (`avutil-60`, e não `avutil_60`).
- **CRT Universal:** os `api-ms-win-crt-*` são servidos pelas funções do msvcrt do host, como `api-ms-win-core-*` vai para o kernel32. Faltavam 69 funções, todas implementadas:
  - impressão e leitura formatada com a `va_list` do Windows: `__stdio_common_vsprintf`/`vfprintf`/`vfwprintf`/`vsscanf`;
  - inicialização de DLL: `_initialize_onexit_table`, `_register_onexit_function`, `_execute_onexit_table`, `_crt_atexit`, argv e ambiente;
  - `_beginthreadex`, `_aligned_realloc`;
  - matemática e cadeias de caracteres;
  - tempo (UTC);
  - `GetConsoleScreenBufferInfo` e `SetConsoleTextAttribute` (sem console).
- **Teste no Linux:** a verificação `host --d3d12` carrega o FFmpeg: `avutil-60` com 137 de 137 imports e `avcodec-62` com 206 de 206, os dois inicializados. Respondeu avcodec 62.28.103, avutil 8.1.3 e decodificador xma2 presente.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 1.01 com Start:** a imagem do jogo foi para memória direta (`0x1000010000`) e o código rodou dela. O PS5 executa memória direta sem a passagem para memória flexível, que nem foi usada. Mas, antes das telas de abertura, o motor pediu reservas de 128 KiB com faixa `0x20000000000`-`0x3ffffffffff` (2 a 4 TiB). A área do título vai até `0xFC00000000`, e o `sceKernelReserveVirtualRange` recusou (`0x8002000c`). O `mmap` de antes ignorava a faixa e funcionava, porque o motor aceita o endereço que recebe. Agora 46 reservas falharam, e o jogo leu um ponteiro nulo (thread 344).

O 1.02:
- **Faixa de endereço na reserva:** a parte dentro da área do título é respeitada (a faixa de 4 GiB a 1 TiB do 1.00 vira `0x1000000000`-`0xFBFFFFFFFF`). Uma faixa toda fora da área (os 2 a 4 TiB) é ignorada, e a reserva vai para qualquer lugar da área, como no `mmap` de antes. O log avisa uma vez.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 1.00 com Start: o jogo iniciou.** As telas de abertura (Microsoft, Playground Games…) apareceram, e o tratamento de exceções funcionou: a `COMException` do Kinect foi capturada. Depois, onde deveria entrar o menu, a tela ficou preta. O motor registrou que o jogo parou de apresentar no quadro 841. Cerca de um minuto depois, o jogo se derrubou de propósito: "XMemAlloc alloc failed" (`0x14125da99`, escrita no endereço 2), memória esgotada. A sequência:
1. **Reservas pequenas esgotadas.** O jogo pedia muitas reservas pequenas por `VirtualAlloc`, umas sem faixa e outras com faixa de endereço (`0x100000000`-`0xffffffffff`, alinhamento 64 KiB). Abaixo de 1 GiB, o host as fazia com `mmap` anônimo, que no título sai de uma janela pequena de endereços e da memória flexível. Por volta de `0x229000000` a janela acabou, e reservas de 64 KiB até 512 MiB passaram a falhar.
2. **Recurso do motor.** O alocador do motor (`engine+0x7b879`) recorreu então a blocos de 1 GiB sem faixa: onze deles, um a cada 2 s, depois de 2, 4 e 8 GiB. No host, cada commit vira memória direta de verdade, zerada, e ela acabou.
3. **Fim.** Sem memória, o `XMemAlloc` do jogo falhou e o jogo abortou.

O 1.01:
- **Toda reserva de `VirtualAlloc` no console é espaço de endereço na área do título** (`sceKernelReserveVirtualRange`), com memória direta nos commits, como já era a partir de 1 GiB. A faixa de 4 GiB a 1 TiB fica atendida, e a memória flexível (448 MiB) deixa de pagar pelas alocações do jogo.
- **Código em memória direta:** se o kernel recusar tornar executável um bloco de 16 KiB (o motor mapeia a imagem do jogo por `VirtualAlloc` e depois torna o código executável), o host passa aquele bloco, com o conteúdo, para memória flexível no mesmo endereço. Só as páginas de código usam memória flexível assim, e o log registra quantas.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 0.99 com Start:** `GetCurrentPackageId` passou a responder, e o relatório novo mostrou a exceção:
- **O erro:** `Platform::COMException` com HRESULT `0x8007048F` (`ERROR_DEVICE_NOT_CONNECTED`), lançada pelo próprio motor (`engine+0x89485`, seu `_CxxThrowException`).
- **A origem:** o motor emula `Windows.Xbox.Speech.Recognition.SpeechRecognizerFactory`, o reconhecimento de voz do Kinect, como dispositivo não conectado (`0x14009dda0`, vtable `0x140259818`).
- **O que o jogo espera:** no Windows, o jogo captura essa exceção e segue sem comandos de voz. O host não sabia entregar exceções aos blocos `catch`.

O 1.00:
- **Tratamento de exceções do Windows x64** (`Exceptions.cpp`, pontos de entrada em `guest_abi.c`), como o ntdll e o Wine fazem:
  - `RaiseException` e `RtlUnwindEx` entram por código de máquina que guarda os registradores do chamador como `CONTEXT`.
  - O despacho percorre os quadros pelas tabelas de unwind (motor, DLLs e imagens com tabela registrada, como o jogo) e chama o manipulador de cada quadro: `__CxxFrameHandler3` e `__C_specific_handler` do msvcr110, ou os do motor.
  - `RtlVirtualUnwind` foi implementado: códigos de unwind, epílogos, informação encadeada e quadro de máquina.
  - `RtlUnwindEx` desempilha até o quadro de destino, chamando os manipuladores de término (destrutores). O bloco `catch` roda por `STATUS_UNWIND_CONSOLIDATE`, e os registradores são restaurados no ponto de continuação. `longjmp` também é suportado.
  - Os trampolins do host guardam o contexto onde a busca continua, então o desempilhamento atravessa o código do host. Isso inclui `throw;` dentro de um `catch`.
  - Uma exceção que nenhum quadro trata continua com o relatório e a parada do 0.99.
- **`RtlPcToFileHeader`** também reconhece imagens com tabela registrada (o jogo). **`RtlAddFunctionTable`** registra cada tabela no log.
- **Carregador:** aceita DLL sem ponto de entrada (`/noentry`), como o Windows.
- **Teste no Linux:** `host/tests/seh/` gera `sehtest.dll` com clang para `x86_64-pc-windows-msvc` (tabelas de C++ do MSVC, ligado ao msvcr110 de verdade). A verificação `host --d3d12` chama `sehTest()`, que passou nos cinco casos (31): throw/catch, destrutores em quadros intermediários, rethrow, `catch(...)` e `__try/__except`.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 0.98 com Start: o som do jogo começou.**
- O XAudio2 do host funcionou. O FMOD criou o motor, a voz principal (2 canais, 48 kHz) e cinco vozes PCM de 16 bits, e a porta `sceAudioOut` abriu.
- O jogo fez `ResizeBuffers` para 1920x1080 e continuou carregando.
- Uns sete segundos depois, a tela congelou: a thread 344 (inicialização do jogo) lançou uma exceção C++/CX `Platform::COMException`, e o host, que ainda não trata exceções do Windows, parou o motor. A mensagem do log saiu ilegível, porque o host lia o objeto como `std::exception`.
- Pouco antes, o jogo procurou `KERNEL32!GetCurrentPackageId`, que o host não tinha.

O 0.99:
- **Relatório de exceção C++:** registra quem lançou (endereços de código na pilha) e os bytes do objeto lançado e do objeto apontado, onde fica o HRESULT de um `COMException`. A mensagem só é lida de tipos `std::exception`.
- **`GetCurrentPackageId`, `GetCurrentPackageFullName` e `GetCurrentPackageFamilyName`** devolvem `APPMODEL_ERROR_NO_PACKAGE` (15700), como para o motor no Windows, que roda como programa de desktop.

Próximo teste: `title-mode=host`, Start; klog sendo capturado. Se a exceção voltar, o relatório mostra o HRESULT e o ponto de lançamento.

---


**Teste do 0.97 com Start:** a primeira tela apareceu de novo, e o `VirtualQuery` não parou mais o motor. A parada seguinte foi "Cannot load desktop dependency xaudio2_9.dll". O FMOD (`fmod_os_misc.cpp`) carrega o `xaudio2_9.dll` e pede `CreateXAudio2Object`, `X3DAudioInitialize`, `X3DAudioCalculate`, `CreateFX`, `CreateAudioVolumeMeter` e `CreateAudioReverb`. O motor responde esses seis nomes com funções próprias (`0x14006d84e`):
- **`CreateXAudio2Object`** (`0x140141720`): carrega o `xaudio2_9.dll` do Windows, chama `XAudio2Create` e embrulha o objeto (vtable `0x140263488`). O wrapper só aceita vozes PCM/float sem efeitos nem envios, e repassa os callbacks do FMOD ao XAudio2 do Windows. `CreateSubmixVoice` para o motor.
- **As outras cinco** são paradas de diagnóstico do motor.

Ou seja, o FMOD usa o XAudio2 como saída de som. O 0.98:
- **XAudio2 2.9 no host** (`win32/XAudio2.cpp`, vtables em `win32/xaudio2_abi.c`, export `xaudio2_9!XAudio2Create`): motor, voz principal e vozes de origem.
  - Formatos: PCM de 8, 16, 24 e 32 bits, e float.
  - Buffers com região de reprodução e laços, `GetState`, volumes, volumes por canal, matriz de saída e razão de frequência.
  - Callbacks: `OnVoiceProcessingPassStart/End`, `OnBufferStart/End`, `OnLoopEnd` e `OnStreamEnd`.
  - Efeitos e filtros são aceitos e não aplicados (o log avisa).
- **Thread do mixer.** É uma thread do motor criada com `CreateThread`, para poder chamar os callbacks do FMOD com a convenção do Windows. A cada 10 ms ela mistura as vozes na voz principal, convertendo a taxa e passando pela matriz. O resultado vai para um fluxo do host na saída de áudio do 0.90 (`hostAudioStreamOpen` em `Audio.cpp`, quatro passos de latência).
- **Teste no Linux:** a verificação `host --d3d12` cria um motor, a voz principal e uma voz float estéreo com callback, e envia três buffers de 480 quadros. Passou: 3 `OnBufferEnd` e 1440 amostras tocadas.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 0.96 com Start: a primeira tela do jogo apareceu.** A fábrica WIC foi criada, e o swap chain fez o primeiro `Present` (`DXGI: Present (primeiro) = 0`). A imagem do jogo foi para a TV pelo WSI do RADV. Uns dois segundos depois, o host parou o motor na thread 400 ("Azimuth worker"): "VirtualQuery fora da memoria do VirtualAlloc: 0x213338000". Esse endereço era de um bloco de 2368 KiB (`0x213330000`) que o motor tinha acabado de liberar. No Windows, `VirtualQuery` responde para qualquer endereço; o host só respondia para memória do próprio `VirtualAlloc`. O 0.97:
- **`VirtualQuery` fora das regiões do `VirtualAlloc`** responde como o Windows:
  - memória livre (`MEM_FREE`), com o tamanho até a próxima região;
  - páginas de imagem confirmadas (`MEM_IMAGE`) no motor e nos DLLs carregados;
  - memória privada confirmada (`MEM_PRIVATE`, leitura e escrita) no heap do host, entre `0x6000000000` e `0x7000000000`.

  As dez primeiras consultas de memória livre vão para o log.
- **Teste de memória:** `make test-memory` agora cobre a divisão de placeholder em 16 KiB (0.93) e o `VirtualQuery` de memória liberada.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 0.95 com Start:** o motor parou com "GetProcAddress unsupported export=WICCreateImagingFactory_Proxy at game+0x112dd34". Para o jogo, o motor atende `LoadLibraryExW("WindowsCodecs.dll")` em `0x14006d490`:
1. carrega o DLL e guarda o identificador em `0x1402cfce0` (`0x1402cfce8` para o `xaudio2_9`);
2. devolve ao jogo o módulo que *contém* esse identificador, via `GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)`.

Depois, o `GetProcAddress` do motor (`0x14006d939`) só aceita `WICCreateImagingFactory_Proxy` se o módulo for o guardado. Num DLL de verdade, o endereço base pertence ao próprio DLL. O identificador de uma biblioteca do host, porém, não é um endereço dentro de um módulo. O `GetModuleHandleExW` do host falhava, o jogo recebia NULL e chamava `GetProcAddress(NULL, …)` assim mesmo. O 0.96:
- **`GetModuleHandleExW(FROM_ADDRESS)` reconhece o identificador de uma biblioteca do host** e o devolve, como o Windows faz com o endereço base de um DLL.
- **`GetModuleFileNameW` dessas bibliotecas** devolve `C:\Windows\System32\<nome>.dll`.
- **Teste no Linux:** `make test-wic` agora começa pela sequência do motor (`LoadLibraryExW`, `GetModuleHandleExW(FROM_ADDRESS)`, `GetProcAddress`); 18 verificações, todas passando.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 0.94 com Start:** a divisão de placeholder passou, e a inicialização gráfica foi adiante. O motor parou com "Cannot load desktop dependency windowscodecs.dll: 0x7e". O próprio jogo carrega `WindowsCodecs.dll` (WIC, o decodificador de imagens do Windows) e chama `WICCreateImagingFactory_Proxy`. O motor atende esse pedido carregando o DLL do Windows e encerra se não o encontrar. A fábrica não podia simplesmente falhar: o jogo a guarda num global (`0x143660b20`) e decodifica imagens da memória sem conferir se ela é nula (`0x14112e4a0`). A sequência é:
1. `CreateStream` e `InitializeFromMemory`;
2. `CreateDecoderFromStream`, `GetFrame(0)`, `GetSize` e `GetPixelFormat`;
3. pela tabela de formatos do jogo (DXGI 28, 87 e 88), `CopyPixels` direto ou `CreateFormatConverter`, `Initialize` e `CopyPixels`.

O 0.95:
- **WIC no host** (`win32/Wic.cpp`, vtables em `win32/wic_abi.c`, export `windowscodecs!WICCreateImagingFactory_Proxy`): fábrica, stream de memória, decodificador, quadro e conversor.
- **Decodificação com stb_image** (`host/third_party/stb/stb_image.h`, v2.30, domínio público): PNG, JPEG, BMP, TGA e GIF, em 32bpp RGBA, com a memória do alocador do host. O conversor dá RGBA, BGRA, BGRX, RGBX e os pré-multiplicados (PRGBA, PBGRA).
- **Imagens que não decodificam** vão para o log com o tamanho e os primeiros bytes; os métodos que faltam aparecem como `E_NOTIMPL`.
- **Teste no Linux:** `make test-wic` faz a sequência do jogo pelas vtables Windows sobre um PNG gravado pelo host, com 15 verificações, todas passando.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 0.93 com Start: o swap chain foi criado.**
- O host fechou a porta VideoOut do launcher. O driver tem uma tela "PS5 VideoOut" (3840x2160 a 59,94 Hz), e o `vkCreateDisplayPlaneSurfaceKHR` funcionou nela.
- O vkd3d-proton criou o swap chain de 2560x1440 (formato 24, 3 buffers, `FLIP_DISCARD`, flag de latência).
- Logo depois, o motor parou com "graphics placeholder split error=0x57" (`azimuth+0x7e970`). Ele reservou um placeholder de 73856 KiB em `0x20daf0000` e ocupou 512 vistas de 64 KiB do início. Em seguida pediu `VirtualFree(0x20faf0000, 41072 KiB, MEM_RELEASE|MEM_PRESERVE_PLACEHOLDER)`, que deixa 16 KiB no fim. O host só aceitava divisões em múltiplos de 64 KiB; o Windows divide em páginas.

O 0.94:
- **Divisão de placeholder em páginas**, como no Windows (`Memory.cpp`). A divisão só mexe na tabela de regiões. A proteção continua por bloco de 16 KiB quando a parte é ocupada.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 0.92 com Start:** os nomes de thread passaram (`BinkAsy1`, `BinkAsy3`), e o jogo chegou a criar o **swap chain**, o objeto que põe os quadros renderizados na tela. O motor chamou `IDXGIFactory::CreateSwapChainForHwnd` na fila D3D12. O DXGI do host ainda não tinha esse método (`E_NOTIMPL`), e o motor mostrou "Azimuth stopped: CreateSwapChainForHwnd on the D3D12 queue HRESULT=0x80004001" e encerrou. O 0.93:
- **Swap chain DXGI** (`host/src/gpu/SwapChain.cpp`, vtables em `dxgi_abi.c`):
  - `CreateSwapChainForHwnd` e `CreateSwapChain` pedem à fila o `IDXGIVkSwapChainFactory` do vkd3d-proton 3.0.1. Ele implementa a metade D3D12 do swap chain, como faz para o DXVK.
  - O resultado vira um `IDXGISwapChain4` (`Present`/`Present1`, `GetBuffer`, `ResizeBuffers`/`ResizeBuffers1`, `GetCurrentBackBufferIndex`, latência, espaço de cor, HDR, descrições).
- **Superfície da tela.** O vkd3d-proton recebe a superfície Vulkan do `IDXGIVkSurfaceFactory` do host. O RADV do Playden tem um WSI próprio sobre o VideoOut (`wsi/videoout`), que faz escala, ritmo e flips. O host liga `VK_KHR_display` e `VK_EXT_headless_surface` na instância quando o driver os oferece. Usa uma superfície de plano de tela se o driver tiver uma tela chamada "VideoOut"; senão, uma superfície headless. O log registra as telas e a escolha.
- **Passagem da tela.** Antes de criar a superfície, o host fecha a porta VideoOut que mostrava o launcher (`video::release`), para que o WSI do driver abra a sua.
- **`ntdll!NtDelayExecution`**, que o swap chain do vkd3d-proton usa para dar o ritmo dos quadros.
- **Teste no Linux:** a verificação `host --d3d12` agora cria um swap chain de 1280x720, pega o buffer 0 e apresenta três vezes. Passou com o llvmpipe, numa superfície headless.

Próximo teste: `title-mode=host`, Start; klog sendo capturado (as mensagens `wsi/videoout` do driver devem aparecer nele).

---


**Teste do 0.91 com Start:** o `D3DCompile` respondeu com os shaders pré-compilados (o log não registrou nenhuma falta). O motor parou na thread 416 por `RaiseException` com código `0x406D1388` (`MS_VC_EXCEPTION`). É o jeito do Visual C++ de dar nome a uma thread para o depurador: a exceção é lançada dentro de um `__try` cujo `__except` só a engole. Sem depurador, o programa simplesmente segue. O 0.92:
- **`RaiseException` com mensagens para o depurador:** `0x406D1388` (nome de thread) e `0x40010006`/`0x4001000A` (`OutputDebugString`) agora retornam sem parar o motor, o mesmo efeito do tratamento do Windows quando não há depurador. O nome dado à thread vai para o log.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 0.90 com Start:** o áudio funcionou: o `MMDeviceEnumerator` foi criado e o jogo seguiu. O motor criou o dispositivo D3D12 (as threads `vkd3d_fence`, `vkd3d_queue`, `Azimuth queue` e `Azimuth completion` subiram) e parou num import ainda sem implementação: `D3DCOMPILER_47.dll!D3DCompile`, chamado de `engine+0x10d283` na thread 380. O motor compila em tempo de execução alguns shaders HLSL fixos, que estão dentro do próprio `azimuth.exe`:
- o compute shader `detile` (`cs_5_0`);
- um vertex shader de tela cheia (`vs_5_0`);
- três pixel shaders (`plain`, `curved` e `ps`, todos `ps_5_0`).

O 0.91:
- **`D3DCompile` com shaders pré-compilados.** Os cinco shaders foram compilados neste PC com o `d3dcompiler_47.dll` do Windows (`host/tools/precompile_shaders.py`). O DXBC resultante está em `host/src/win32/CompiledShaders.inc`, com cerca de 7 KB.
- **Como o host responde:** procura o shader pelo SHA-256 do código-fonte, do ponto de entrada, do alvo e das flags, e devolve um `ID3DBlob` constante (`guest_abi.c`). Qualquer outro código-fonte vai para o log e falha com `E_FAIL`.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 0.89 com Start:** a correção do `DebugInfo` destravou a inicialização, e o jogo avançou até abrir o áudio. A thread 344 caiu escrevendo no endereço 0 (`0x140ff5d00`, base preferida do jogo). O jogo chamou `CoCreateInstance(MMDeviceEnumerator, IMMDeviceEnumerator)`, recebeu `REGDB_E_CLASSNOTREG` e usou o ponteiro nulo sem conferir. No Windows, o próprio motor (`azimuth.exe`, `0x1401444b0`) cria o `MMDeviceEnumerator` real e o embrulha num objeto seu, que oferece ao jogo a interface de áudio do Xbox (`8b557adc-555e-47a0-b223-43477e481dad`). Esse objeto pede ao dispositivo padrão um `IAudioClient` e chama `GetMixFormat`. O 0.90:
- **WASAPI no host** (`win32/Audio.cpp`, vtables em `win32/audio_abi.c`):
  - `MMDeviceEnumerator` com um único dispositivo de saída, "PlayStation 5" (`IMMDevice`, `IPropertyStore` com nome e formato).
  - `IAudioClient`/`IAudioClient2`, com `IAudioRenderClient`, `IAudioClock`, `ISimpleAudioVolume` e `IAudioStreamVolume`.
  - Formato de mixagem: 48 kHz, estéreo, float de 32 bits.
- **Thread de saída:** soma os fluxos em blocos de 256 quadros, convertendo a taxa e reduzindo 5.1/7.1 para estéreo, e manda cada bloco para `sceAudioOutOutput`. A porta é a principal do usuário do sistema, 48 kHz, 16 bits estéreo, a mesma receita que o fh-ps5 validou no console; a chamada bloqueia e dá o ritmo. Depois de cada bloco, sinaliza o evento de cada fluxo. Se a porta não abrir, a thread segue em silêncio, no ritmo do relógio.
- **`CoCreateInstance`** agora registra no log o CLSID das classes que recusa.
- **Título** passa a ligar `libSceAudioOut.so`.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 0.88 com Start — causa do travamento.** As threads 344–364 giram todas no mesmo laço do jogo (`0x1413d0780`, base preferida). É uma trava com tempo limite: `TryEnterCriticalSection` + `SwitchToThread`, medida com `QueryPerformanceCounter`. Antes de tentar, o jogo confere se o primeiro campo da `CRITICAL_SECTION` (`DebugInfo`) é diferente de zero, e faz o mesmo antes de `EnterCriticalSection` (`0x1413d08bf`). O host deixava `DebugInfo` = 0, então o jogo considerava a seção não inicializada e nunca entrava. A thread 344 não chegava a sinalizar o evento `0x14354f7f8` que a thread principal esperava. O 0.89:
- **`InitializeCriticalSection(Ex/AndSpinCount)` grava `DebugInfo = -1`**, como o Windows 8+ e o Xbox One fazem ("sem informação de depuração").

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 0.87 com Start:** o relatório de espera longa mostrou o que trava. O jogo foi carregado em `0x205200000`, porque o motor ocupa `0x140000000`. Os endereços abaixo estão na base preferida do jogo, `0x140000000`.
- **Thread principal 256:** espera em `0x140f6a2c0` (via `0x1410887a0`, `WaitForSingleObject` da kernelx) um evento manual global do jogo em `0x14354f7f8`. Esse evento só é sinalizado em `0x140f64f61` (`0x1410868f0`, `SetEvent`), no meio de uma grande função de inicialização.
- **Thread 368:** espera um evento automático criado pela thread 344 (`0x14107cda0`, `CreateEventA`).
- **Thread 344:** é a primeira thread do jogo (pilha de 512 KiB) e fica girando em `SwitchToThread` + `QueryPerformanceCounter`. Provavelmente a inicialização está nela, presa num laço de espera ativa.

O 0.88 é de diagnóstico:
- **Relatório de giro:** uma thread que chama `SwitchToThread` ou `Sleep(0)` 100 000 vezes (e depois a cada 10×) registra de onde chama.

Próximo teste: `title-mode=host`, Start; esperar uns 30 s depois que parar; klog sendo capturado.

---


**Teste do 0.86 com Start:** nenhuma falha e nenhuma mensagem; o título continuou rodando, mas parado. A thread principal do jogo (256) ficou em `WaitForSingleObject` para sempre, logo depois de `CoCreateFreeThreadedMarshaler`, e a thread 368 também (depois de `InitOnceExecuteOnce`). As threads 344–364 só giram em `SwitchToThread` (esperando trabalho), e a 360 está em `WaitOnAddress`. As pastas `Mount/media/Stripped/StringTables/EN` e `Mount/media/ui/fonts` não existem no pendrive (o sistema de arquivos não diferencia maiúsculas, então não é isso; devem estar dentro dos `.zip` do jogo). O 0.87 é de diagnóstico:
- **Relatório de espera longa:** uma espera de mais de 10 s registra no log o que se espera (evento manual/automático e se está sinalizado, thread e se foi iniciada, semáforo, mutex e dono), quem criou o objeto (thread e endereços de código) e de onde veio a espera.

Próximo teste: `title-mode=host`, Start; esperar uns 30 s depois que parar; klog sendo capturado.

---


**Teste do 0.85 com Start:** sem falha de memória; as threads do jogo rodaram. O motor foi parado pelo host na thread 348: o jogo pediu `VirtualAlloc` com tipo `0x203000`, isto é, `MEM_WRITE_WATCH` (rastreamento de páginas escritas). O 0.86:
- **`MEM_WRITE_WATCH` aceito.** O host não rastreia escritas: `GetWriteWatch` informa todas as páginas do intervalo como escritas (o chamador só relê mais do que precisaria), e `ResetWriteWatch` foi adicionado.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 0.84 com Start:** o jogo passou de `CoCreateFreeThreadedMarshaler` e criou suas threads. Uma delas caiu (sinal 11) dentro do próprio host: a função que anota no log quem chamou `VirtualAlloc` lia 512 palavras da pilha a partir do quadro atual e, numa thread nova com pilha pequena (64 ou 128 KiB), passou do topo. O 0.85:
- **Varredura da pilha limitada ao topo da thread** nos logs de `VirtualAlloc` (Memory.cpp) e de `HeapAlloc` (Kernel32.cpp), como já era no tratador de falhas.
- **Eventos em OVERLAPPED:** leituras e escritas com OVERLAPPED terminam antes de retornar, e agora sinalizam o `hEvent` da estrutura (antes só iam para o log).

Próximo teste: `title-mode=host`, Start; klog sendo capturado.

---


**Teste do 0.83 com Start:** a realocação funcionou. As reservas do jogo em `0x60000000000`, `0x60300000000` e `0x800000000` foram realocadas, não houve mais corrupção, e **o código do jogo rodou** até chamar `combase.dll!CoCreateFreeThreadedMarshaler` (`game+0x216dc7`), que o host não tinha; o motor parou ali. Efeito colateral: a reserva da imagem do jogo em `0x140000000` (59 MB) também foi realocada, com alinhamento de 1 GiB, e falhou por falta de memória. O 0.84:
- **Realocação só para reservas grandes:** de 1 GiB ou mais, as do gerenciador de memória do jogo. As menores voltam a falhar como antes, e o motor usa o plano B dele para a imagem.
- **`CoCreateFreeThreadedMarshaler`:** um objeto que responde só a `IUnknown`, suficiente dentro de um mesmo processo.
- **Demais funções que o motor procura para o jogo:** `CoCreateGuid` (GUID aleatório v4), `BCryptGenRandom`, `RtlGetVersion` (Windows 10 19045), `GetTempPath2W`, `LCIDToLocaleName` e **portas de conclusão de E/S** (`CreateIoCompletionPort`, `PostQueuedCompletionStatus` e um `GetQueuedCompletionStatus` que de fato espera pacotes). `CreateStreamOnHGlobal`, `CoMarshalInterThreadInterfaceInStream` e `CoGetInterfaceAndReleaseStream` devolvem `E_NOTIMPL` com log. Fibras e `SuspendThread` continuam ausentes.

Próximo teste: `title-mode=host`, Start; klog sendo capturado.


**Teste do 0.82 com Start — causa encontrada.** Na falha, o objeto do pool do jogo (`rdi = 0x208862f88`) tinha **tamanho total 0** e **contagem 0**, com blocos de 64 KiB. Com contagem zero, `count − 1` dá `0xFFFFFFFFFFFFFFFF` em aritmética sem sinal, e o laço que monta a lista livre (`0x14125ef40`) escreve registros de 10 bytes sem fim a partir de uma tabela mínima. Seguindo para trás no executável do jogo: `0x14125ed70` divide a região de memória em pools, e a região vem do gerenciador de memória do jogo (`0x14125f220`). Ele reserva, via `0x14125fcb0`, regiões nos endereços fixos do Xbox `0x60000000000` (6 TiB) e `0x800000000`. São as reservas de 12 GiB e 4 GiB que o log atribuía ao motor (`engine+7e597`, a emulação de `VirtualAlloc` da `kernelx`). No console esses endereços ficam fora da área de mapeamento do título; recusada a reserva, a região fica com tamanho zero.

No 0.83, quando uma reserva com endereço fixo cai fora da área do título (`0x1000000000`-`0xFC00000000`), o host a **realoca**: reserva o mesmo tamanho em qualquer lugar da área, com o alinhamento que o endereço pedido tinha (até 4 GiB), e devolve o endereço novo. O jogo guarda o endereço devolvido pelo `VirtualAlloc`. O log registra cada realocação ("Reserva em ... fora da area do titulo: realocada"). Próximo teste: `title-mode=host`, Start; klog sendo capturado.


**Teste do 0.81 com Start:** o registro imediato pegou o autor da varredura. A thread 256 escreveu em `0x6007ff0002`, os primeiros bytes da guarda no fim da arena 2, com a instrução em `0x20645efda`. Esse endereço fica **dentro da imagem do jogo** (`forza_durango_release_final.exe`, copiado para `0x205200000`, RVA `0x125efda`). A função do jogo (`0x14125ef40`) inicializa um pool de blocos: `count = total / tamanhoDoBloco`, pede `count × 10` bytes com `HeapAlloc(GetProcessHeap(), 0, ...)` e grava `índice+1` (16 bits) a cada 10 bytes. O código do jogo está correto; o bloco recebido era menor que o pedido. As duas funções vêm da `kernelx.dll` do Xbox, que o motor emula, então o pedido passa pelo heap do motor antes de chegar ao host. O 0.82 registra:
- **Na falha:** todos os registradores e 16 palavras em `rdi` (o objeto do pool: total, tamanho do bloco, contagem, tabela), sem alocar memória.
- **Em `HeapAlloc`:** toda chamada de 1 MiB ou mais, com o resultado e os endereços de código na pilha (motor, jogo e DLLs).

Próximo teste: `title-mode=host`, Start; klog sendo capturado.


**Teste do 0.80 com Start (klog capturado):** o kernel registrou a morte: SIGSEGV de escrita em página ausente em `0x6003fffe20`, com o ponteiro de pilha nesse mesmo endereço, logo abaixo da arena 2 (`0x6004000000`). A pilha de chamadas é o tratador de falhas do host (`onFault`) chamando a si mesmo sem fim. O tratador falhava ao tratar uma falha porque `describeModuleAddress` monta uma `std::string` (aloca na arena corrompida) antes de marcar que já está tratando uma. A pilha alternativa, um `new` de 256 KiB no começo da arena 2, esgotou-se na recursão e o kernel encerrou o título. Era isso que vinha matando o título em silêncio. O 0.81:
- **Sem reentrada:** "dentro do tratador" é marcado antes de tudo. Uma falha dentro dele grava uma mensagem fixa e estaciona a thread.
- **Registro imediato:** antes de qualquer alocação ou trava, grava direto no arquivo (`write`) uma linha com sinal, instrução (e posição no motor), endereço acessado e pilha. A falha original fica registrada mesmo que o resto falhe.
- **Pilha alternativa por `mmap`:** fica fora das arenas.
- **Guardas conferidas:** a guarda no fim de cada arena é conferida; se o kernel recusar, o log diz.

Próximo teste: `title-mode=host`, Start; klog sendo capturado. A linha "FALHA (registro imediato)" deve mostrar a instrução do motor que faz a varredura.


**Teste do 0.79 com Start (com klog):** as arenas foram para `0x6004...`-`0x600a...` e a varredura de registros de 10 bytes **apareceu lá do mesmo jeito**. O motor não escreve num endereço fixo: escreve a partir de um ponteiro que o próprio host entregou, muito além do tamanho pedido. Como nenhuma guarda de bloco grande foi tocada, a tabela foi alocada pequena (menos de 1 MiB, dentro de uma arena) e preenchida como se tivesse dezenas de MB. Algum tamanho que o motor calcula para alocá-la sai errado no host, enquanto o laço de preenchimento usa o tamanho real do heap. Depois disso vieram falhas em cascata no host e a parada; o título mostrou "teste terminado, codigo 0". O 0.80 termina cada arena de 64 MiB numa guarda sem acesso. A varredura bate nela dentro do código do motor, e o log mostra a instrução e a pilha do motor que escrevem. Próximo teste: `title-mode=host`, Start, esperar uns 30 s; klog sendo capturado.


**Teste do 0.78 com Start (com klog):** nenhuma guarda foi atingida, então não é estouro de bloco grande. Comparando os valores do contador em blocos distantes (`0x20b441ef6` → `0xdd60`, `0x20b481006` → `0x4248`: 258.320 bytes = 25.832 registros, e `0xdd60+25832 ≡ 0x4248`), é **uma única varredura linear** de registros de 10 bytes, com índice de 16 bits, atravessando dezenas de MiB entre `0x208000000` e `0x20c000000`, onde estavam as arenas do host. O tamanho bate com uma tabela de páginas do heap gráfico de 20 GiB (5.242.880 páginas de 4 KiB × 10 bytes ≈ 52 MB). Hipótese: o motor monta essa tabela num endereço que, no console, cai sobre a memória do host; a origem do endereço ainda não se sabe. O 0.79 pede ao kernel que ponha as arenas a partir de `0x6000000000` e os blocos grandes a partir de `0x6800000000`, longe dessa faixa; o log mostra onde cada arena ficou. Se a varredura continuar mirando `0x208...`, ela cai fora da memória do host (ou falha, com o ponto do motor no log). Próximo teste: `title-mode=host`, Start, esperar uns 30 s; klog sendo capturado.


**Teste do 0.77 com Start (com klog):** os blocos corrompidos mostram o mesmo padrão em todos os casos: só 2 bytes a cada 10 foram alterados, com um contador de 16 bits crescente (`6d61`, `6d62`, `6d63`...; `66e6`, `66e7`...), e os bytes entre eles ficaram intactos. É uma varredura linear que escreve um campo de um vetor de registros de 10 bytes. Ela atravessa a arena inteira e atinge blocos livres e também blocos em uso do host: os "free de ... que nao e inicio de bloco" são cabeçalhos estragados. Acontece logo depois das divisões do heap gráfico, enquanto a thread principal do motor roda sem chamar o host. Hipótese: o motor escreve além do fim de uma tabela grande alocada com `HeapAlloc` (os blocos de 8 MiB e 1 MiB que aparecem a cada divisão). No console esses blocos e as arenas são mapeamentos de memória direta que o kernel pode pôr lado a lado. O 0.78 põe uma **guarda** de 64 KiB sem acesso logo depois de cada bloco grande e alinha o bloco ao fim da área útil. Um estouro vira falha imediata no código que escreve, com o ponto do motor no log, e as arenas ficam protegidas. Próximo teste: `title-mode=host`, Start, esperar uns 30 s; o klog está sendo capturado.


**Teste do 0.76 com Start:** a thread 276 (cache de pipelines do vkd3d) achou um bloco livre de 64 bytes escrito depois do `free`. Os 48 bytes mostram um contador de 16 bits crescente (`0b99`, `0b9a`, ... `0b9d`) a cada 10 bytes, mais um `1.0f`: um vetor de registros de 10 bytes escrito por cima do bloco, por ponteiro antigo ou por estouro de um bloco vizinho; o autor ainda não se sabe. Logo depois, a mesma thread teve uma falha, e o tratador falhou também: percorria 256 palavras da pilha a partir do ponto da falha e passou do topo da pilha (`0x7ee04c000`), então a falha original não foi registrada. No 0.77 a varredura para no quadro do host da própria thread, o mesmo limite já usado pelo amostrador no 0.49. Próximo teste: `title-mode=host`, Start, esperar uns 30 s; o klog está sendo capturado.


**Teste do 0.75 com Start:** nenhum "free de ponteiro que não é início de bloco", mas de novo blocos "escritos depois do free" em quase todas as classes de tamanho, detectados logo depois das divisões do heap gráfico (como no 0.74). Nesse trecho o host e o motor trocam blocos grandes de memória direta (as tabelas de páginas das regiões). O 0.76:
- **Liberação de blocos grandes:** confere `sceKernelMunmap` antes de devolver a memória física com `sceKernelReleaseDirectMemory` e registra as falhas. Se a memória fosse devolvida ainda mapeada, poderia voltar como outro bloco e o endereço antigo ficaria apontando para ele.
- **Conteúdo escrito:** num bloco escrito depois do `free`, o log mostra os primeiros 48 bytes em hexadecimal e como texto, para identificar o que foi escrito.
- **Quarentena:** os blocos pequenos liberados esperam numa fila de 4096 por arena antes de serem reusados, como acontece na prática no heap do Windows. Uma escrita tardia cai num bloco que ainda não tem dono.

Também passa a valer a captura do log do kernel (klogsrv, porta 3232) durante os testes. Próximo teste: `title-mode=host`, Start, esperar uns 30 s.


**Teste do 0.74 com Start:** sem mensagens na tela, título aberto, despejos a cada 10 s funcionando. O alocador acusou blocos livres "escritos depois do free" em várias classes de tamanho. O registro de chamadas de metade das threads de trabalho (292, 300, 308, ...) está corrompido: ponteiros de nome alterados, apontando para o meio de textos do título, e a thread 300 aparece como "44441". Esses registros nunca são liberados, então algum bloco teve dois donos. Uma via concreta: quando o `heapFree` recebia um ponteiro sem a marca do alocador, ele o repassava ao `free` do sistema. Hoje nada do host usa o `malloc` do sistema, então isso só acontece com um ponteiro liberado pela função errada (um bloco de `HeapAlloc` no `free` da `msvcrt`, ou o contrário). O `free` do sistema então escrevia as próprias estruturas dentro das arenas. No 0.75 esses ponteiros são registrados ("free de ... que nao e inicio de bloco") e ignorados. A thread principal do motor continua viva: nas últimas chamadas ela alterna travas SRW e `SetLastError`, como num laço de espera. Próximo teste: `title-mode=host`, Start, esperar uns 30 s.


**Teste do 0.73 com Start:** nenhum erro na tela: o título ficou aberto. O alocador não acusou nenhum bloco escrito depois do `free` nem liberado duas vezes. O motor reservou e dividiu o heap gráfico e ficou em silêncio. Aos 12 s, a thread vigia caiu (SIGSEGV na `libkernel`) ao montar o despejo das últimas chamadas: um ponteiro de nome lido do registro das threads estava inválido. Agora ela só fica parada, e o título seguiu aberto. O 0.74:
- **Últimas chamadas:** valida cada ponteiro de nome antes de ler (um inválido vira "?") e deixa de mostrar a contagem de aninhamento, que no console nunca foi confiável.
- **Amostrador removido:** o diagnóstico que interrompia as threads com sinais causou duas quedas e quase nunca respondeu no console.
- **Despejos contínuos:** saem a cada 10 s durante toda a execução, com a medição de memória, para mostrar o que o motor faz depois de dividir o heap.

Próximo teste: `title-mode=host`, Start, esperar uns 30 s.


**Teste do 0.72 com Start:** apareceu "FPU not available". O log mostra a cadeia. Primeiro, um SIGSEGV dentro do `heapAlloc` do host, na thread vigia, ao ler `0x33c3095a6f80`: um ponteiro tirado da lista de blocos livres das arenas, ou seja, a lista estava corrompida. Depois, o tratador de falhas de uma thread do host chamava `_exit`, que um título não pode usar: veio SIGSYS, o tratador rodou de novo e tentou de novo, até estourar a pilha. Antes do 0.72 o SIGSYS nem era tratado. Essa é a explicação mais provável das mortes silenciosas desde o 0.68. O 0.73:
- **Arenas resistentes:** cada bloco livre guarda uma marca e uma verificação ligada ao próprio endereço. Ao reusar, um bloco escrito depois do `free` é detectado: a lista é descartada e o log diz qual bloco. Um bloco liberado duas vezes também é detectado e fica fora da lista. A marca de livre fica no lugar da marca do cabeçalho, que antes era apagada pela lista.
- **Tratador sem `_exit`:** numa thread do host, ou numa falha dentro do tratador, a falha é registrada e a thread fica parada para sempre. O título continua aberto, sem o laço de SIGSYS.

Próximo teste: `title-mode=host`, Start. O log deve dizer se o motor escreve em blocos depois de liberá-los.


**Teste do 0.71 com Start:** nenhuma chamada de memória falhou. O motor reservou o heap gráfico de 20 GiB em `0x1000000000`, fez as quatro divisões de 4 GiB, e o título morreu sem nenhuma outra chamada ao host e sem "FALHA FATAL". O tratador de falhas do host só cobria SIGSEGV, SIGBUS, SIGILL e SIGFPE, e rodava na pilha da própria thread. Uma instrução `int3` (`__debugbreak`, comum nos caminhos de erro do código MSVC) gera SIGTRAP, que matava o processo sem relatório; um estouro de pilha não deixava o tratador rodar. O 0.72 trata também SIGTRAP, SIGABRT e SIGSYS e dá a cada thread do motor uma pilha alternativa de 256 KiB para o tratador (`sigaltstack`, `SA_ONSTACK`). Launcher conferido no Linux. Próximo teste: `title-mode=host`, Start.


**Teste do 0.70 com Start:** a memória flexível estava folgada (360 MiB livres) até o fim, então não foi falta de memória. O motor reservou o heap gráfico de 20 GiB em `0x1000000000`, fez as quatro divisões de 4 GiB (`VirtualFree 0x8002`) e o título morreu logo depois, sem "FALHA FATAL". Hipótese: alguma chamada de memória do motor falhou em silêncio, e ele tocou numa faixa só reservada (`sceKernelReserveVirtualRange`), o que no console pode encerrar o processo sem passar pelo tratador de sinais. O 0.71 envolve `VirtualAlloc2` (e portanto `VirtualAlloc`), `VirtualFree`, `MapViewOfFile3` e `VirtualProtect`: toda falha vai para o log com os parâmetros, o código de erro e os endereços de retorno do motor, assim como toda chamada dentro de uma região só de endereços (até 200 linhas). Próximo teste: `title-mode=host`, Start; anotar o código de erro da tela.


**Teste do 0.69 com Start:** igual ao 0.68. O título sumiu logo depois de reservar e dividir o heap gráfico de 20 GiB, antes de ocupar qualquer placeholder, mapear a ESRAM ou colocar memória direta: nenhuma das linhas novas apareceu. A medição de memória flexível só rodava nos primeiros 12 s da execução, antes do Start. O 0.70 mantém a medição ativa a execução inteira (a cada 100 ms, quando muda 8 MiB ou mais) e registra cada `VirtualFree` de divisão ou junção de placeholder. Próximo teste: `title-mode=host`, Start; anotar o código de erro da tela.


**Teste do 0.68 com Start:** a reserva de 20 GiB do heap gráfico funcionou (placeholder em `0x1000000000`), e o motor o dividiu em pedaços de 4 GiB. Logo depois o título sumiu sem nenhuma linha de falha no log, como nas vezes em que a memória estourava. Os passos seguintes (ocupar um placeholder com `MEM_REPLACE_PLACEHOLDER`, colocar memória direta, mapear a ESRAM) não deixavam registro. O 0.69 registra cada placeholder ocupado (tamanho, commit e endereços de retorno do motor), cada commit com memória direta de 1 MiB ou mais (com o maior trecho livre de memória direta) e o fim de cada um de 64 MiB ou mais, já mapeado e zerado. Registra também cada `MapViewOfFile3`. Próximo teste: `title-mode=host`, Start; anotar o código de erro que o console mostrar.


**Teste do 0.67 com Start:** o console não desligou: as reservas fora da área do título foram recusadas no host. O log mostra a origem. As reservas de 12 GiB em `0x60000000000` e 4 GiB em `0x60300000000` são do próprio motor (`engine+7e597`), que tolera a recusa e segue. O heap gráfico contíguo (`engine+7ba30`) tenta 20 GiB fixos em `0x800000000` (recusado no host) e depois a alternativa: 20 GiB em qualquer lugar entre `0x800000000` e `0xFFFFFFFFFF`, alinhados em 4 GiB. Essa cabe na área do título, mas `sceKernelReserveVirtualRange` recusou (EINVAL), provavelmente pelo alinhamento de 4 GiB. O 0.68 reserva o tamanho mais o alinhamento com o alinhamento de bloco do próprio kernel e desfaz as sobras antes e depois do trecho alinhado. Próximo teste: `title-mode=host`, Start.


**Teste do 0.66 com Start:** não houve erro na tela, mas o **console inteiro desligou**. O log termina logo depois de duas reservas recusadas pelo kernel: 12 GiB em `0x60000000000` e 4 GiB em `0x60300000000` (cerca de 6 TiB), ambas com EINVAL (`0x80020016`) de `sceKernelReserveVirtualRange` com endereço fixo. O motor registrou de novo "contiguous graphics heap reservation error=0x8". São endereços do layout de memória do Xbox (2 a 6 TiB), válidos no Windows. Num título, porém, mapas com endereço fixo só cabem na área de mapeamento (`0x1000000000`-`0xFC00000000`, 64 GiB a 1008 GiB). A hipótese é que pedir ao kernel uma reserva fixa tão fora da área derrubou o console. O 0.67:
- **Protege o console:** recusa no próprio host, sem chamar o kernel, toda reserva fixa fora da área do título.
- **Respeita as faixas:** usa o menor e o maior endereço das exigências do `VirtualAlloc2` (não só o alinhamento), procurando a partir do início da área.
- **Registra a origem:** em cada reserva que falha, o log mostra a faixa pedida e os endereços de retorno do motor na pilha, para descobrir quem pede os endereços de 6 TiB (o motor ou o jogo).

O jogo ainda não deve passar daqui. Se o próprio jogo do Xbox depender desses endereços altos, será preciso um caminho diferente; o log deste teste diz qual. Próximo teste: `title-mode=host`, Start.


**Teste do 0.65 com Start:** o motor parou com "contiguous graphics heap reservation error=0x8". O código dele (`engine+0x7ba30`) pede ao `VirtualAlloc2` um placeholder de **20 GiB** (`0x500000000`) em `0x800000000`. Se falhar, tenta em qualquer lugar com exigência de endereço; depois divide em pedaços de 4 GiB. Um mapeamento anônimo de 20 GiB não cabe na memória flexível do título (448 MiB), e o que o jogo grava ali precisa vir da memória direta (12 GB). No 0.66, no console, toda reserva de 1 GiB ou mais só reserva endereços (`sceKernelReserveVirtualRange`). O commit dentro dela mapeia memória direta nova, zerada, em blocos de 16 KiB; decommit e liberação devolvem a memória e reservam os endereços de novo. Divisões, junções e mapeamentos de seção herdam esse modo. Reservas menores continuam como antes (inclusive as executáveis). No Linux nada muda: `make test-memory` passa e o launcher continua normal. Uma reserva que falhar agora vai para o log com tamanho, endereço e tipo. Próximo teste: `title-mode=host`, Start.


**Teste do 0.64 com Start:** o `xg_x.dll` executou (a proteção por bloco funcionou). O próprio motor parou com "Azimuth stopped: desktop memory API unavailable: VirtualAlloc2": ele procura em `KernelBase.dll` as APIs de memória do Windows 10 (`VirtualAlloc2`, `MapViewOfFile3`, `UnmapViewOfFile2`). Com elas, emula a memória do Xbox: o heap gráfico é uma reserva "placeholder" dividida e preenchida aos poucos, e a ESRAM é uma seção de memória compartilhada mapeada dentro de placeholders. O 0.65 implementa esse modelo no `win32/Memory.cpp`:
- **Placeholders:** `VirtualAlloc2` com `MEM_RESERVE_PLACEHOLDER`, `MEM_REPLACE_PLACEHOLDER` e exigência de alinhamento; `VirtualFree` com `MEM_PRESERVE_PLACEHOLDER` (divide, ou devolve memória privada a placeholder) e `MEM_COALESCE_PLACEHOLDERS` (junta).
- **Seções sem arquivo:** `CreateFileMapping` com `INVALID_HANDLE_VALUE` cria memória compartilhada (memória direta no console, `memfd` no Linux). `MapViewOfFile`/`MapViewOfFile3` a mapeiam em qualquer lugar ou dentro de um placeholder. `UnmapViewOfFile2` com `MEM_PRESERVE_PLACEHOLDER` devolve o placeholder.
- **Outros:** modificadores de cache (`PAGE_WRITECOMBINE`, `PAGE_NOCACHE`) são aceitos e ignorados; `KernelBase` é sinônimo da `kernel32`.

Novo teste `make test-memory` (`host/tests/MemoryTest.cpp`), que passa no Linux: placeholder, divisão, memória privada, seção vista em dois endereços com o mesmo conteúdo, remapeamento, junção, liberação e reserva alinhada. Launcher conferido no Linux. Próximo teste: `title-mode=host`, Start.


**Teste do 0.63 com Start:** os imports do `xg_x.dll` foram todos resolvidos. O motor protegeu as seções dele (`VirtualProtect` ×7), registrou a tabela de exceções (`RtlAddFunctionTable`) e saltou para o código em `0x20c78df84`. Falhou ali com `err 0x15`: busca de instrução numa página presente, mas não executável. É a granularidade de 16 KiB do console. O `VirtualProtect` do host protegia páginas de 4 KiB, mas o kernel aplica a última proteção ao bloco inteiro: o fim do código e o começo dos dados no mesmo bloco deixavam o código sem execução. O carregador de DLLs do host já resolvia isso; o 0.64 faz o mesmo em `VirtualAlloc`/`VirtualProtect`. Cada bloco tocado recebe a união das proteções das suas páginas. Se o kernel recusar um bloco gravável e executável ao mesmo tempo, o bloco fica executável e o log avisa. Launcher conferido no Linux com blocos de 16 KiB simulados. Próximo teste: `title-mode=host`, Start.


**Teste do 0.62 com Start:** a reserva corrigida funcionou (`VirtualAlloc` de 59 MiB com commit em `0x21c000000`, e a imagem do jogo foi copiada). O motor então carregou, com o carregador próprio, o `xg_x.dll`, uma biblioteca gráfica do Xbox que vem no jogo (`EmbeddedXvd/Windows/System32`). Ele resolve os imports dela nas bibliotecas de desktop e parou com "Missing XG dependency msvcrt.dll!vsprintf_s". Os imports do `xg_x.dll` são 10 de `msvcrt`, 3 de `ntdll` e 13 de `kernelx`, que o próprio motor emula. O 0.63 acrescenta no `msvcrt` `vsprintf_s`, `sqrt`, `__C_specific_handler` e `_XcptFilter` (os dois últimos só atuam no tratamento de exceções). Cria a biblioteca `ntdll` do host com `RtlCaptureContext`, `RtlLookupFunctionEntry` e `RtlVirtualUnwind`. `RtlLookupFunctionEntry` agora é real: procura nas tabelas registradas com `RtlAddFunctionTable` (que passam a ser guardadas) e na `.pdata` do motor e dos DLLs. `RtlVirtualUnwind` para com mensagem clara (o desempilhamento de exceções vem depois). Launcher conferido no Linux. Próximo teste: `title-mode=host`, Start.


**Teste do 0.61 com Start:** o motor procurou várias funções do `ws2_32` que o host não oferecia e depois falhou com SIGSEGV dentro do próprio `memcpy` (`engine+0x204ef3`), escrevendo em `0x20c79c020`, endereço sem mapeamento (SEGV_MAPERR), logo depois de um `VirtualAlloc`. Causa provável: no console o kernel mapeia e desmapeia blocos inteiros de 16 KiB. Numa reserva "em qualquer lugar", o host mapeia a mais e corta o excesso; quando o fim da reserva não caía num múltiplo de 16 KiB, o corte desfazia o bloco com as últimas páginas da própria reserva. O 0.62 mapeia cada reserva arredondada para 16 KiB (e libera o mesmo tamanho). Registra também todo `VirtualAlloc`/`VirtualFree` de 1 MiB ou mais, ou com endereço pedido. Acrescenta no `ws2_32`: `WSAStartup` (versão 2.2, sem rede), `WSACleanup`, `WSAGetLastError`/`WSASetLastError`, `htonl`/`htons`/`ntohl`/`ntohs`, `inet_addr`, `inet_ntoa`. Launcher conferido no Linux com blocos de 16 KiB simulados. Próximo teste: `title-mode=host`, Start.


**Teste do 0.60 com Start:** `msvcr110` e `msvcp110` carregaram e inicializaram. O motor então pediu `ws2_32.dll` e parou com "Cannot load desktop dependency ws2_32.dll". O executável lista as dependências de desktop e o que usa de cada uma: `ws2_32` (só os eventos `WSACreateEvent/CloseEvent/SetEvent/ResetEvent/WaitForMultipleEvents`), `vccorlib110` (`GetActivationFactoryByPCWSTR`), `mfplat` (`MFStartup`, `MFShutdown`, `MFCreateMediaType`, `MFInitMediaTypeFromWaveFormatEx`) e `mfreadwrite` (`MFCreateSourceReaderFromURL`); depois virão `xaudio2_9`, `WindowsCodecs`, `xinput1_4` e o `xg_x.dll` do jogo. O 0.61 oferece as quatro primeiras como bibliotecas do host (`win32/Desktop.cpp`). Os eventos do Winsock são eventos comuns. O Media Foundation inicializa, mas recusa criar mídia (E_NOTIMPL, com log). O Windows Runtime responde "classe não registrada" e registra o nome da classe pedida. Próximo teste: `title-mode=host`, Start.


**Teste do 0.59 com Start:** a correção do `RtlPcToFileHeader` funcionou, e a exceção chegou ao host com a mensagem: "Cannot open file to hash: C:\windows\system32\msvcr110.dll". O motor confere suas dependências calculando o hash do arquivo, e pede o caminho com `GetModuleFileNameW`. O host respondia com um caminho inventado em `system32`. O 0.60 devolve o caminho Windows verdadeiro para DLLs que estão numa pasta montada (`E:\PC\FH2\data\bin\msvcr110.dll`); os do vkd3d continuam em `system32`. Também acrescenta `LocaleNameToLCID` (procurado via `GetProcAddress`). Próximo teste: `title-mode=host`, Start.


**Teste do 0.58 com Start:** a `msvcr110` ficou com 158 de 194 imports e falhou com SIGSEGV em `msvcr110+0x214cb`, lendo `0xae538`, logo depois de `RtlPcToFileHeader`. Ao lançar uma exceção C++, a `msvcr110` pede a base do próprio módulo a `RtlPcToFileHeader` e soma a ela os deslocamentos da descrição da exceção. O host só reconhecia endereços do motor e devolvia base zero para a `msvcr110`. O 0.59 reconhece também os DLLs carregados. A exceção que a `msvcr110` lança deve então aparecer no log com o tipo e a mensagem (o tratamento de exceções do Windows ainda é uma parada de diagnóstico). Também implementa as funções que o log mostrou sendo procuradas via `GetProcAddress`: `CreateMutexA/W` + `ReleaseMutex` (mutex recursivo por thread), `CreateEventExW`, `ResetEvent`, `WaitForMultipleObjectsEx`, `CompareFileTime`. Fibras, portas de conclusão de E/S e `SuspendThread` continuam ausentes. Próximo teste: `title-mode=host`, Start.


**Teste do 0.57 com Start:** o `msvcr110.dll` carregou (106 de 194 imports no host). A primeira parada foi `GetThreadTimes`, chamada pela própria `msvcr110` na thread principal. O 0.58 implementa de uma vez as funções simples que a `msvcr110` e a `msvcp110` importam:
- **Threads:** `ResumeThread` (o `CreateThread` passa a aceitar `CREATE_SUSPENDED`, que o `_beginthreadex` usa; a thread só é criada no `ResumeThread`), `Set/GetThreadPriority` e `SetThreadAffinityMask` (aceitos, sem efeito), `GetThreadTimes`, `SetThreadStackGuarantee`.
- **Sincronização e sistema:** `CreateSemaphoreExW`, `SignalObjectAndWait`, listas `Interlocked*SList`/`QueryDepthSList`, `GetSystemInfo`, `GetLogicalProcessorInformation`, `GetNumaHighestNodeNumber`, `GetCurrentProcessorNumber`, `SetProcessAffinityMask`.
- **Tempo e versão:** `FileTimeToSystemTime` e conversões (o console é UTC, sem fuso), `GetLocalTime`, `GetTimeZoneInformation`, `GetTickCount`, `GetVersionExW` (Windows 10 19045).
- **Heap e localidade:** `HeapQueryInformation/Validate/Compact/Walk`, `CompareStringEx`, `IsValidLocaleName`, `EnumSystemLocalesEx`, `GetDate/TimeFormatEx` (recusam, e a biblioteca usa os formatos próprios).
- **Arquivos:** `GetFileInformationByHandle`, `GetFullPathNameA`, `Get/SetCurrentDirectoryA/W` (a pasta atual continua a do motor), `GetTempPathA/W` (`E:\PC\FH2\data\tmp`), `GetDriveTypeW`, `GetLogicalDrives`, `SetFileAttributesW`, `SetFileTime`, `LockFileEx/UnlockFileEx`, `LoadLibraryExA`, `SetErrorMode`, `OutputDebugStringW`, `DisableThreadLibraryCalls`.

Restam 38 imports da `msvcr110` como paradas de diagnóstico: timers e esperas do pool de threads (do agendador ConcRT), console, criação de processos e desempilhamento de exceções. Próximo teste: `title-mode=host`, Start.


**Teste do 0.56 com Start:** o `azimuth-tls.dll` passou. O motor então pediu `E:\PC\FH2\data\bin\msvcr110.dll`, a biblioteca C do Visual C++ 2012, e parou com "Cannot load desktop dependency msvcr110.dll: 0x7e" porque o carregador do host não a aceitava. O 0.57 aceita `msvcr110` e `msvcp110` (a biblioteca C++ do mesmo pacote, que depende da primeira). O `msvcr110` importa 194 funções do Windows; 89 o host ainda não tem e ficam como paradas de diagnóstico, que só param se forem chamadas. O `msvcp110` resolve as suas no próprio `msvcr110`. Próximo teste: `title-mode=host`, Start.


**Teste do 0.55 com Start:** o motor saiu do launcher e, antes de criar a swapchain, tentou carregar `E:\PC\FH2\data\bin\azimuth-tls.dll`. O host recusou ("nao disponivel no host"), e o motor parou com a exceção C++ "Cannot load the native static-TLS helper". Esse DLL é do próprio Azimuth: 4,6 KB, um bloco de TLS estático de 0x150 bytes, um callback de TLS que grava o id da thread em `azimuth_tls_attached`, e exporta `azimuth_tls_index`. Ele serve para o motor conferir que o carregador dá TLS estático aos módulos, como o Windows. O carregador do host só aceitava `d3d12` e `d3d12core`. O 0.56 aceita também `azimuth-tls` e procura DLLs também em `data\bin` do motor. O carregador já dava TLS por thread e rodava os callbacks dos módulos com diretório TLS. Próximo teste: `title-mode=host`, Start, esperar uns 20 s.


**Teste do 0.55 — cache funcionando:** o vkd3d-proton juntou o cache gravado pelas execuções anteriores ("Done merging shader caches, existing entries: 1, new entries: 2362", "Successfully replaced shader cache with merged cache") e mapeou o cache só de leitura em 0,2 ms. Os pipelines guardados são usados, e o launcher chega pronto muito mais rápido. Estado atual: launcher na TV, controle funcionando, sem fechamentos nem travamentos, cache de pipelines persistente. Próximo passo: Start para entrar no jogo (swapchain DXGI → VideoOut, XInput).


**Teste do 0.54:** continuou compilando tudo. O console repetiu `Failed to MapViewOfFile` para `vkd3d-proton.cache` e `.write`, sem nenhuma linha do host sobre o mapeamento. O vkd3d-proton confere o retorno de `CreateFileMappingA` contra `INVALID_HANDLE_VALUE`, não contra nulo: uma falha silenciosa do `CreateFileMapping` aparece como falha do `MapViewOfFile`. No 0.54 os caminhos sem log eram `fstat`, arquivo vazio e `dup`, e o `dup` (que no Linux funciona) é o suspeito no título. O 0.55 não usa mais `dup`: o mapeamento guarda o próprio objeto do arquivo, que fica aberto enquanto o mapeamento existir, como no Windows. Todo caminho de falha de `CreateFileMapping`/`MapViewOfFile` agora vai para o log. Conferido no Linux com o cache do console. Próximo teste: rodar duas vezes.

**Teste do 0.53:** sem erro. O launcher congela só no início da compilação dos pipelines e fica fluido quando ela termina. Mas os pipelines são compilados de novo a cada abertura. O log mostra o motivo: `err:vkd3d_file_map_read_only: Failed to MapViewOfFile for vkd3d-proton.cache`. O vkd3d-proton grava o cache (`vkd3d-proton.cache.write`, 125 KB no pendrive) e, na abertura seguinte, o lê mapeando o arquivo, mas `CreateFileMapping`/`MapViewOfFile` não existiam no host. Sem ler, ele também não junta o `.write` ao cache principal (que ficou com 344 bytes) e compila tudo de novo. O 0.54 implementa o mapeamento de arquivos só para leitura (`CreateFileMappingA/W`, `MapViewOfFile`, `UnmapViewOfFile`): a visão é uma cópia do arquivo na memória do host. Pedidos de escrita ou mapeamentos com nome são recusados e registrados. No Linux o vkd3d passou a mapear e ler o cache copiado do console (e o recusou só por ser de outro driver, como esperado). Próximo teste: rodar **duas vezes**. A primeira junta o cache gravado pelas anteriores; a segunda deve abrir sem compilar.

**Teste do 0.52:** o título fechou com erro no despejo dos 3 s, antes de "Amostras (1)". A causa é o próprio diagnóstico novo: ler os 16 bytes de código antes do endereço da chamada ao kernel. O código do sistema (`libkernel`) deve ficar em páginas só de execução, e a leitura falha na thread vigia. O 0.53 é o 0.52 sem essa leitura (o cache de fontes por nome de arquivo continua). Próximo teste: `title-mode=host`, mexer no menu durante a compilação e, terminada a compilação, tentar Start.

**Teste do 0.51:** igual, mesmo com a thread do launcher em prioridade 600. A amostra dela aponta para uma chamada ao kernel em `engine+0x25ddd`, logo após a chamada importada de `DrawTextW` (`0x1402236d0`). O launcher cria as fontes de novo a cada quadro, e o cache de fontes do host era indexado pelo caminho completo. Por isso, a cada fonte nova, ele voltava ao disco: `fopen` de `/data/azimuth-ps5/fonts/arial.ttf` (que não existe) antes de achar a do pendrive no cache. Durante a compilação o sistema de arquivos está ocupado (vkd3d-proton grava o cache de pipelines), e esse `fopen` segurava o desenho com a trava do GDI. O 0.52 guarda o resultado da busca por nome de arquivo, inclusive quando a fonte não existe; o disco é consultado uma vez por arquivo. O amostrador também registra os bytes antes do endereço da chamada ao kernel, para identificar o número da chamada se ainda houver congelamento. Próximo teste: `title-mode=host`, mexer no menu durante a compilação.

**Teste do 0.50:** igual: a tela congela enquanto compila. Com 16 arenas, as amostras continuam com quase todas as threads (as 15 de compilação e a do launcher) no mesmo ponto de uma chamada ao kernel (`0x80000038a`). Não dá para ver qual chamada é, mas a disputa pelo alocador não era o único problema: 16 threads com a mesma prioridade revezam os núcleos, e a do launcher espera a vez. O 0.51 coloca acima das outras (prioridade 600 em vez de 700; no console, menor é mais alta) a thread principal do motor (launcher e, depois, o jogo), o laço do host que manda os quadros para a TV, a vigia e a thread do controle. Toda outra thread do motor fica na prioridade original do título (700), mesmo quando criada por uma thread elevada. Próximo teste: `title-mode=host`, mexer no menu durante a compilação.

**Teste do 0.49:** sem erro, e o launcher já mostra a compilação dos shaders. Mas a imagem congela por instantes e o contador de shaders salta depois. O despejo explica o motivo: as 15 threads que compilam (284–340) passam o tempo em `malloc`/`free`, e as amostras as mostram quase todas paradas na espera de uma trava do kernel. Era a trava única das arenas do alocador do host (0.48). A thread do launcher (256) também esperava nela, no meio do desenho, segurando a trava do GDI. O 0.50 divide as arenas em 16, cada uma com a própria trava, escolhida pela thread; um bloco liberado volta à arena de onde veio. Conferido no Linux com o motor real. Próximo teste: `title-mode=host`, deixar compilar e mexer no menu.

**Teste do 0.48:** o travamento sumiu. O motor rodou normalmente (4 arenas de 64 MiB, memória flexível estável em 363 MiB livres), até que o **amostrador de diagnóstico** causou um SIGSEGV em `onSample`, no despejo dos 3 s. Com o `malloc` livre, as threads enfim se registraram, e o amostrador copiou 512 palavras da pilha da thread 260, passando do topo dela. O 0.49 limita a cópia ao quadro do host da própria thread, que fica acima de todo código do motor. Próximo teste: `title-mode=host`, mexer no menu.

**Teste do 0.47 — a causa dos travamentos:** a memória direta do título tem 12 GB e ficou quase toda livre, mas o log mostrou blocos de 64 a 192 KiB indo para a memória direta. Isso só acontece quando o `malloc` do título recusa o bloco: **o heap do `malloc` do título se esgotou**. O host mandava para ele todas as alocações pequenas do motor e das DLLs. Esse heap é pequeno e fixo, e o próprio sistema depende dele (`pthread_create` aloca por dentro). Por isso a criação da thread 284 não voltava e tudo que alocava parava, desde o 0.35. O 0.48 tira o host do `malloc` do título: blocos menores que 1 MiB saem de arenas próprias de 64 MiB na memória direta, com listas livres por classe de tamanho (68 classes), e os de 1 MiB ou mais continuam com mapeamento próprio. Conferido no Linux com o motor real (uma arena basta até o launcher). Próximo teste: `title-mode=host`, mexer no menu.

**Teste do 0.46:** o fechamento acabou (a memória flexível ficou estável, em ~386 MiB livres), mas travou de novo. Logo depois que a thread 280 começou a verificação da pasta do jogo, os blocos grandes do alocador do host saltaram de 150 MiB para **872 MiB em 300 ms**, agora vindos da memória direta. Em seguida o `pthread_create` da thread 284 não voltou e o launcher parou. No Linux (sem a pasta do jogo) os blocos grandes não passam de ~150 MiB. Hipótese: a memória direta também se esgota, e o `pthread_create` e o driver da GPU dependem dela. O 0.47 registra o tamanho da memória direta do título, o maior trecho livre dela, cada bloco de 1 MiB ou mais com a thread que pediu (até 300) e as recusas da memória direta. As amostras por sinal e a contagem de aninhamento continuam erradas no console (no Linux funcionam); ainda não sei por quê. Próximo teste: `title-mode=host`.

**Teste do 0.45 — causa confirmada: falta de memória flexível.** O título tem 448 MiB de memória flexível (de onde vêm `malloc` e `mmap`). Em menos de 1 s os blocos grandes do alocador do host chegaram a 215 MiB (23, 32, 60 MiB...), a memória livre caiu de 425 para 200 MiB e o sistema encerrou o título sem sinal. Os travamentos anteriores (threads paradas ao alocar) eram o mesmo esgotamento. O 0.46 tira os blocos de 1 MiB ou mais do alocador do host da memória flexível e os coloca na **memória direta** do console, o pool grande que o VideoOut já usa (`sceKernelAllocateDirectMemory` + `sceKernelMapDirectMemory`, CPU leitura e escrita, write-back). Se a memória direta falhar, cai no `mmap` como antes. O log diz "bloco de N MiB em memoria direta". Próximo teste: `title-mode=host`, mexer no menu.

**Teste do 0.44:** fechou de novo cerca de 1 s após a partida, logo depois de criar a thread 284 (`pthread_create` devolveu 0). A medição mostrou **448 MiB** de memória flexível configurada, 425 MiB livres no início e **294 MiB livres meio segundo depois** (90 MiB nos blocos grandes do alocador do host). A morte veio uns 300 ms depois, antes da medição seguinte. O 0.45 mede a cada 100 ms (registra quando muda 8 MiB ou mais). Também registra cada reserva de `VirtualAlloc` de 64 MiB ou mais (com o tamanho da tabela de páginas que o host cria para ela), cada commit de 8 MiB ou mais e cada bloco de 8 MiB ou mais do alocador do host. Próximo teste: `title-mode=host`.

**Teste do 0.43:** o título fechou com erro cerca de 1 s depois da partida. O `host.txt` termina no `CreateThread` da thread 280, sem a linha do `pthread_create` e sem falha registrada pelo host: o processo foi encerrado de fora. Uma morte assim e os travamentos anteriores (threads paradas justamente quando alocam: `CreateFontW`, `pthread_create`, a cópia de 8 MB do quadro) combinam com **falta de memória flexível do título**. O 0.44 mede isso: a vigia registra "Memoria flexivel: X MiB livres de Y MiB" a cada meio segundo quando a memória livre muda 16 MiB ou mais, junto com o que o alocador do host mapeou. Para aliviar a memória, o quadro do launcher reaproveita o mesmo buffer e o `frame.png` só é gravado uma vez (e no fim). Próximo teste: `title-mode=host`, mexer no menu.

**Teste do 0.42:** o launcher **reagiu ao controle por alguns segundos** e depois travou. O log confirma que as threads do título eram FIFO (política 1, prioridade 700) e passaram a round-robin (política 3); as teclas chegaram ao motor ("Tecla entregue ao motor"). O travamento começa no `CreateThread` da thread 284 (feito pela 264), que nunca retorna. A partir daí a thread do launcher (256) fica em `CreateFontW` e o laço do host para. A consulta de afinidade falhou (errno 34, tamanho de `cpuset_t`). O 0.43 marca também a **saída** de cada função do host, e o despejo diz se a thread está "[dentro de X]" ou "[no codigo do motor]". Registra quem segura a trava do GDI, quantas threads estão registradas para amostragem e quanto tempo cada `pthread_create` levou. Próximo teste: `title-mode=host`, mexer no menu até travar e esperar uns 20 s.

**Teste do 0.41:** a thread 264 chamou `CreateThread` para a 284, e a 284 **nunca começou** ("iniciada" não aparece). Ao mesmo tempo a thread do launcher (256) parou, o laço do host parou e a gravação do PNG quase não avançou. No Linux o mesmo motor fica saudável (launcher esperando mensagens, threads 284..324 rodando). O quadro aponta para agendamento: no PS5 as threads do título são FIFO de prioridade fixa, e uma thread que gira sem bloquear nunca cede o núcleo. O 0.42 coloca cada thread do motor, o laço do host e a vigia em round-robin na mesma prioridade, liberadas em todos os núcleos do processo (`platform::shareCpu`), e registra a política e a afinidade originais ("Agendamento ..."). As amostras por sinal saíram vazias no console (no Linux funcionam); ficam no log para comparar. Próximo teste: `title-mode=host`, mexer no menu.

**Objetivo atual:** o launcher vivo na TV, reagindo ao controle (sem pular para o jogo).

**Teste do 0.40:** a vigia funcionou. O controle posta as teclas, mas a thread 256 (a do launcher, dona da janela) fez `CreateFontW` e depois não chamou mais nenhuma função do host por pelo menos 9 s: ela está rodando código do próprio motor sem sair dele, por isso não lê as teclas nem redesenha. O laço principal do host apareceu sempre na etapa 4. O 0.41 acrescenta amostras: a vigia interrompe cada thread do motor com SIGUSR2 e registra o endereço em execução (4 amostras) e os endereços de retorno do motor na pilha ("Amostras (n)"). Próximo teste: `title-mode=host`, esperar uns 20 s.

**Teste do 0.39:** continuou sem nenhum despejo, e o `frame.png.tmp` apareceu de novo parcial (36 KB) parado, então o laço principal do host trava por outro motivo. O 0.40 move o despejo para uma thread vigia própria (aos 3, 6 e 12 s) que escreve "Despejo n: laco principal na etapa X" (1 esperando parada, 2 salvando, 3 apresentando no VideoOut, 4 depois do VideoOut) e as últimas chamadas de cada thread do motor. Próximo teste: `title-mode=host`, esperar uns 20 s.

**Teste do 0.38:** de novo nenhum despejo, e o `frame.png.tmp` ficou parcial (32 KB) e parado no console: o laço principal do host estava preso gravando o PNG de 6 MB, por isso não despejava o estado nem repassava quadros. O 0.39 grava o PNG numa thread de fundo, no máximo a cada 30 s, e mantém os despejos aos 3, 6 e 12 s. Próximo teste: `title-mode=host`, esperar uns 20 s.

**Teste do 0.37:** o `host.txt` parou na partida, sem nenhum "Estado do motor". Ou o app foi fechado antes de 10 s, ou a coleta travou. O 0.38 despeja aos 3, 6 e 12 s e escreve "Despejo n do estado do motor" antes de coletar (se essa linha aparecer sem a seguinte, a coleta travou). Próximo teste: `title-mode=host`, esperar uns 20 s sem fechar.

**Teste do 0.36:** o pad funciona (Baixo/Cima/Círculo viraram 0x28/0x26/0x1b e foram postados à janela 0xae0002), mas **nenhuma tecla chegou ao motor**: não houve "Tecla entregue ao motor". Ou seja, a thread da janela não está lendo a fila; ela deve estar bloqueada esperando algo, e o launcher continua em "Game folder: Checking…". O 0.37 despeja, aos 10, 20 e 30 s, as últimas 24 chamadas de cada thread do motor ("Estado do motor (n)") para ver em que cada uma está parada. Próximo teste: `title-mode=host`, esperar uns 40 s e enviar o `host.txt`.

**Teste do 0.35:** o controle abriu e conectou (`Controle conectado`), mas o launcher continuou parado em "Game folder: Checking…" e o `host.txt` não registrou nada depois do início, então não dá para saber se os botões e as teclas chegam. O 0.36 só acrescenta diagnóstico: "Controle: botoes 0x..." (mudança de botão), "Tecla do controle: vk ..." (tecla postada, com a janela de destino) e "Tecla entregue ao motor" (quando o motor a recebe em `DispatchMessageW`). Próximo teste: `title-mode=host`, apertar Cruz, Baixo e Cima algumas vezes e enviar o `host.txt`.

**0.35 — controle:** o 0.34 rodou sem erro (launcher na TV, motor vivo). Para chegar ao vídeo do jogo é preciso apertar Start no launcher, então o título agora lê o controle do PS5 (`host/src/Input.cpp`: `sceUserServiceInitialize`, `scePadInit/scePadOpen/scePadReadState`, link com `libScePad` e `libSceUserService`). Cruz/Options = Enter, Círculo = Esc, direcional e analógico esquerdo = setas; as teclas entram na fila de mensagens da janela ativa (`win32::postKey`, WM_KEYDOWN/WM_KEYUP) e em `GetKeyState`/`GetAsyncKeyState`. O `host.txt` registra "Controle: ..." (abertura do pad e conexão). Ainda não há XInput nem swapchain: o vídeo do jogo depende do passo seguinte. Próximo teste: `title-mode=host`, apertar Cruz no launcher e enviar o `host.txt`.

**Teste do 0.33 com `title-mode=host`:** o anel da thread 280 mostrou a vida inteira dela, e o CRT do motor (`__acrt_getptd`, `engine+0x1f6794`) chama `abort()` quando `FlsSetValue` falha. No host, essa falha vinha do bloco de FLS, alocado com o `calloc` do título. O 0.34 aloca esse bloco com o alocador do host e o libera no fim da thread. Próximo teste: `title-mode=host`.

## 0.33

**Teste do 0.32 com `title-mode=host`:** a mensagem do motor veio da thread 260, o relator de falhas, e as últimas chamadas dela só mostram o relatório. O klog mostra o título suspenso, sem falha do sistema. O 0.33 guarda os anéis de chamadas de todas as threads e os lista na mensagem do motor, para ver o que a thread 280 fez antes do `abort()`. Próximo teste: `title-mode=host`.

## 0.32

**Teste do 0.31 com `title-mode=host`:** a thread de trabalho 280 do motor chamou o próprio tratador de falha: "exception 0xc0000409 at 0x0 ... the C++ runtime's abort, where terminate ends". Foi um `abort()` do CRT do motor, sem nenhuma `RaiseException` no host; depois o título parou sem registrar nada. O 0.32 guarda as últimas 64 chamadas de cada thread ao host e as lista na parada e na mensagem do motor. Próximo teste: `title-mode=host`, com o klog ligado.

## 0.31

**Teste do 0.30 com `title-mode=host`: o launcher apareceu na TV** (VideoOut, dois framebuffers de 1920x1080 em tiles de 64 KiB). Logo depois, uma parada de diagnóstico: `RtlCaptureStackBackTrace`, chamado numa thread de trabalho do motor. O 0.31 responde esse e mais 22 imports simples como o Windows sem o recurso (captura de pilha sem quadros, console ausente, COM sem classes, ambiente, etc.). Seguem como paradas: `D3DCompile` e o despacho de exceções (`RtlUnwindEx` e afins). Próximo teste: `title-mode=host`.

## 0.30

**Teste do 0.29 com `title-mode=host`:** o motor passou da assinatura de raiz (`D3D12SerializeRootSignature`) e seguiu rodando sem mensagem de erro, com a TV preta: o host só gravava `frame.png`. O 0.30 apresenta o quadro da janela do motor (o launcher) no VideoOut: dois framebuffers de 16 MiB em memória direta, tiles de 64 KiB, B8G8R8A8, `sceVideoOutSubmitFlip` no vblank, e a tela de abertura é escondida na primeira chamada. Próximo teste: `title-mode=host`; o launcher deve aparecer na TV.

## 0.29

**Teste do 0.28 com `title-mode=host`: o motor chegou ao launcher.** O `opendir` foi recusado (`errno 1`) na pasta do jogo, mas `sceKernelGetdents` leu os nomes; o `D3D12CreateDevice` do motor passou pelo vkd3d-proton, e o host gravou `/data/azimuth-ps5/frame.png` com o launcher (pasta do jogo detectada). A parada seguinte foi `D3D12SerializeRootSignature`, que o 0.29 encaminha ao vkd3d-proton. Próximo teste: `title-mode=host`.

## 0.28

**Teste do 0.27 com `title-mode=host`:** sem queda. O motor criou a janela e a fábrica DXGI, o RADV carregou pelo host e o título terminou normalmente com a parada de diagnóstico "Incomplete: no Forza_Durango_Release_Final.exe in Mount". Os `FindFirstFile` na pasta `Mount` do jogo deram erro 5, embora a pasta exista no pendrive. O 0.28 lista pastas com `opendir` e, se recusado, com `sceKernelGetdents`, e registra o errno real. Próximo teste: `title-mode=host`.

## 0.27

**Teste do 0.26 com `title-mode=host`:** o motor mapeou, criou a janela e a fábrica DXGI, e o título fechou sozinho. O klog mostra `abort is called`, com a pilha `win_CreateCompatibleBitmap` → `operator new` → `std::bad_alloc`: o `malloc` do título recusa o bitmap de 8 MB do launcher. O 0.27 passa todo `operator new`, o heap do motor e o `msvcrt` por um alocador único que usa `mmap` para blocos de 1 MiB ou mais e para os que o `malloc` recuse. Próximo teste: `title-mode=host`, com o klog ligado.

## 0.26

**Teste do 0.25 com `title-mode=host`:** o motor foi achado em `/mnt/usb1/PC/FH2` (o `access(2)` era o defeito, e o título lê o pendrive), e o mapeamento parou em "Bloco de protecao com secoes de protecoes diferentes; recusado". O 0.26 aplica ao motor a união das proteções nos 3 blocos mistos, sem permitir bloco gravável e executável. Próximo teste: `title-mode=host`, fechar pelo menu.

## 0.25

**Teste do 0.24 com `title-mode=host`:** "azimuth.exe nao encontrado", mesmo com o exe em `/data/azimuth-ps5` (3.087.872 bytes). A causa provável é o `access(2)` do título, que responde errado onde `fopen` lê. O 0.25 troca o `access` por `fopen` e lista o errno de cada candidato. Próximo teste: trocar o `eboot.bin`, manter `title-mode=host`, fechar pelo menu.

## 0.24

**Teste do 0.23 no PS5: passou.** O vkd3d-proton criou o dispositivo D3D12, a fila, a lista e a fence sobre o RADV do PS5 e executou a lista (SM 6.8, DXR 1.1). O 0.24 liga o `D3D12CreateDevice` do motor a esse vkd3d-proton, com o adaptador do DXGI do host. Próximo teste: `title-mode=host` (o motor), com o `azimuth.exe` no pendrive como antes; o relatório principal é o `host.txt`. O modo `d3d12` continua valendo para a verificação sem o motor.

## 0.23

**Teste do 0.22 no PS5:** o `d3d12core.dll` carregou e inicializou; o RADV carregou pelo host, a fábrica DXGI descreveu a GPU e o vkd3d-proton criou a instância Vulkan. O `D3D12CreateDevice` devolveu `E_OUTOFMEMORY` (`0x8007000e`) 30 ms depois. O 0.23 manda para o `mmap` os blocos de 1 MiB ou mais e os que o `malloc` do sistema recuse, no `msvcrt` do host, e registra os erros do driver Vulkan. Próximo teste: trocar o `eboot.bin`, manter `title-mode=d3d12`, fechar pelo menu.

## 0.22

**Teste do 0.21 no PS5:** o `d3d12.dll` passou (blocos unidos, o console aceitou 1 bloco RWX, `DllMain` concluído). O `d3d12core.dll` (5,9 MB) deu `std::bad_alloc` ainda na leitura do arquivo. O 0.22 guarda os bytes dos PEs num vetor cujo alocador usa `mmap` para blocos de 256 KiB ou mais. Próximo teste: trocar o `eboot.bin`, manter `title-mode=d3d12`, fechar pelo menu.

## 0.21

**Teste do 0.20 no PS5:** a sondagem mostrou que o PS5 protege memória em blocos de 16 KiB (vale a última proteção aplicada no bloco): `.data` ficou sem escrita e o `DllMain` caiu. O 0.21 aplica a proteção por bloco, dando a união das proteções aos blocos mistos das DLLs, com 1 bloco RWX por DLL (`.text` junto de `.data`). Próximo teste: trocar o `eboot.bin`, manter `title-mode=d3d12`, fechar pelo menu.

## 0.20

**Teste do 0.19 no PS5:** o `d3d12.dll` foi lido do Playden e mapeado, mas o `DllMain` caiu em `d3d12+0x11fb` ao gravar em `0x35e4f10b4` (`.data`). O 0.20 sonda cada página das DLLs carregadas (`R-`/`RW` por seção no `host.txt`) e registra o código da falha. Próximo teste: trocar o `eboot.bin`, manter `title-mode=d3d12`, fechar pelo menu.

## 0.19

O 0.19 tem um modo novo, `title-mode=d3d12`. Nele, o host roda sem o motor e
carrega o vkd3d-proton da instalação do Playden (`d3d12.dll` e
`d3d12core.dll`) com o próprio loader, sobre o `vulkan-1.dll` e o DXGI do
host. Depois cria um dispositivo D3D12, uma fila, um alocador e uma lista,
executa a lista e espera a fence. O relatório vai para
`/data/azimuth-ps5/host.txt`, incluindo o log do próprio vkd3d-proton
(linhas `[stderr]`). O cache de pipelines do vkd3d-proton fica em
`/data/azimuth-ps5/vkd3d-proton.cache`. No Linux, essa verificação passou
(`host --d3d12`). Os modos `smoke`, `gpu` e `host` não mudaram (o 0.18 só
acrescentou o `vulkan-1.dll` ao host).

Próximo teste: substituir somente `eboot.bin` pelo 0.19, escrever `d3d12` em
`title-mode` e, depois da notificação, fechar o título pelo menu.

Histórico do 0.17:

**Teste do 0.17 no PS5: passou e fechou sem erro.**
- Modo `gpu`: `[PASSOU]`, com 16, 64 e 1024 palavras corretas e
  `vkDeviceWaitIdle` 0. O `title.txt` terminou com `codigo=0` e a linha de
  espera. Ao fechar pelo menu, o klog mostra só `KillApp()`, `Kill App` e
  `ProcessTerm()`, sem `exit()`, SIGSYS, SIGILL, falha de GPU nem "App Crash".
- Modo `smoke`: também fechou limpo.

Evidência em
[`../console-reports/2026-10-28-title-0.17-gpu-close/`](../console-reports/2026-10-28-title-0.17-gpu-close/)
e
[`../console-reports/2026-10-28-title-0.17-smoke-close/`](../console-reports/2026-10-28-title-0.17-smoke-close/).

Requisitos confirmados para usar o RADV num título:
1. Depois do `sceKernelLoadStartModule`, chamar a entrada do RADV (`+0x4ef0`),
   que roda o `INIT_ARRAY`, antes de qualquer chamada Vulkan.
2. Apagar o cache `/app0/radv-shader-cache` se ele foi gerado sem isso.
3. Manter o RADV carregado e não retornar do `main`.

O 0.17 não retorna mais do `main`, seguindo a regra da base. Ao terminar,
em qualquer modo, ele:
1. grava `Fim do titulo: codigo=N` e `Titulo aguardando ser fechado pelo menu
   do sistema` no `title.txt`;
2. mostra a notificação "teste terminado (codigo N); feche o aplicativo pelo
   menu do PS5";
3. fica parado, sem usar a CPU, até ser fechado pelo menu do PS5.

O GPU probe é o mesmo do 0.16 (0.22).

Próximo teste: substituir somente `eboot.bin`, voltar `title-mode` para
`gpu` e manter `gpu-stop` ausente. Depois da notificação, fechar o aplicativo
pelo menu. O resultado esperado é `[PASSOU]` e nenhum "App Crash", SIGSYS ou
SIGILL no klog.

Histórico do 0.16:

**Teste do 0.16 no PS5:** `[PASSOU]`, e nenhum `SINAL NA SAIDA` foi
registrado. O klog mostra só `exit() exit_value=0` e, em seguida, SIGILL
("privileged instruction fault") em `0x8000ba9bf`, dentro do `exit()` da
`libSceLibcInternal`, com `r15` na `libkernel` e nenhum frame do título além
do `start`. O tratador do probe não participou. O padrão é o da libc executando
uma instrução privilegiada depois que a saída voltou ao processo. Evidência em
[`../console-reports/2026-10-28-title-0.16-gpu-exit/`](../console-reports/2026-10-28-title-0.16-gpu-exit/).

**Smoke com o 0.16 (klog):** as verificações passaram (`codigo=0`), mas a
saída também falhou: `exit() exit_value=0`, depois SIGSYS (o tratador do
smoke chamou `_exit(140)`) e SIGILL. Portanto, o erro na saída não depende
da GPU. A base pública documenta: "Do not return from `main` or call an exit
function" (`.deps/native-foundation/docs/TROUBLESHOOTING.md`); o esqueleto
mantém o `main` vivo. O nosso `main` retorna, e o `_start` da base chama
`exit(status)`. Evidência em
[`../console-reports/2026-10-28-title-0.16-smoke-exit/`](../console-reports/2026-10-28-title-0.16-smoke-exit/).

Correção proposta: ao terminar, o título registra o resultado, mostra uma
notificação e fica parado, esperando ser fechado pelo menu do sistema, sem
retornar do `main`.

O 0.16 (GPU probe 0.22) serve para identificar o sinal que chega depois do
`exit(0)`:
- O `gpu-probe.txt` não é mais fechado pelo probe e fica aberto até o fim do
  processo. A última etapa passa a ser "saida do processo (depois do probe)".
- Um sinal recebido nessa fase é registrado como `SINAL NA SAIDA`, com sinal,
  código, `rip` e módulo, endereço, `rsp`, pid de origem e 16 posições da
  pilha. Depois o sinal volta ao comportamento padrão (`SIG_DFL`), sem
  `_exit(3)` e, portanto, sem o `ud2` que gerava o SIGILL.

Próximo teste: substituir somente `eboot.bin` pelo 0.16, manter
`title-mode=gpu` e `gpu-stop` ausente. Os relatórios são baixados pelo
Prospero Manager.

Histórico do 0.15:

**Teste do 0.15 no PS5:** `[PASSOU]` de novo, e o RADV ficou carregado. O
klog mostra `exit() exit_value=0`, ou seja, os handlers de `atexit` passaram.
Depois disso, a thread principal recebeu outro sinal; o tratador do probe
chamou `_exit(3)` duas vezes, a chamada retornou, e o `ud2` seguinte gerou
SIGILL. O relatório do tratador se perdeu porque o arquivo já estava fechado.
Evidência em
[`../console-reports/2026-10-28-title-0.15-gpu-exit/`](../console-reports/2026-10-28-title-0.15-gpu-exit/).

O 0.15 (GPU probe 0.21) corrige a saída do 0.14: o RADV iniciado pelo
runtime não é mais descarregado no fim do probe e fica carregado até o fim do
processo. Os construtores do RADV e o `util_queue` do Mesa registram handlers
de `atexit`, que a libc executa no `exit()`; com o módulo descarregado, um
deles falhava, e o título terminava com SIGILL. O resto do teste é igual ao
0.14, que passou.

Próximo teste: substituir somente `eboot.bin` pelo 0.15, manter
`title-mode=gpu` e `gpu-stop` ausente. O cache de shaders atual, gerado pelo
0.14 com os construtores executados, pode ficar. O resultado esperado é
`[PASSOU]`, o título terminar sem aviso de erro e o klog não registrar
SIGILL nem "App Crash".

Histórico do 0.14:

**Repetição do 0.14 com o cache apagado: [PASSOU].** A GPU executou o
compute shader: 0 de 1024 palavras diferentes, `vkDeviceWaitIdle` 0, e o
shader copiado é o preenchimento correto para gfx1030 (`global_store_dwordx4`,
`s_endpgm`). Na saída, depois de `codigo=0`, um handler de `atexit` falhou e o
título terminou com SIGILL. A hipótese é um handler do RADV, que o probe
descarrega antes do `exit`. Evidência em
[`../console-reports/2026-10-28-title-0.14-gpu-cache-limpo/`](../console-reports/2026-10-28-title-0.14-gpu-cache-limpo/).

**Teste do 0.14 no PS5:** os construtores do RADV não tinham rodado (flag 0,
tabelas zeradas). A entrada `+0x4ef0` os executou (flag 1, tabelas
`ffff0032 00390052`). 4c e 4d concluíram; 4e falhou como antes, e a janela
copiada é idêntica byte a byte à do 0.13. O RADV usa um cache de shaders
em disco em `/app0/radv-shader-cache` (`/mnt/usb1/FAKE78002/radv-shader-cache`).
A única entrada de `radv_builtin_shaders`, criada no primeiro teste completo
(27/10, 21:21), contém o shader com opcodes zerados, e o RADV a reaproveita.
Cópia da entrada em
[`../console-reports/2026-10-28-title-0.14-gpu-init/radv-shader-cache/`](../console-reports/2026-10-28-title-0.14-gpu-init/radv-shader-cache/).

Próximo teste: apagar `/mnt/usb1/FAKE78002/radv-shader-cache` e repetir o 0.14
sem outras mudanças.

Histórico do 0.14:

**Teste do 0.13 no PS5 — causa encontrada:** a cópia da janela dos shaders
trouxe o código no PC `0x20004001C`, que o klog voltou a apontar como
`ILLEGAL_INST`. Todos os opcodes estão zerados: `s_endpgm` aparece como
`s_nop 0`, `global_store_dwordx4` como opcode 0, e `s_mov_b32`/`v_mov_b32`
também, mas os operandos e o preenchimento `s_code_end` estão certos. Isso
indica a tabela de opcodes do ACO (`aco::instr_info`) vazia.

No `libvulkan.prx` do Playden (SHA-256 `db21b394…0b94`):
- `DT_INIT` é 0, então o `init` que o sistema registra é `módulo+0x0`, que
  contém só `xor eax,eax; ret`.
- O `INIT_ARRAY` (5 construtores) roda pela entrada `+0x4ef0`, protegido pela
  flag `+0x17f3278`, que o próprio `INIT_ARRAY[1]` também liga.
- O construtor `[4]` (`+0x4e7660`) copia seis blocos de `0xcc6` bytes para a
  `.bss` em `+0x17fa088`: as tabelas de opcodes do ACO.

O `sceKernelLoadStartModule` do título não rodava esses construtores.
Relatórios, klog e `gpu-shader-window.bin` em
[`../console-reports/2026-10-28-title-0.13-gpu-shader/`](../console-reports/2026-10-28-title-0.13-gpu-shader/).

O 0.14 (GPU probe 0.20), depois do `sceKernelLoadStartModule`, registra a
flag e as primeiras palavras das tabelas de opcodes. Se a flag estiver
zerada, chama a entrada do RADV e registra os dois valores de novo. O resto
do teste é igual, incluindo a cópia da janela, que agora deve mostrar o
shader com opcodes reais.

Próximo teste: substituir somente `eboot.bin` pelo 0.14, manter
`title-mode=gpu` e `gpu-stop` ausente.

Histórico do 0.13:

**Teste do 0.12 no PS5:** `setenv` funcionou, e o RADV leu `RADV_DEBUG`. Ao
gravar o preenchimento por compute, o dump do shader falhou na CPU: SIGSEGV
lendo o endereço 0 em `libSceLibcInternal.sprx+0x4953`. Isso aconteceu antes
da cópia da janela e de qualquer envio. O probe se recuperou, descarregou o
RADV e o título terminou normalmente; o klog não registrou falha de GPU.
Relatórios em
[`../console-reports/2026-10-28-title-0.12-gpu-shader/`](../console-reports/2026-10-28-title-0.12-gpu-shader/).

O 0.13 (GPU probe 0.19) remove o `RADV_DEBUG` e mantém a cópia da janela
`0x200000000` + 4 MiB para `/data/azimuth-ps5/gpu-shader-window.bin`, feita
depois de gravar o preenchimento por compute e antes do envio. A etapa 4e deve
voltar a falhar na GPU, agora com o código do shader copiado.

Histórico do 0.12:

**Klog da repetição do 0.11:** o sistema encerrou o título com
`GPU_FAULT_WAVEFRONT_ERROR_ASYNC`. As quatro ondas do compute shader de
preenchimento pararam em `ILLEGAL_INST` no PC `0x20004001C`, dentro da janela
de 32 bits dos shaders. Evidência em
[`../console-reports/2026-10-28-title-0.11-gpu-klog/`](../console-reports/2026-10-28-title-0.11-gpu-klog/).

O 0.12 (GPU probe 0.18) serve para identificar essa instrução:

- Antes de `vkCreateInstance`, define `RADV_DEBUG=metashaders,asm` com o
  `setenv` da libc do sistema e confere o valor com `getenv`, que é a função
  usada pelo RADV. O RADV deve imprimir o shader de preenchimento,
  desmontado, no `gpu-stderr.txt`.
- Depois de gravar o preenchimento por compute (quando o RADV já compilou e
  enviou o pipeline), copia a janela `0x200000000` + 4 MiB para
  `/data/azimuth-ps5/gpu-shader-window.bin`, página por página, cada uma no
  seu offset. A cópia usa `pwrite`: uma página sem mapeamento ou sem leitura
  pela CPU devolve `EFAULT` e é pulada, sem derrubar o processo. O relatório
  lista as faixas copiadas e as 16 palavras em torno do PC do 0.11.
- O resto é igual ao 0.11: 4c e 4d devem concluir, e 4e deve falhar de novo.

No Linux, as três etapas passaram; ali a janela não tem páginas mapeadas.

Próximo teste: substituir somente `eboot.bin` pelo 0.12, manter
`title-mode=gpu` e `gpu-stop` ausente, e capturar o klog. Enviar
`title.txt`, `gpu-probe.txt`, `gpu-stderr.txt` e `gpu-shader-window.bin`.

Histórico do 0.11:

**Teste do 0.11 no PS5:** 4c (`vkCmdUpdateBuffer`, CP WRITE_DATA) e 4d
(`vkCmdFillBuffer` de 256 bytes, CP DMA) concluíram com todas as palavras
corretas. 4e (4096 bytes, compute shader): fence `VK_NOT_READY` em 283
consultas até 351 ms; todas as chamadas retornaram, e o título foi encerrado
antes do registro seguinte, sem sinal nem stderr. A GPU executa comandos;
o que falha é a execução de shader. Relatórios em
[`../console-reports/2026-10-28-title-0.11-gpu-submit/`](../console-reports/2026-10-28-title-0.11-gpu-submit/).

**Teste do 0.10 no PS5:** a memória tipo 3 (heap 1, flags 0x7) foi
escolhida, e o resultado foi o mesmo do 0.9: envio vazio e command buffer
vazio concluíram; com o preenchimento, envio 0, primeiro status
`VK_NOT_READY`, pausa 0 e nenhum outro registro. Relatórios em
[`../console-reports/2026-10-28-title-0.10-gpu-submit/`](../console-reports/2026-10-28-title-0.10-gpu-submit/).

No RADV (`src/amd/vulkan/meta/radv_meta_buffer.c`), numa GPU integrada,
`vkCmdFillBuffer` de 4096 bytes ou mais é um dispatch de compute shader;
abaixo disso usa CP DMA. `vkCmdUpdateBuffer` abaixo de 1024 bytes grava
pacotes WRITE_DATA da CP. Do 0.5 ao 0.10 só foi testado o preenchimento de
4096 bytes, isto é, só o caminho de compute.

O 0.11 (GPU probe 0.17) executa três escritas pela GPU, da mais simples para
a mais complexa, cada uma com buffer, command buffer e fence próprios e com
a mesma barreira `TRANSFER_WRITE -> HOST_READ`:

| Etapa | Comando | Caminho no RADV |
|---|---|---|
| 4c | `vkCmdUpdateBuffer`, 64 bytes (palavra n = padrão xor n) | CP WRITE_DATA |
| 4d | `vkCmdFillBuffer`, 256 bytes | CP DMA |
| 4e | `vkCmdFillBuffer`, 4096 bytes | compute shader |

A primeira etapa sem conclusão encerra o probe, então a última etapa do
relatório indica o primeiro caminho que falhou. O polling da fence registra
as dez primeiras consultas antes e depois da chamada e, depois, uma a cada
100 ms, com o tempo. A etapa do watchdog não é mais reiniciada a cada
consulta. Assim, o relatório distingue uma chamada que não retornou de um
processo encerrado entre consultas. A escolha da memória continua a do 0.10.
No Linux, as três etapas passaram nas variantes comum e de título.

Próximo teste: substituir somente `eboot.bin` pelo 0.11, manter
`title-mode=gpu` e `gpu-stop` ausente. Enviar os três relatórios.

Histórico do 0.10:

**Teste do 0.9 no PS5:** submissão sem command buffers e command buffer
vazio concluíram com fences sinalizadas. O preenchimento com a barreira
`TRANSFER_WRITE -> HOST_READ` continuou sem conclusão registrada: envio 0,
primeiro status `VK_NOT_READY`, pausa retornou 0, sem outro registro ou fim
do título. Não houve comparação das 1024 palavras. Relatórios em
[`../console-reports/2026-10-28-title-0.9-gpu-submit/`](../console-reports/2026-10-28-title-0.9-gpu-submit/).

O 0.10 (GPU probe 0.16) prioriza memória com `DEVICE_LOCAL | HOST_VISIBLE |
HOST_COHERENT`. Nos tipos anunciados nesse PS5, o buffer aceita o tipo 3
(heap 1, flags 0x7), além do tipo 2 usado até agora (heap 0, flags 0x6).
O tipo selecionado, heap e flags ficam explícitos no relatório. Há fallback
para memória visível/coerente sem `DEVICE_LOCAL` em dispositivos sem essa
combinação. Os demais testes de fila e a barreira continuam iguais.
Essa mudança testa uma hipótese sobre a memória; não estabelece a causa
do fechamento nem comprova que o novo caminho funciona no PS5.

O teste completo do 0.10 manteve `title-mode=gpu` e `gpu-stop` ausente.

Histórico do 0.9:

**Teste do 0.8 no PS5:** a pausa de 1 ms antes do RADV retornou 0.
A submissão sem command buffers concluiu com fence sinalizada. A submissão
com `vkCmdFillBuffer` retornou 0; a primeira consulta retornou
`VK_NOT_READY`, a pausa seguinte retornou 0 e o relatório terminou antes
de registrar outra consulta. Não houve comparação do buffer nem final do
título. Isso não prova a causa da interrupção seguinte.
Relatórios em
[`../console-reports/2026-10-28-title-0.8-gpu-submit/`](../console-reports/2026-10-28-title-0.8-gpu-submit/).

O 0.9 (GPU probe 0.15) adiciona um teste com um command buffer vazio
entre o envio sem command buffers e o preenchimento. Também corrige uma
dependência de memória ausente: após `vkCmdFillBuffer`, grava uma barreira
`TRANSFER_WRITE -> HOST_READ` antes da leitura pela CPU após a fence.
Essa dependência é necessária para a leitura correta, mas sua ausência
não foi comprovada como causa do fechamento. Referência:
[exemplos de sincronização da Khronos](https://docs.vulkan.org/guide/latest/synchronization_examples.html).

O teste completo do 0.9 manteve `title-mode=gpu` e `gpu-stop` ausente.

Histórico do 0.8:

**Teste do 0.7 no PS5:** a thread do watchdog confirmou o início.
`vkQueueSubmit` retornou 0 em 0,05 ms e a primeira consulta a
`vkGetFenceStatus` retornou 1 (`VK_NOT_READY`). O relatório terminou depois
desse retorno, antes de registrar outra consulta, o prazo ou o fim do título.
Portanto, ainda não é possível distinguir a pausa seguinte, outra consulta
ou uma falha assíncrona após o envio. Não houve comparação do buffer.
Relatórios em
[`../console-reports/2026-10-28-title-0.7-gpu-submit/`](../console-reports/2026-10-28-title-0.7-gpu-submit/).

O 0.8 (GPU probe 0.14) registra a entrada/saída de uma pausa de 1 ms
antes de carregar o RADV. Depois cria uma fence não sinalizada, confere o
status inicial e submete uma operação sem command buffers, consultando sua
conclusão. Somente após essa etapa passa ao `vkCmdFillBuffer`. O primeiro
intervalo entre consultas também tem registros antes/depois. Uma submissão
vazia bem-sucedida não comprova execução de comandos de preenchimento.
Em caso de fence pendente/erro, mantém a saída `_exit(5)` sem espera ociosa
ou liberação de recursos pendentes. A causa do fechamento segue desconhecida.

O teste completo do 0.8 manteve `title-mode=gpu` e `gpu-stop` ausente.

Histórico do 0.6 e do 0.7:

**Teste do 0.6 no PS5:** `vkQueueSubmit` retornou 0 em 0,05 ms.
O último registro é a entrada em `vkWaitForFences`, com limite de 2 s;
não há retorno, heartbeat, sinal capturado nem fim do título. Isso localiza
a interrupção na espera, mas não comprova que a GPU executou o comando.
Relatórios preservados em
[`../console-reports/2026-10-28-title-0.6-gpu-submit/`](../console-reports/2026-10-28-title-0.6-gpu-submit/).

O 0.7 (GPU probe 0.13) consulta `vkGetFenceStatus` a cada 1 ms por até 2 s,
em vez de chamar `vkWaitForFences`. Registra o primeiro retorno e o resultado
final. Se a fence continuar pendente ou a consulta falhar, encerra com
`_exit(5)` sem liberar objetos possivelmente em uso nem chamar
`vkDeviceWaitIdle`. A consulta ao driver ainda pode falhar ou travar;
o prazo só pode ser aplicado entre chamadas que retornam. O watchdog
agora precisa confirmar que sua thread iniciou antes de o teste prosseguir.

O teste completo do 0.7 manteve `title-mode=gpu` e `gpu-stop` ausente.

Histórico do teste completo do 0.5:

O dispositivo lógico e o buffer foram criados no PS5; o teste completo
do 0.5 fechou com erro após a marca anterior à submissão.
No teste completo, `vkCreateDevice` retornou 0 e um buffer de 4096 bytes
foi mapeado no tipo de memória 2 em `0x4000000000`. O último registro foi
"Enviando vkCmdFillBuffer para a fila". Esse registro vinha antes de
`vkQueueSubmit` e `vkWaitForFences`, sem registrar o retorno de cada uma.
Não houve fence sinalizada, comparação das 1024 palavras, fim do probe ou
fim do título. O usuário informou tela preta, erro e fechamento do título.
Relatórios em
[`../console-reports/2026-10-27-title-0.5-gpu-submit/`](../console-reports/2026-10-27-title-0.5-gpu-submit/).
Esse resultado motivou o diagnóstico detalhado de submissão/espera do 0.6.

Histórico da etapa de instância:
`vkCreateInstance` retornou 0; `vkEnumeratePhysicalDevices` retornou 0 e um
dispositivo, identificado como "PlayStation 5 GPU (RADV NAVI21)", Vulkan
1.4.354. O driver informou dois heaps e duas famílias de filas. O probe
respeitou a parada antes do dispositivo lógico, destruiu a instância,
descarregou o RADV com retorno 0 e o título terminou com código 0. Relatórios
em [`../console-reports/2026-10-27-title-0.5-gpu-instance/`](../console-reports/2026-10-27-title-0.5-gpu-instance/).
Para o próximo teste, manter eboot e `title-mode=gpu` e remover `gpu-stop`.
O teste completo cria o dispositivo lógico e verifica na CPU 1024 palavras
preenchidas pela GPU com `vkCmdFillBuffer`.

Histórico da etapa de módulo:
`sceKernelLoadStartModule` retornou handle 8402 e resultado de entrada 0.
`vkGetInstanceProcAddr` e `vk_icdNegotiateLoaderICDInterfaceVersion` foram
resolvidos, e a interface ICD negociou versão 5 com retorno 0. O
descarregamento por `sceKernelStopUnloadModule` também retornou 0. O probe
e o título encerraram normalmente (código 0). Relatórios em
[`../console-reports/2026-10-27-title-0.5-gpu-module/`](../console-reports/2026-10-27-title-0.5-gpu-module/).
Manter o eboot e `title-mode=gpu`; mudar `gpu-stop` para `instancia`.

Histórico da investigação:
No 0.4, o descritor correto do relatório foi 12, e a escrita direta passou.
A falha foi SIGSEGV (11), RIP `0x800041aeb`, endereço `0x70`, erro de página
`0x4` (leitura), com RAX=0. O RIP está fora dos segmentos do RADV. Isso
difere da falha de busca de instrução observada no payload; o nome da função
do sistema ainda não foi identificado. Relatórios completos em
[`../console-reports/2026-10-27-title-0.4-gpu-entry/`](../console-reports/2026-10-27-title-0.4-gpu-entry/).

Próximo teste: substituir o eboot pelo 0.5 e manter `title-mode=gpu`,
`gpu-stop=modulo`. A chamada normal `sceKernelLoadStartModule` passa a cuidar
do carregamento/inicialização no título, em vez de mapear por syscall e
invocar a entrada manualmente. A hipótese é que falta registro no runtime
de usuário no caminho bruto, também sugerido por `ESRCH` nas APIs de consulta
de símbolo/descarregamento. Isso ainda precisa ser demonstrado no console.
No 0.3, `sceSysmoduleIsLoadedInternal` informou AgcDriver, Agc e VideoOut
já carregados (retorno 0 nos três). O RADV foi mapeado em `0x80118000`, e
a parada planejada foi registrada como `[PAROU]`, com encerramento normal.
O descarregamento continua retornando `0x80020003`; sua limpeza por essa
chamada ainda não foi confirmada. Relatórios em
[`../console-reports/2026-10-27-title-0.3-gpu-map/`](../console-reports/2026-10-27-title-0.3-gpu-map/).
O teste com `gpu-stop=modulo` tentou chamar `0x8011cef0` (RADV+`0x4ef0`)
e recuperou uma falha antes de registrar retorno da entrada. Sem sinal/RIP
no relatório, ainda não é possível dizer se alguma instrução do RADV rodou.
Extrato em
[`../console-reports/2026-10-27-title-0.3-gpu-entry/report-excerpt.txt`](../console-reports/2026-10-27-title-0.3-gpu-entry/report-excerpt.txt).
Próximo teste: substituir o eboot pelo 0.4 e manter `title-mode=gpu`,
`gpu-stop=modulo`, para obter o sinal e os registradores da falha.
Construtores, exceções/unwind, isolamento de TLS e destrutor de thread
passaram. As reservas de 64 KiB em `0x140000000` e `0x1000000000` ficaram
exatamente nos endereços pedidos, foram liberadas e o título terminou com
código 0. Relatório em
[`../console-reports/2026-10-27-title-0.2/title.txt`](../console-reports/2026-10-27-title-0.2/title.txt).
Isso valida o smoke; execução do motor e GPU como título continuam pendentes.
No teste seguinte, o RADV foi mapeado e a entrada não foi executada, como
pedido por `gpu-stop=carregado`. As dependências falharam com `0x80020002`
pelo caminho `/system/common/lib`, e o 0.8 reportou essa parada planejada
como falha de carregamento. Os logs estão em
[`../console-reports/2026-10-27-title-0.2-gpu-map/`](../console-reports/2026-10-27-title-0.2-gpu-map/).
O título 0.1 iniciou no console: construtores, exceções C++ e isolamento de
TLS passaram; o destrutor de TLS falhou e a execução terminou ao entrar no
teste de memória. Relatório preservado em
[`../console-reports/2026-10-27-title-0.1/title.txt`](../console-reports/2026-10-27-title-0.1/title.txt).

O mesmo eboot reúne o diagnóstico de inicialização, o GPU probe 0.16 e o host
0.7.1. O motor original continua externo e inalterado. O título usa o ID
`FAKE78002`, separado do Playden (`FAKE78001`). A configuração usada no
console permitiu iniciar o título 0.1.

## Compilar no WSL/Linux

```bash
cd /mnt/c/Projects/Azimuth_PS5/title
make PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk
make test-tls
make test-gpu
make verify
```

O primeiro build baixa a revisão `9c0b994a048521af6fb84c73ded364504fe250e9`
do [PS5 Native App Boilerplate](https://github.com/mpereiraesaa/ps5-native-app-boilerplate).
Ela é a revisão fixada pelo script `build_native.sh` do prospero-win consultado
neste ambiente. O runtime acompanhante é reconstruído de código público duas
vezes, com comparação byte a byte e SHA-256 conferido pelo script dessa base.
O build não baixa firmware, jogo nem módulos proprietários.

Saídas:

- `dist/FAKE78002/`: pasta para instalação, com `eboot.bin`, `sce_sys/` e `sce_module/libc.prx`.
- `dist/Azimuth-PS5-title-0.84.zip`: a mesma pasta compactada.
- `build/eboot.elf`: ELF nativo `ET_SCE_EXEC_ASLR`.
- `build/dynamic-imports.txt`: imports por nome antes da conversão para NIDs.
- `build/self-inspection.txt`: inspeção do SELF e resultado de integridade.
- `dist/FAKE78002/build-info.json`: revisão da base e hashes dos arquivos.

O host mantém libc++, libc++abi, libunwind e a compatibilidade C do SDK,
ligados estaticamente. As chamadas de sistema resolvem para `libkernel` e
`libSceLibcInternal`; o `crt1.o` de payload não entra no título. O startup
nativo chama `_init_env`, os construtores e `main`. O script `eboot.ld`
deriva do layout de endereço alto da base e acrescenta os limites de unwind
que a libunwind do SDK exige. O hook opcional sem provider,
`__cxa_thread_atexit_impl`, fica nulo para selecionar o caminho por pthread
da libc++abi. O runtime emulado de TLS, derivado do SDK v0.45, mantém seu
armazenamento por mais uma rodada de limpeza, até que os destrutores C++
terminem de usar a lista. O teste local reproduz 0/64 destrutores com a
ordem antiga e verifica 64/64 na ordem correta com o ajuste.

**Correção de chamadas de plataforma no 0.2:** a libc C de compatibilidade
do SDK era ligada antes das bibliotecas do sistema. Isso incorporava
`mmap`/`mprotect` com instruções de syscall direta, próprias do modelo de
payload. Agora os imports de plataforma são resolvidos antes desse arquivo
de compatibilidade. O `gs` do host usa `sysarch` da libkernel no build de
título. `check_runtime.py` exige `mmap`, `mprotect`, `munmap` e `sysarch` como
imports e rejeita instruções de syscall direta no executável. O relatório
0.1 não contém o sinal da parada em memória; portanto sua causa exata ainda
precisa da próxima execução, embora esse caminho inadequado tenha sido
identificado no binário. O 0.2 instala tratamento de falhas no smoke e grava
o endereço solicitado antes de entrar em `mmap`.

**Dependências e parada planejada no 0.3:** o build de título usa
`sceSysmoduleIsLoadedInternal`/`sceSysmoduleLoadModuleInternal`, com os IDs
do SDK v0.45 (`AgcDriver=0x80000080`, `Agc=0x80000094`,
`VideoOut=0x80000022`), em vez de presumir o caminho do processo de payload.
O probe exige confirmação de carregamento e para antes do RADV se uma
dependência falhar. Só descarrega pelo Sysmodule os módulos que ele próprio
carregou; os que já existiam ficam sob controle do sistema. A parada
`carregado` agora termina com `[PAROU]` e uma descrição da etapa, sem
afirmar que a GPU foi testada nem que o mapeamento falhou. O teste Linux
confere essa parada e a execução Vulkan completa (0 divergências em 1024
palavras). O carregamento via Sysmodule ainda precisa do teste no PS5.

No relatório do mapeamento do 0.2, o descarregamento do RADV retornou
`0x80020003`; o probe registrou o erro e encerrou. Isso não confirma a
limpeza do módulo pela chamada, embora o processo tenha chegado ao fim.
O 0.3 continua registrando o resultado desse descarregamento.

**Diagnóstico no 0.4:** em C, o `fileno(report)` dos cabeçalhos FreeBSD do
SDK podia expandir para leitura direta de um campo privado de `FILE` antes
da primeira thread. Isso pressupõe um layout que não é o do runtime do
título. O probe agora chama `(fileno)(report)`, registra o descritor e testa
uma escrita direta. O build rejeita o uso dessas macros de stdio no objeto
do GPU probe. Na falha da thread principal do título, o tratador salva sinal,
endereço e 21 registradores e volta imediatamente por `siglongjmp`; o host
grava esses dados por `note` depois da recuperação. O teste Linux verifica
o diagnóstico tanto pelo caminho de payload quanto pelo caminho de título,
além da parada planejada e do buffer Vulkan. Essa versão melhora a coleta
de evidências; ainda não corrige uma causa identificada dentro do RADV.

**Carregamento normal no 0.5:** no build de título, os testes que executam
o módulo (`modulo`, `instancia` ou probe completo) usam
`sceKernelLoadStartModule` e seu handle para `sceKernelDlsym` e
`sceKernelStopUnloadModule`. O teste `carregado` continua sendo só mapeamento
bruto, sem executar entrada. No payload permanece a entrada manual usada
nas versões anteriores. O relatório registra endereços de `getenv`, `malloc`,
`__cxa_atexit` e `sceKernelDlsym` como âncoras; na falha do título consulta
`sceKernelGetModuleInfoFromAddr` para nome/base/offset do RIP. A causa da
leitura em `0x70` não está confirmada; esse build testa uma hipótese do loader.

**Submissão no 0.6:** o probe verifica e registra os retornos de início/fim
do command buffer e criação da fence, além de recusar fila/fence nulas.
`vkQueueSubmit` e `vkWaitForFences` têm etapas e registros separados, com
tempo do retorno da submissão. A finalização registra `vkDeviceWaitIdle`
e separa a destruição do dispositivo e da instância. Os tratadores são
reinstalados depois de criar o dispositivo, com `SIGSYS` incluído. A criação
do watchdog é conferida; seus avisos vão diretamente ao descritor, sem o
mutex do logger, e a thread é aguardada antes de fechar o relatório. O
estado compartilhado do watchdog usa atomics. No Linux, passaram o buffer
completo, os registros das chamadas, recuperação de SIGSEGV e de SIGSYS e
o encerramento do watchdog. A causa do fechamento no PS5 ainda não está
confirmada; o teste de execução de comandos permanece pendente.

## Instalar e testar

Copiar a pasta `FAKE78002` completa para `/data/homebrew/FAKE78002` (ou a raiz
de homebrew USB configurada no console) e registrar pelo mesmo fluxo de
títulos em pasta usado no console, por exemplo ShadowMountPlus. Preservar
permissão de execução no eboot e no módulo. O build local não instala nem
registra o título automaticamente.

O upload pode ser conferido sem conexão e depois executado por FTP:

```bash
python3 deploy.py --host IP_DO_PS5 --port 2121 --dry-run
python3 deploy.py --host IP_DO_PS5 --port 2121
```

Usar a porta real do servidor FTP do console. O script confere os hashes
do build, envia apenas para `/data/homebrew/FAKE78002` e tenta preservar
permissões de execução. Ele não registra nem abre o aplicativo.

A seleção é feita por `/data/azimuth-ps5/title-mode`, contendo uma palavra:

| Palavra | Execução | Relatório |
|---|---|---|
| `smoke` (padrão se o arquivo não existir) | Construtores, exceção C++/unwind, thread, TLS e seu destrutor; reservas de 64 KiB em `0x140000000` e `0x1000000000`, sem commit | `title.txt` |
| `gpu` | GPU probe completo, com calibração, watchdog e limpeza do 0.8 | `title.txt`, `gpu-probe.txt`, `gpu-stderr.txt` |
| `host` | Motor externo, usando os caminhos e `host-stop` já existentes | `title.txt`, `host.txt` |

Todos os relatórios ficam em `/data/azimuth-ps5`. `title.txt` grava cada etapa
do smoke antes de executá-la e registra o código final. O smoke mostra uma
notificação ao terminar; ainda não há apresentação via VideoOut.

Ordem de validação no console:

1. Abrir sem `title-mode`, conferir as seis verificações `[OK]` e `Fim do titulo: codigo=0`.
2. Escrever `host` em `title-mode` e `mapa` em `host-stop`; conferir o motor na base preferida. Remover `host-stop` para repetir a execução até o DXGI.
3. Escrever `gpu` em `title-mode`; usar `gpu-stop=carregado` para conferir o mapeamento sem chamar a entrada do RADV. Depois remover `gpu-stop` para testar a entrada e o Vulkan.
4. Confirmar instância, dispositivo, execução do `vkCmdFillBuffer` e conferência na CPU. Só após esse resultado integrar DXGI/vkd3d-proton.

O RADV continua externo: `/data/azimuth-ps5/radv/libvulkan.prx`, com fallback
para a instalação do Playden. O probe ainda chama manualmente a entrada
`+0x4ef0` do build conhecido, pois o 0.8 demonstrou `DT_INIT=0` nesse módulo.
Executar como título é a hipótese a validar; a GPU ainda não foi comprovada
neste projeto. O orçamento de vários GiB e a apresentação VideoOut também
continuam pendentes. O smoke só testa reservas pequenas de espaço virtual.
No build de título, o probe identifica a plataforma como `ps5-title` e não
encerra processos anteriores por nome/pid: `eboot.bin` pode identificar
outros aplicativos. O fluxo de payload mantém seu comportamento anterior.

## Verificações locais

O build exige todos os imports resolvidos pelos stubs públicos do SDK,
converte os metadados genéricos em metadados SCE e verifica o ELF e o SELF.
`verify.py` confere tipo, arquitetura, entrada, alinhamentos, ausência de
segmentos graváveis/executáveis e ausência de colisão com a base do motor.
O inspetor da base confere a integridade do contêiner SELF. Esses resultados
não substituem a execução no console.


## Título 1.28 — esperas e diagnóstico de write watch (30/10/2026)

A captura da 1.27 em 2160p permaneceu em 11,62 FPS (10,8–12,3 no trecho estabilizado); evidências em `../console-reports/2026-10-30-title-1.27-watch-212046/ANALISE.md`.

- WaitForSingleObject/Ex usam uma lista de tamanho fixo na pilha; o vetor de registro só é criado quando a espera realmente precisa dormir. Mantidas a trava compartilhada e as regras de consumo de eventos/timers.
- WakeByAddressSingle/All evitam a trava e a notificação quando o bucket não tem esperadores. Registro anterior à consulta e a trava continuam protegendo a transição para o sono. O broadcast do bucket continua necessário porque endereços diferentes compartilham o bucket.
- O host registra a cada ~10 s consultas, bytes, páginas e resets de GetWriteWatch. O rastreamento ainda é conservador: todas as páginas são devolvidas. É necessário medir antes de implementar uma alternativa correta.
- `make -C host test-threads` cobre eventos automáticos/manuais, consulta imediata, handle inválido, espera bloqueada e 200 disputas entre WaitOnAddress e wake.

Pacote: `dist/Azimuth-PS5-title-1.28.zip`. Ganho de FPS ainda depende de teste no console. O INI do console não é incluído nem modificado pelo pacote. Para testar velocidade de simulação independente do FPS, usar `frame_pacing=real-time` no INI, preservando uma cópia do original; isso não aumenta por si só os quadros/s.

Validação local da 1.28: test-threads e test-memory passaram no Linux/WSL. Build do PS5, verificação de runtime, ELF/SELF e empacotamento passaram. SHA-256 do eboot.bin: `2bc1d7ee6ecd4b6b36f6716ef8f1e596b0f4b05cd045faee1c01e671e175ad3f`.


## Título 1.29 — rastreamento real de escritas (30/10/2026)

Substitui o retorno de todas as páginas por detecção de escrita nas reservas MEM_WRITE_WATCH. O host remove temporariamente a permissão de escrita e o handler de SIGSEGV registra a primeira escrita, devolvendo a proteção solicitada. O rastreamento é integrado ao commit, VirtualProtect, decommit e release. Alterações de proteção preservam o histórico; decommit e reutilização o limpam.

O PS5 protege blocos de 16 KiB. Uma escrita marca conservadoramente as páginas Windows graváveis do bloco (até quatro de 4 KiB). O retorno continua em endereços de páginas de 4 KiB, pois o motor usa esses índices. Blocos sem escrita não são devolvidos. Consultas de blocos já limpos não fazem mprotect.

Reset reprotege antes de limpar o histórico, sob guarda por bloco: uma escrita posterior falha e é registrada na próxima época. O handler não aloca, não registra logs e não usa a trava da tabela de regiões. A guarda usa atomic_flag; seu trecho crítico não acessa buffers do motor. Falhas em páginas sem permissão de escrita continuam no tratamento normal de erros.

Metadados: tabela fixa de 524.288 entradas, aproximadamente 16 MiB de BSS; chaves de endereço persistem até o fim do processo e são reutilizadas quando o endereço retorna. Esgotamento faz a reserva falhar, sem desativar o rastreamento silenciosamente. Custo novo: faults e mprotect em blocos escritos desde o reset; o impacto real precisa ser medido no console.

Validação específica: `make -C host test-writewatch` roda com blocos de 4 KiB e com `AZIMUTH_BLOCK16K=1`. Cobre restauração do valor original, consultas sem reset, reset junto da consulta, ResetWriteWatch, proteções, decommit/recommit, buffer insuficiente, flags inválidas, 1.000 épocas com escritor de outra thread e 1.000 consultas/reset concorrentes com um escritor contínuo. Passou nas duas granularidades, incluindo os testes de I/O. Testes de memória e sincronização também passaram.

Semântica de referência: [GetWriteWatch](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-getwritewatch) e [ResetWriteWatch](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-resetwritewatch). O reset separado exige sincronização do chamador entre consulta e reset, conforme documentado pela Microsoft.

Pacote previsto: `dist/Azimuth-PS5-title-1.29.zip`. Ainda não validado no console. Não altera o INI nem o motor original. Comparar FPS e páginas devolvidas por intervalo no mesmo trecho do mundo; se houver falha, capturar host.txt e klog para distinguir fault de rastreamento de acesso inválido.

Leituras para buffers rastreados passam por um buffer intermediário do host (`ReadFile`, `_read` e `fread`), para que as escritas no destino sejam instruções de CPU e participem do rastreamento mesmo com reset concorrente. O kernel não entrega ao handler uma falha de copyout para uma página protegida. Incluído teste com arquivo temporário para as três rotas de leitura de baixo nível/CRT.

Build PS5 da 1.29 concluído: verificação de runtime, ELF/SELF e manifesto/pacote passaram. SHA-256 do eboot.bin: `9d79af9571cf3301cf46b5cb2ffdb2412a327b57f3b257b363716fb85a044639`. Logs locais em `title/build/test-1.29.log`, `test-writewatch-1.29.log` e `build-1.29.log`.

**Teste no PS5 da 1.29: FALHOU durante loading.** PID 415, 1440p/AA 4x. Primeiro SIGSEGV na thread 348 em 0x180044549, seguido de falha interna do handler e thread estacionada; parada posterior na thread 380. Nenhuma melhora de FPS no mundo foi validada. Evidências e análise: `../console-reports/2026-10-30-title-1.29-watch-215438/ANALISE.md`. Prioridade: preservar o primeiro fault sem leituras inseguras e registrar as proteções/metadados do bloco antes de alterar a lógica de recuperação.


## Título 1.30 — retomada pelo tratador do motor (30/10/2026)

A 1.29 falhou no loading. O rastreamento real permite ao motor selar regiões com VirtualProtect(PAGE_READONLY), expondo uma dependência de VEH que faltava no host. O motor registra FUN_1400dbb00 em AddVectoredExceptionHandler e, ao receber uma violação de escrita numa região selada, chama FUN_1400dba30 para reabri-la e retorna EXCEPTION_CONTINUE_EXECUTION. O host ainda tinha um stub que aceitava o registro e nunca chamava o callback. Essa dependência foi identificada no decompilado original (registro na linha 174230, filtro e unseal nas linhas 173955–174032).

O primeiro RIP capturado, 0x180044549, corresponde a msvcr110+0x3c549 (base carregada 0x180008000). A desmontagem da DLL local mostra `movnti %r9,(%rcx)`, escrevendo em RCX=0x104ac70000, igual ao endereço da falha. O primeiro teste da 1.29 não preservou as proteções desse bloco, portanto a confirmação da recuperação nesse caso concreto ainda depende do PS5.

A 1.30 implementa registro ordenado e remoção de handlers; despacha SIGSEGV do guest como STATUS_ACCESS_VIOLATION com EXCEPTION_POINTERS, EXCEPTION_RECORD e CONTEXT AMD64. Converte os registradores e o estado FXSAVE entre Linux/PS5 e Windows; só retoma se o handler retorna -1. O motor controla a alteração das proteções. A falha não é ignorada nem se força escrita numa página somente leitura sem a decisão do callback. Esse despacho cobre violações de acesso vindas do sinal; não substitui o unwinder de SEH e não acrescenta VEH ao caminho de RaiseException nesta versão.

Falhas de tracking em escritas feitas dentro de um callback são atendidas antes da guarda de recursão. A lista de handlers é percorrida sem mutex no sinal; remoção marca o nó inativo, mantendo os nós vivos até o fim do processo. O relatório periódico inclui acessos despachados e retomados pelo motor.

O diagnóstico fatal deixa de ler RDI e de percorrer a pilha sem validação. O primeiro registro já inclui trap e err. Na 1.29, RDI=0x60000 foi seguido de nova falha dentro do tratador e thread estacionada; a leitura insegura foi removida.

Teste `make -C host test-vectored`: reproduz a escrita MOVNTI numa região watch selada, callback reabrindo a região, tracking dentro do callback, prioridade/remoção, edição de RAX/RIP no contexto e falha não tratada com RDI=0x60000 em subprocesso com prazo. Roda com blocos de 4 e 16 KiB. Mantidos os testes de GetWriteWatch concorrente e I/O.

Referência de ordenação e retorno dos callbacks: [AddVectoredExceptionHandler](https://learn.microsoft.com/en-us/windows/win32/api/errhandlingapi/nf-errhandlingapi-addvectoredexceptionhandler) e [Vectored Exception Handling](https://learn.microsoft.com/en-us/windows/win32/debug/vectored-exception-handling).

Pacote: `dist/Azimuth-PS5-title-1.30.zip`. Ainda não validado no console. Primeiro repetir loading e entrada no mundo; somente depois comparar FPS e WriteWatch. Preservar a configuração usada para comparar versões.

Validação local da 1.30: test-vectored e test-writewatch passaram com blocos de 4/16 KiB. O teste fatal imprime propositalmente SIGSEGV e verifica a saída normal do subprocesso. Runtime, ELF/SELF e pacote/manifesto passaram. SHA-256 do eboot.bin: `db535d31f231de02b87adcdc954c32c9537cbf4bb3ac871043e53ef689b529c1`. Logs: `build/test-1.30.log` e `build/validation-1.30.log`.

**1.30 testada no PS5: usuário confirmou entrada no mundo e funcionamento do jogo.** Captura posterior da sessão PID 418 em 2160p/AA off/limite 30 registrou média 29,036 FPS, mínimo 26,6 e máximo 30,1 em 25 amostras novas. Persistem engasgadas; correlação inicial com streaming/gestão de memória, sem causa por frame estabelecida. Evidências: `../console-reports/2026-10-30-title-1.30-stutter-225142/ANALISE.md`.


## Título 1.31 — custo de proteção e medição das engasgadas (30/10/2026)

A captura da 1.30 em 2160p/AA off/limite 30 teve 29,036 FPS de média e duas amostras de 26,6. As quedas coincidiram com atividade de leitura, commit/free e SRW; ainda não há atribuição por frame. Evidências: `../console-reports/2026-10-30-title-1.30-stutter-225142/ANALISE.md`.

Mudanças:

- `Memory.cpp::commit` agrupa blocos contíguos com a mesma proteção numa chamada mprotect, fora de MEM_WRITE_WATCH. Preserva a união das proteções por bloco de 16 KiB e não inclui lacunas sem backing. Se a chamada agrupada falhar, aplica o caminho anterior por bloco, inclusive a migração de memória executável e o fallback RWX/RX do console. O contador periódico mostra blocos lógicos e chamadas ao kernel, incluindo tentativas que falharam.
- `WriteWatch.cpp` mantém a última proteção efetivamente aplicada a cada bloco sob a mesma guarda usada pelo tracking. Reaplicações idênticas não chamam mprotect. O cache é invalidado ao mudar a máscara de páginas committed, ao preparar/reutilizar/liberar metadados e se mprotect falhar. Reset ainda reprotege antes de limpar o histórico; a mudança não elimina faults necessários nem altera o mecanismo de unseal/VEH do motor. Contadores próprios mostram chamadas e reaplicações evitadas.
- Histograma de intervalos entre entradas de Present por swap chain: máximo exato, p95/p99 arredondados para cima em milissegundos, e contagens acima de 50/100 ms. Não conta DXGI_PRESENT_TEST nem o primeiro Present da cadeia. Percentis acima de um segundo aparecem como >1000 ms. A medição registra chamadas de apresentação, não timestamps de exibição física, e pode incluir loading, pausa e chamadas que depois falham. O relatório de ~10 s é uma janela aproximada devido à concorrência entre coleta e apresentação.
- Logs nominais de ReadFile lento não param após os primeiros 40 eventos: passam a registrar até aproximadamente quatro eventos por janela de dez segundos, com thread, duração e arquivo. O limite evita transformar o diagnóstico em um custo grande de I/O. É uma amostra, não uma lista completa de leituras lentas.

Validação: teste de memória inclui mudanças em faixas uniformes e separação de um trecho gravável entre bordas somente leitura, conferidas por tentativas de escrita em subprocessos. Roda também com AZIMUTH_BLOCK16K=1. Testes de WriteWatch/VEH cobrem reset concorrente, decommit/recommit e unseal. Teste do histograma usa uma distribuição conhecida com intervalos longos e reset das amostras. Os subprocessos que tentam escrever em páginas somente leitura devem terminar com SIGSEGV; isso é o resultado esperado do teste.

O efeito nas engasgadas e a aceitação de faixas maiores pelo kernel do PS5 precisam de teste no console. Pacote: `dist/Azimuth-PS5-title-1.31.zip`. Não altera o INI. Comparar no mesmo cenário/configuração; usar os contadores de intervalos longos junto dos eventos de I/O e de memória, sem atribuir uma causa apenas pela média de FPS.

Validação local concluída: histograma, memória com proteções mistas em 4/16 KiB e regressões WriteWatch/VEH passaram. No teste de memória em 16 KiB, 1.165 blocos foram cobertos com 7 chamadas mprotect (resultado local, não benchmark do jogo). Runtime, ELF/SELF e manifesto/ZIP passaram. SHA-256 eboot.bin: `b862191d8e45529110d6fc6052a968df03dab0dfa2cc03bac0ca258000a9946b`. Logs: `build/test-1.31.log`, `build/test-writewatch-1.31-final.log`, `build/build-1.31.log`.


## Título 1.32 — leitura maior para memória rastreada (30/10/2026)

A captura da 1.31 a 2160p/MSAA 4x/60 FPS registrou 127 intervalos acima de 50 ms (36 acima de 100), máximo 198,55 ms, junto de streaming de GeoChunk2/3 na thread 348 e picos de proteção do WriteWatch. O buffer de 8 KiB fragmentava uma leitura rastreada de 4 MiB em cerca de 512 pread/cópias.

A 1.32 usa buffers de 256 KiB do alocador do host, reutilizados por thread, nos caminhos watchedPread, watchedRead/_read e fread. Uma leitura de 4 MiB passa a precisar normalmente de 16 operações da ponte. Buffers ficam fora da pilha das fibras. Cada operação mantém uma reserva exclusiva durante a cópia: um VEH que reentre em I/O adquire outro buffer, sem sobrescrever a origem da operação interrompida. Até quatro buffers são retidos por thread (1 MiB de dados); reentrada adicional ou falta de memória usa o buffer anterior de 8 KiB na pilha. Os buffers retidos são liberados ao sair da thread.

As escritas no destino continuam sendo instruções normais de CPU. Não se abre memória somente leitura antecipadamente nem se suprime tracking: reset concorrente e unseal pelo VEH mantêm os caminhos da 1.30/1.31. O número de faults/proteções pode continuar alto; a mudança reduz fragmentação de leitura, não elimina o custo por página.

Nova linha `I/O rastreado`: número de operações da ponte (pread/read/fread), bytes, tempo acumulado e máximo da etapa de leitura e da cópia, além do uso do buffer reduzido. A etapa de cópia inclui faults, callbacks VEH e desescalonamento. Tempos são decorridos, agregados entre threads; podem se sobrepor, inclusive com I/O aninhado. O tempo de aquisição/alocação do buffer e a busca inicial de metadados não entram nessas duas etapas. Para fread, uma operação não equivale necessariamente a uma syscall. O relatório é uma janela aproximada de dez segundos.

Testes ampliados:

- Conteúdo e cauda de um arquivo de 4 MiB + 123 bytes, pread em laço, _read e fread com item parcial no EOF; posição do arquivo, EOF sem escrita e erro EBADF preservado.
- Reset concorrente com a cópia de leitura grande, mais todos os testes anteriores de WriteWatch (4 e 16 KiB).
- VEH reentrando em watchedPread durante uma cópia para memória selada. A leitura aninhada usa dados diferentes, e o teste confere que todo o resultado externo permanece intacto. Mantidos os testes de MOVNTI, contexto e falha não tratada.
- Regressão de memória/proteções mistas também com blocos de 16 KiB.

Pacote: `dist/Azimuth-PS5-title-1.32.zip`. Impacto nas engasgadas ainda precisa de teste no console com a mesma configuração. Comparar contagem de intervalos >50/100 ms e os tempos separados de leitura/cópia. Não interpretar a redução de chamadas como aceleração proporcional do jogo.

Validação local da 1.32 concluída: WriteWatch, I/O grande e reentrante via VEH, EOF/EBADF, reset concorrente e memória/proteções mistas passaram com blocos de 4/16 KiB. O teste de 4 MiB + 123 bytes usou 17 chamadas pread, com comparação de conteúdo integral. Runtime, ELF/SELF e manifesto/ZIP passaram. SHA-256 do eboot.bin: `dfcd09e77243352cac56c7ffdab08ca800ddd7953a48bad1f3d0a517cd169d01`. Logs em `build/test-1.32.log` e `build/build-1.32.log`.

**Teste da 1.32 no PS5:** usuário confirmou persistência das engasgadas. Captura PID 421 em 2160p/MSAA 4x/60 teve média 57,05 FPS, 120 intervalos >50 ms e máximo 198,26 ms em 24 janelas novas. Buffer maior em uso, zero fallback; cópia para memória rastreada somou 2,46 vezes o tempo da leitura na ponte. Isso não identifica ainda a causa de cada pausa. Evidências: `../console-reports/2026-10-30-title-1.32-stutter-232923/ANALISE.md`.


## Título 1.33 — WriteWatch em lotes (30/10/2026)

A 1.32 manteve as engasgadas: a ponte de I/O somou 2,46 vezes mais tempo na cópia que na leitura, com 943.622 mprotect do WriteWatch em 24 janelas. A 1.33 ataca as proteções e os faults, sem voltar à resposta sempre suja.

- Preparação das cópias de I/O em lotes: só páginas já committed e graváveis segundo as permissões solicitadas pelo motor podem ser preparadas. Os blocos são marcados como sujos conservadoramente e faixas contíguas com a mesma proteção são abertas por uma chamada. Páginas seladas/somente leitura não são liberadas: a escrita continua pelo fault/VEH do motor. As guardas são liberadas antes da memcpy.
- Consulta com reset e ResetWriteWatch reprotegem faixas contíguas antes de limpar o histórico. Um escritor posterior precisa esperar as guardas e é registrado na próxima época. Apenas as páginas realmente devolvidas são limpas quando o buffer é insuficiente; as restantes mantêm o histórico e a proteção adequada.
- No máximo 32 blocos são guardados por lote, sempre em ordem crescente de endereço. Não se toca em memória do guest sob essas guardas. Metadados permanecem vivos no processo. Uma recusa da operação agrupada pelo kernel invalida os caches dos blocos envolvidos e usa a proteção individual como fallback.
- Nova linha `Etapas WriteWatch`: operações, tempo decorrido acumulado e máximo de mprotect, faults de tracking, consulta, reset e preparação de cópia. Também registra lotes bem-sucedidos e blocos agrupados. O relógio é inicializado antes das guardas. Erro de mprotect preserva errno.

Tempos das etapas se sobrepõem: mprotect está dentro de fault/consulta/reset/preparo, e preparo está dentro do tempo de cópia da ponte. Não somar os campos como tempos exclusivos de CPU. Incluem contenção/desescalonamento; o relatório de dez segundos é uma janela aproximada, não atribuição exata por frame. O contador de fault cobre tentativas de tracking com permissão de escrita, não os callbacks de reabertura do motor, que continuam no relatório VEH.

A preparação pode marcar vizinhas graváveis do mesmo bloco físico como sujas, assim como a recuperação anterior por fault. Reset concorrente pode rearmar uma página após a preparação; nesse caso, a cópia volta a produzir o fault normal e a escrita não é perdida. Uma falha de preparação não é tratada como uma cópia concluída: a cópia comum e seu caminho de falha permanecem ativos.

Testes: cópia grande com reset concorrente, EOF/erro e conteúdo integral; buffer insuficiente em reserva maior que um lote; histórico das páginas não devolvidas; memcpy em região selada com VEH reentrando em I/O; MOVNTI, contexto e falha não tratada; memória/proteções mistas. Teste com `--wrap=mprotect` simula recusa de faixas maiores que um bloco e verifica o fallback na preparação e no reset. Roda com blocos de 4 e 16 KiB.

Na cópia inicial do teste local de 4 MiB + 123 bytes com blocos de 16 KiB, a preparação usou 17 mprotect, com 16 lotes cobrindo 256 blocos, e zero faults durante a cópia. É um teste local isolado, não um benchmark do jogo nem garantia de redução das engasgadas no PS5.

Pacote: `dist/Azimuth-PS5-title-1.33.zip`. Validação no console pendente. Comparar na mesma configuração (2160p/MSAA 4x/60 FPS) e trecho do cenário, principalmente contagens de intervalos >50/100 ms e os tempos das etapas. Faixas maiores e maior quantidade de blocos guardados por vez podem ter custos diferentes no kernel do console; os testes locais não medem esses custos.

Validação local da 1.33 concluída: regressões de memória, WriteWatch/VEH, reset concorrente, buffer insuficiente maior que um lote e fallback de mprotect agrupado recusado passaram em 4/16 KiB. Runtime, ELF/SELF e manifesto/ZIP passaram. SHA-256 do eboot.bin: `fd1db897fd06a710a51242d3dd6242a58e48ef6852f190e3a11496a3bca58670`. Logs: `build/test-1.33.log`, `build/test-writewatch-1.33-final.log`, `build/test-writewatch-1.33-fallback.log`, `build/build-1.33.log`.


## Título 1.34 — selagem e reabertura de regiões em lotes (31/10/2026)

Usuário confirmou a 1.33 instalada e persistência das engasgadas. Não houve captura dessa versão: console desligado na tentativa de 31/10. Não atribuir melhora/regressão à 1.33 sem os contadores novos.

A revisão encontrou um caminho que ainda usava uma chamada de proteção por bloco: `Memory.cpp::commit`, usado por VirtualProtect/commit/decommit de regiões MEM_WRITE_WATCH. A 1.33 agrupava cópias e consultas/reset, mas a selagem e a reabertura pelo motor continuavam individuais.

A 1.34 prepara até 32 descritores de blocos contíguos, calcula a proteção efetiva conforme dirty/permissões de cada bloco e agrupa faixas adjacentes iguais. Blocos sem backing não entram numa faixa. As guardas seguem ordem crescente e não acessam buffers do guest; o callback VEH continua responsável pela reabertura de páginas seladas. Mudanças de backing invalidam o cache, páginas que permanecem committed preservam o histórico; decommit limpa os bits correspondentes. Se o kernel rejeitar a faixa, tenta blocos individuais.

Teste ampliado mede chamadas em selagem/reabertura de uma região de 4 MiB + 123 bytes, verifica conteúdo e histórico, páginas somente leitura e bordas de lotes, além de recusa simulada de faixas. Mantidos os testes de reset concorrente, I/O reentrante via VEH e memória com permissões mistas em 4/16 KiB.

Pacote: `dist/Azimuth-PS5-title-1.34.zip`. O efeito no jogo permanece pendente de teste no PS5; não altera INI nem motor. A telemetria de etapas e mprotect da 1.33 continua ativa. Comparar no mesmo cenário em 2160p/MSAA 4x/60 FPS, junto dos intervalos >50/100 ms.

Validação local concluída: WriteWatch/I/O e VEH passaram em 4/16 KiB; memória/proteções mistas passaram nas duas granularidades. Na região de 4 MiB + 123 bytes, cada selagem/reabertura usou 33 chamadas para 1.025 blocos de 4 KiB, ou 9 para 257 blocos de 16 KiB, contra uma por bloco anteriormente. Isso mede a redução de syscalls no teste, não ganho de FPS. Runtime nativo, ELF/SELF e manifesto/ZIP (12 arquivos) conferidos. SHA-256 eboot.bin: `bd0ce932273bddea1e52c51bfabaca0af7c2bb7b2e1ee4ea8f8744e5b3be9616`. Logs: `build/test-1.34.log` e `build/build-1.34.log`. Script de captura preparado em `../console-reports/watch-1.34-stutter.py`. Console pendente.


## 31/10/2026 — captura da 1.34: melhora com pausas residuais

PID 661, 2160p/MSAA 4x/high/60/fixed/vsync. Cinco minutos, 30 janelas novas: média 59,15 FPS (55,4–60,1), 36 intervalos >50 ms e três >100, máximo 138,39 ms. Sem fatal/GPU fault registrado. Cópia rastreada somou 532,02 ms para 1,857 GB, máximo 0,51 ms; leitura 3.811,64 ms. 384.166 mprotect e 341.165 faults ainda somam custo, mas nenhum evento individual medido explica a maior pausa. 30.053 avisos repetidos XMA em stderr são pista secundária, sem causalidade estabelecida. Comparação observacional com 1.31/1.32 indica menos pausas; não há A/B controlado nem captura da 1.33. Relatório: console-reports/2026-10-31-title-1.34-stutter-233452/ANALISE.md. Captura encerrada.


## Título 1.35 — escritas grandes da CRT e diagnóstico repetido (31/10/2026)

A captura da 1.34 registrou melhora relatada pelo usuário, média de 59,15 FPS e 36 intervalos >50 ms em cinco minutos, mas ainda 341.165 faults de rastreamento. A cópia do I/O já somou apenas 532,02 ms; nenhuma operação individual medida explica a pausa máxima de 138,39 ms. Havia também 30.053 avisos XMA idênticos em stderr (91,3% das linhas novas). Relatório: `../console-reports/2026-10-31-title-1.34-stutter-233452/ANALISE.md`.

A 1.35 estende a preparação em lotes ao destino das chamadas importadas memcpy/memmove/memset com pelo menos 16 KiB quando o destino cruza memória rastreada. Abre apenas páginas committed com permissão de escrita do motor, marcando os blocos correspondentes como sujos conservadoramente. As guardas são liberadas antes da operação real; páginas seladas continuam dependendo do VEH e reset concorrente continua produzindo faults. A ordem e a semântica de sobreposição de memmove são mantidas pela biblioteca. Operações pequenas e destinos sem tracking mantêm a cópia normal. Escritas implementadas dentro das DLLs ou inline pelo motor não passam por esta ponte e não são antecipadas. Não marca regiões inteiras sujas, não desativa tracking nem altera o motor original.

Nova linha `Escritas CRT rastreadas` conta operações/bytes por função que passaram pela preparação, para confirmar o uso no console. O contador não afirma que todas as páginas foram abertas: páginas seladas são puladas. A etapa `preparo` agora inclui I/O e essas chamadas CRT; os tempos de cópia no relatório de I/O continuam cobrindo somente a ponte de leitura.

Somente o diagnóstico stderr `[xma2 @ <endereço hexadecimal>] Could not update timestamps for skipped samples.` é compactado. A primeira ocorrência após cada relatório periódico continua integral, as seguintes são contadas, e `Avisos de audio` informa ocorrências/compactados a cada ~10 s. Outros erros de áudio, stdout e diagnósticos permanecem no log. Isso reduz a fila e a escrita dos avisos; não corrige o decodificador nem remove a formatação original que acontece antes da ponte.

Testes ampliados cobrem cópias grandes, memset, memmove com sobreposição nos dois sentidos, faixas desalinhadas, conteúdo/bytes vizinhos, histórico dirty, operações vazias, recusa de faixas pelo kernel e reset concorrente. VEH testa as três funções contra páginas seladas e mantém os testes anteriores de I/O reentrante. O filtro tem teste de correspondência exata, preservação de outras mensagens e contagem concorrente durante relatórios.

Pacote: `dist/Azimuth-PS5-title-1.35.zip`. Ganho nas pausas precisa ser medido no PS5 com a mesma configuração. Não há atribuição causal definitiva das engasgadas. INI não alterado. Captura preparada em `../console-reports/watch-1.35-stutter.py`.

Validação local da 1.35 concluída: filtro exato de áudio com contagens concorrentes; WriteWatch/CRT, VEH e memória/proteções mistas com blocos de 4/16 KiB passaram. No teste CRT de 4 MiB + 123 bytes, memcpy usou 33 chamadas mprotect para 1.025 blocos de 4 KiB e 9 para 257 blocos de 16 KiB. Testes de sobreposição/concorrência e retorno pelo VEH das três funções passaram. Isso é resultado local, não medição de FPS do jogo. Runtime nativo, ELF/SELF e os 12 hashes do manifesto/pacote conferidos. SHA-256 eboot.bin: `cd1fcbbce1206118b68e5c75550cc50a7a7e4a9aed907f1bc125bb392270af1a`. Logs: `build/test-1.35.log` e `build/build-1.35.log`. Console pendente.


## Título 1.36 — contenção de handles e sinalização (01/11/2026)

Usuário reportou engasgos persistentes na 1.35. A consulta de 01/11 ocorreu com jogo fechado (confirmado pelo usuário): todos os snapshots repetiram o mesmo host, zero janelas novas. O histórico contém 25 relatórios CRT com todas as contagens zeradas; as funções importadas grandes não foram usadas para destinos rastreados nessa sessão. O filtro XMA está ativo, mas isso não estabeleceu a causa das pausas. Evidência: `../console-reports/2026-11-01-title-1.35-stutter-000118/ANALISE.md`.

A tabela de handles era um único mapa/mutex compartilhado por eventos, semáforos, arquivos e todas as outras consultas. A 1.36 divide a tabela em 64 mapas/travas por índice do handle, mantendo a numeração monotônica sem reciclagem. Consultas continuam retornando shared_ptr: um close remove o nome, mas não destrói o objeto ainda referenciado por outra operação. Destrutores são executados fora da trava, inclusive quando reentram na mesma partição. A métrica `Disputa de handles` registra falhas do try_lock, tempo decorrido até obter a trava e máximo; não é tempo de CPU nem medição da espera nos objetos. Relatórios são janelas aproximadas e seus campos são lidos separadamente.

SetEvent, ReleaseSemaphore, conclusão de thread, evento interno do host, timer, ReleaseMutex e postagem de completion port notificam esperas na mesma seção protegida que altera o estado. Evita soltar e readquirir waitLock só para percorrer os aguardantes. A espera continua verificando/consumindo o estado sob essa mesma trava; não há caminho sem proteção que possa perder uma notificação. Mutex recursivo só notifica quando o último nível é liberado. A trava global das esperas continua existindo e não se alteram regras de consumo ou wait-all.

Novo diagnóstico de quadros >50 ms: linha `Quadro lento` com thread, fim do intervalo no relógio monotônico, intervalo e duração dos Present anterior/atual, limitada a uma linha por segundo por swap chain. Histograma continua contando todos os intervalos; a linha é amostrada, entre entradas da API, não exibição física. Present atual ocorre depois do intervalo e não é sua causa; o anterior pode integrá-lo. O custo de enfileirar a linha acontece depois da chamada nativa e pode entrar no próximo intervalo.

VirtualAlloc2 (inclui VirtualAlloc), VirtualProtect e VirtualFree >2 ms guardam API, thread, início/fim monotônico, endereço e tamanho pedido em 64 slots pré-alocados, sem alocar/logar no produtor. O watchdog drena como `Memoria lenta`; o prefixo de log é do momento da drenagem, por isso correlacionar pelos campos monotônicos internos. Eventos descartados por slots ocupados são contados. Tempo inclui busca/espera por travas e diagnóstico da API, não só kernel. Em MEM_RELEASE o tamanho pedido pode ser zero; isso não significa zero bytes liberados. Sobreposição entre eventos de threads não prova caminho crítico.

Testes novos: 80 mil ciclos concorrentes de handles, lookup/close concorrente, referência que sobrevive ao close e destrutor reentrante na mesma partição. Sincronização cobre 2.000 passagens de eventos, wait-all preservando consumo, 1.000 permissões de semáforo, recursão de mutex com outra thread, timer, evento interno, completion port e 200 corridas de WaitOnAddress. Mantidas regressões de arquivos, WriteWatch/CRT/VEH, memória/proteções mistas e histograma.

Pacote: `dist/Azimuth-PS5-title-1.36.zip`. Efeito nas engasgadas ainda não medido; não prometer ganho de FPS. Motor/INI inalterados. Script `../console-reports/watch-1.36-stutter.py` preparado para o próximo teste com jogo aberto.

Validação local da 1.36 concluída: handles e sincronização ampliados, arquivos (300 handles com limite de 200 descritores), WriteWatch/CRT e VEH com blocos 4/16 KiB, memória/proteções mistas nas duas granularidades e histograma passaram. Logs de falha fatal dos subprocessos de VEH e SIGSEGV das escritas propositalmente proibidas são esperados pelos testes. Build PS5/runtime/ELF/SELF e 12 hashes do manifesto e ZIP conferidos. SHA-256 eboot.bin: `7d38d9a0483fb87c400bc84ecc083256971e78e0124a2af43b0f61a7ebe9dde5`. Logs: `build/test-1.36.log` e `build/build-1.36.log`. Console pendente; não há benchmark de FPS desta alteração.


## Capturas da 1.36 e pedido de FPS em 100 ms (01/11/2026)

Coleta inicial de cinco minutos incluiu transição e direção: 30 janelas, média 59,24 FPS, 33 intervalos >50 ms (4 >100), máximo 194,67 ms. Recorte conservador após confirmação de direção: 14 janelas, média 59,29, dez >50 e máximo 92,25 ms. Mínimo médio de 57,3 não é o mínimo instantâneo. Segunda coleta só de gameplay, solicitada pelo usuário: ~121 s, 12 janelas, média 59,59, 12 intervalos >50 e máximo 73,75 ms. Usuário observou 47 FPS no overlay; o host 1.36 não tem resolução temporal para confirmar/refutar esse valor a partir da média de dez segundos. Snapshot adicional do marcador preservado separadamente.

Disputa individual da tabela de handles permaneceu abaixo de 0,1 ms, sem explicar as pausas grandes por si só. Eventos de memória registrados são dominados por VirtualFree em outras threads; houve sobreposição com quadros longos, sem provar dependência causal. Buffers de memória perderam eventos por lotação. Relatórios: `../console-reports/2026-11-01-title-1.36-stutter-134350/ANALISE.md` e `../console-reports/2026-11-01-title-1.36-gameplay-2min-135121/ANALISE.md`. Ambas as capturas encerradas.

## Título 1.37 — FPS a cada 100 ms (01/11/2026)

A pedido do usuário, nova linha `FPS 100ms` em uma thread de amostragem independente, com cadência nominal de 100 ms. Registra FPS pela contagem de entradas reais de Present sem PRESENT_TEST e pelo tempo efetivamente decorrido, número de quadros, duração real da janela, maior intervalo encerrado nela, tempo sem quadro novo e timestamps monotônicos. Quando a apresentação para, a amostragem continua e mostra zero quadros/zero FPS e a idade do último quadro; não precisa esperar o próximo Present para revelar o travamento.

A janela começa a ser publicada depois da primeira apresentação. Se o amostrador for desescalonado, registra a duração real e não fabrica janelas atrasadas para fingir precisão de 100 ms. Não é garantia de tempo real. Contagens em 100 ms têm quantização (~10 FPS por quadro em uma janela exata de 100 ms); usar também max intervalo e tempo sem quadro, sem apresentar isso como FPS instantâneo exato de cada frame.

O registro é assíncrono, reutiliza o logger existente e sua política de sync a cada até 500 ms; não faz fsync por amostra nem aloca/loga no callback de gravação do frame. Histograma e diagnósticos anteriores de ~10 s são mantidos com contadores independentes. As fronteiras dos campos atômicos são aproximadas em concorrência: um frame/intervalo pode aparecer no campo da janela vizinha. Contagem entre chamadas da API não é timestamp físico de exibição, nem prova espera/ociosidade da GPU. Contadores agregam swap chains; não contam DXGI_PRESENT_TEST, mas podem incluir chamadas normais que depois falhem.

Testes determinísticos cobrem 60/30/10/zero FPS, janela sem novo Present, intervalo de 250 ms e amostrador atrasado por 500 ms, com denominador real; histograma anterior preservado. Pacote `dist/Azimuth-PS5-title-1.37.zip`; não é uma correção adicional de performance, é a resolução de diagnóstico pedida. Requer instalação para produzir os novos registros. Motor/INI inalterados.

Validação local 1.37 concluída: teste de janelas de 100 ms (60/30/10/zero FPS, stall, intervalo longo e amostrador desescalonado) e regressão do histograma passaram. Build PS5/runtime/ELF/SELF e 12 hashes do manifesto/ZIP conferidos. SHA-256 eboot.bin: `0905c482e26f60ab577f538e02cce0e526134990e72c8309ab12ed6a352226a8`. Logs `build/test-1.37.log` e `build/build-1.37.log`. Console pendente. Próxima coleta de dois minutos preparada em `../console-reports/watch-1.37-gameplay-2min.py`; extração das amostras em `../console-reports/analyze-fps-100ms.py`. Nenhum monitor ativo ao finalizar esta entrega.


## 01/11/2026 — captura 1.37: quedas curtas agora mensuradas

Um minuto de gameplay, PID 904, 2160p/MSAA 4x/high/60/fixed/vsync: 600 janelas de ~100 ms, média ponderada 59,05 FPS, mínimo 10 FPS em uma janela de um quadro/100,001 ms; 19 janelas com até quatro quadros. Isso não significa 10 FPS sustentados. Maior intervalo 85,169 ms, amostrador regular (99,933–100,076 ms), sem janela de zero quadros. Agregados antigos mostravam mínimo médio de 57,9: confirmado que ocultavam as quedas curtas. Sem fatal/GPU fault registrado. Eventos lentos de memória continuam dominados por VirtualFree, com 277 descartados por lotação e sem causalidade por frame provada. Relatório: console-reports/2026-11-01-title-1.37-gameplay-1min-140105/ANALISE.md. Captura encerrada.


## Título 1.38 — liberação de memória direta em faixas (01/11/2026)

A captura de 100 ms da 1.37 confirmou quedas fortes e curtas: um quadro em 100 ms (10 FPS nessa janela), apesar da média 59,05. VirtualFree predominou entre os eventos lentos de memória registrados. Isso não estabelece causalidade por frame, mas a revisão encontrou um custo concreto: unback removia o mapa virtual, liberava memória física e recriava a reserva para cada bloco de 16 KiB; em MEM_RELEASE, a reserva recém-criada era removida novamente no fim.

A 1.38 mantém identidade própria para cada alocação física. Só agrupa blocos contíguos tanto no endereço virtual quanto no físico e da MESMA alocação; proximidade física de alocações diferentes não basta. Lacunas, backing flexível e fragmentos após decommit/recommit separam os trechos. O planejador percorre entradas existentes e pula lacunas sem backing, sem varrer cada bloco de uma reserva grande vazia.

Em MEM_RELEASE, remove a reserva nativa inteira uma vez (incluindo lacunas reservadas), libera os trechos físicos e não recria reservas. Usa a extensão nativa arredondada da reserva, cobrindo também o último bloco físico de reservas pequenas. Em decommit/preservação de placeholder, mantém a semântica anterior de atuar nos blocos nativos inteiros dentro da faixa, repondo reservas no mesmo endereço. Não toca no conteúdo/permissões dos blocos vizinhos.

Chamadas de faixa recusadas têm fallback por bloco. Uma falha individual não é tratada como sucesso: preserva a propriedade das páginas físicas ainda não liberadas e interrompe o guest, pois a operação pode já ter alterado parcialmente o mapeamento e não é seguro prosseguir fingindo rollback. Essa proteção substitui o descarte silencioso de resultados dessas liberações; não afeta o fluxo normal de sucesso. O tracking/VEH e a trava de lifetime das regiões permanecem.

`Liberacao direta` mostra contagens de unmap, release, reservas refeitas, bytes físicos liberados e releases agrupados pelo novo caminho (~10 s). Inclui tentativas de fallback nas chamadas; bytes contam apenas releases bem-sucedidos. Não inclui migração executável por toFlexible nem outras liberações fora de unback. A amostragem FPS de 100 ms permanece ativa. Buffer de eventos lentos ampliado de 64 para 1.024 slots para reduzir as perdas vistas nas capturas; descartes continuam contados.

Teste do MESMO algoritmo usado pelo backend nativo, com kernel simulado: 4 MiB de backing mais uma lacuna reservada liberados com um unmap, um release físico e zero novas reservas (o caminho antigo executava três chamadas por bloco alocado e um unmap final). Cobertura inclui decommit/recommit, alocações fisicamente adjacentes distintas, backing físico descontínuo, memória flexível, lacunas, limites parciais, recusa de faixas, falhas individuais e cem layouts fragmentados. Esses testes verificam plano, propriedade e estados; não simulam a latência/aceitação real do kernel do PS5.

Pacote `dist/Azimuth-PS5-title-1.38.zip`. Efeito nas pausas precisa de teste no console. Não afirmar aceleração proporcional ao número de syscalls. Motor/INI inalterados. Captura de um minuto preparada em `../console-reports/watch-1.38-gameplay-1min.py`.

Validação local da 1.38 concluída: algoritmo de liberação direta com recusa de faixas/falhas individuais, memória/proteções mistas em 4/16 KiB, WriteWatch/CRT e VEH nas duas granularidades e janelas FPS de 100 ms passaram. Runtime nativo/ELF/SELF e 12 hashes do manifesto/ZIP conferidos. SHA-256 eboot.bin: `46c3e3dcb43979c4de9754db1480cbfdc8abcc9fe1a507916f61190dd01f6aee`. Logs `build/test-1.38.log`, `build/test-direct-release-1.38-final.log` e `build/build-1.38.log`. Console pendente; nenhum monitor ativo.


## Título 1.39 — Azimuth Launcher (01/11/2026)

O usuário confirmou que a 1.38 ficou funcionando corretamente após a otimização de liberação direta. Confirmação qualitativa do usuário; sem nova captura quantitativa desta versão.

Nome exibido no menu PS5 atualizado de Azimuth PS5 - Diagnostic para **Azimuth Launcher**, incluindo localização pt-BR e notificações do título. Ícone anterior de bússola substituído pelo símbolo Azimuth branco sobre fundo escuro. SVG editável em `assets/azimuth-symbol.svg`, baseado na marca mostrada na página oficial de Almost There Games, com fonte/referência documentadas em `assets/README.md` e `THIRD_PARTY.md`. O gerador `make_icon.py` rasteriza o SVG em PNG RGB 512 x 512 com antialiasing, sem dependências externas; o build gera e empacota o ícone atualizado.

Mantidos titleId FAKE78002, caminhos/configurações e o motor 0.9.7 sobre o host da 1.38; não houve migração para o motor 1.0 neste pacote. Versão de conteúdo 01.000.139. Pacote: `dist/Azimuth-Launcher-1.39.zip`. Alteração de identidade visual; ícone inspecionado localmente, apresentação no menu do console pendente de reinstalação/atualização do título.

Validação local da 1.39: nome em en-US/pt-BR, PNG RGB 512 x 512 (CRCs e conteúdo), inspeção visual, runtime/ELF/SELF e os 12 hashes do manifesto/ZIP conferidos, incluindo nome e ícone efetivamente dentro do arquivo distribuído. SHA-256 eboot.bin: `66a03b7f919825277cd5daca429a132166695fa0ceee8934febacbb5841d782f`; icon0.png: `750c86ffe4d6874d8f31d709d6476526ae0c142f165adaa1742d981388179f07`. Log `build/build-1.39.log`. Sem alteração funcional no host de jogo; não foi necessário repetir testes de performance nesta mudança visual.


## Título 1.40 — commit de memória direta em pedaços (01/11/2026)

**Teste da 1.39:** jogo liso em 4K, MSAA 4x e 60 FPS, mas depois de ~10 minutos ele caiu: `Commit: memoria direta esgotada` e, na mesma hora, `SIGSEGV` na thread 380 escrevendo no endereço 0, logo depois de um `VirtualAlloc`. O maior trecho contíguo de memória direta livre caiu de 12 GB no início para 3 MiB, enquanto o alocador do host ficou estável em ~5,2 GB. Como o commit pedia um único trecho físico do tamanho de toda a faixa, a fragmentação bastou para ele falhar. Análise em `console-reports/2026-11-01-title-1.39-crash/ANALISE.md`.

A 1.40:
- **Commit em pedaços.** Primeiro tenta o trecho contíguo inteiro, exatamente como antes (o caminho normal não muda, para não trazer de volta os engasgos). Só se falhar, a faixa é preenchida com os maiores pedaços livres, cada um com a sua identidade de alocação para o `unback`. Até quatro novas tentativas se outra thread pegar o trecho entre a consulta e a alocação. Se não sobrar memória alguma, a falha registra o tamanho pedido, o maior trecho livre e o total em uso.
- **Diagnóstico:** a linha "Memoria flexivel" ganha a "memoria direta em uso" (soma das faixas alocadas pelo processo, `sceKernelDirectMemoryQuery`, até 8192 faixas, só na thread de vigia). O log "Commit em N pedacos" aparece no primeiro caso e depois a cada 64.
- **Build:** o trabalho do Codex para o motor Azimuth 1.0.2 (DirectWrite e outros imports novos) estava pela metade e quebrava o link. O stub antigo de `DWriteCreateFactory` em `LauncherApi.cpp` saiu, e `dwrite_abi.c` e `Dwrite.cpp` entraram no `build.sh` e no `host/Makefile`. O motor 0.9.7 do console não importa nada disso. O suporte ao 1.0.2 continua inacabado.

contentVersion 01.000.140. Pacote: `dist/Azimuth-Launcher-1.40.zip`. SHA-256 do eboot.bin: `e9264d3b161ca0e5b9e58fc5c55eb77da3ccc26f56c111b0ae996702bd218d7d`. Sem teste no Linux (pedido do usuário); console pendente.

Próximo teste: jogar mais de 15 minutos. Se a queda não voltar, conferir no `host.txt` quantos "Commit em N pedacos" aconteceram e a "memoria direta em uso" ao longo da sessão. Se o uso crescer sem parar, há também um vazamento a atacar.


## Título 1.41 — motor Azimuth 1.0.2 (01/11/2026)

O Codex deixou pela metade o suporte ao novo motor Azimuth 1.0.2 (`C:\Games\PC\FH2\azimuth.exe`, 9.715.712 bytes, SHA-256 `42527964f48c90a537f2414200b8598ab9a6f92919bfbe046dbecccb36e7c597`). A "Etapa 1" do `host/src/main.cpp` já aceitava os dois perfis (0.9.7 e 1.0.2) pelo SHA-256, e o DirectWrite, o DirectInput, o pool de trabalho/timers, a configuração de tela, o `MapViewOfFile` e os demais imports novos já tinham implementação. Comparando a tabela de imports do 1.0.2 (287 funções) com o `exports.inc`, faltavam 11. A 1.41 implementa todos eles:

- **TLS estático em todas as threads** (`CreateToolhelp32Snapshot`, `Thread32First/Next`, `SuspendThread`, `GetThreadContext` e `ntdll!NtQueryInformationThread` via `GetProcAddress`). O 1.0.2 reserva slots de TLS carregando `azimuth-tls-N.dll` e depois copia o template de TLS do módulo do jogo para o bloco desse slot em cada thread viva (`engine+0x672600`). Para isso, lê o TEB de cada thread pela classe `ThreadBasicInformation` e usa o `ThreadLocalStoragePointer` em `+0x58`. Sem essas funções, o motor lança "No NtQueryInformationThread for a static TLS template". O host agora guarda o TEB de cada thread do motor (`GuestThreads.cpp`) e lista as threads em execução no snapshot. O `NtQueryInformationThread` devolve o TEB real. O `SuspendThread` só conta: o bloco escrito ainda não está em uso pela thread, então ela continua rodando. Por isso o `GetThreadContext` responde sem registradores. O `OpenThread` agora também abre a thread principal, que antes só tinha objeto depois de pedir o próprio.
- **E/S pelo pool de threads** (`CreateThreadpoolIo`, `StartThreadpoolIo`, `CancelThreadpoolIo`, `WaitForThreadpoolIoCallbacks`, `CloseThreadpoolIo`). O kernelx do motor repassa essas funções ao jogo. O arquivo é ligado a uma porta de conclusão própria do objeto, como no `CreateIoCompletionPort`, e uma thread entrega cada pacote ao callback. O `Files.cpp` não mudou.
- **Relatório de módulos** (`K32EnumProcessModules`, `K32GetMappedFileNameW`, `QueryDosDeviceW`, `GetSystemWindowsDirectoryW`). Esse relatório lista o motor e as DLLs mapeadas, com caminhos `\Device\HarddiskVolumeN\...` (N = letra do drive, A = 1), que o `QueryDosDeviceW` converte de volta para `C:` e `E:`.
- **`CreateProcessW`.** O motor tenta se reiniciar ao voltar para o launcher. O título não cria outro processo: a chamada falha com `ERROR_NOT_SUPPORTED`, e o motor registra o erro.

Nenhum caminho quente mudou (commit/liberação de memória, WriteWatch, esperas e travas). O `ResumeThread` só mudou para uma thread com contagem de `SuspendThread`. O 0.9.7 não importa nenhuma das funções novas.

contentVersion 01.000.141. Pacote: `dist/Azimuth-Launcher-1.41.zip`. SHA-256 do eboot.bin: `b71fee308ceddff01137ae0038d13969ce1af3440db2b51f6b74828eab4c8169`. Sem teste no Linux (pedido do usuário); console pendente.

Próximo teste: o motor 1.0.2 na pasta do jogo deve passar do início do launcher. Depois, conferir que o 0.9.7 continua igual (4K, MSAA 4x, 60 FPS, sem engasgos novos). Se o 1.0.2 parar, o `host.txt` diz em qual import ou método do DirectWrite ("DirectWrite ainda sem metodo").


## Título 1.42 — WIC do motor 1.0.2 (01/11/2026)

- O motor 1.0.2 cria a fábrica do WIC por `CoCreateInstance` com `CLSID_WICImagingFactory2` (pedindo `IID_IWICImagingFactory`) em vez de `WICCreateImagingFactory_Proxy`. `win_CoCreateInstance` (Misc.cpp) passa a entregar a fábrica do host (`createWicFactory`, Wic.cpp) para `CLSID_WICImagingFactory` e `CLSID_WICImagingFactory2`.
- `IWICImagingFactory::CreateDecoderFromFilename` (slot 3) implementado: resolve o caminho Windows com `fullPath`/`hostPath`, lê o arquivo inteiro e decodifica pelo mesmo caminho do stb_image. Arquivo ausente devolve `0x80070002`. O motor usa isso para `data\binzimuth-mark.png`.
- O motor 1.0.2 precisa do seu próprio `data\bin` (fontes Montserrat, `azimuth-tls-1/2.dll`, runtimes 140, `avcodec` e `azimuth.pipelines` novos). No console, o do 0.9.7 ficou em `data/bin-0.9.7`.
- Ainda não implementados (aparecem no log como "WIC: metodo ... sem implementacao"): `IWICStream::InitializeFromFilename` e os codificadores, usados pelo motor só para gravar imagens.


## Título 1.43 — espaçamento de linha do DirectWrite (01/11/2026)

- `SetLineSpacing` (slot 10 de `IDWriteTextFormat` e de `IDWriteTextLayout`): método 0 mantém a altura da fonte; 1 (uniforme) usa a altura e a baseline pedidas; 2 (proporcional) multiplica a altura e a baseline da fonte. `GetLineSpacing` (slot 18) devolve o que foi gravado.
- `SetIncrementalTabStop`/`GetIncrementalTabStop` (slots 8/16; padrão 4 em) e `SetTrimming`/`GetTrimming` (slots 9/17). O trimming é aceito, mas o texto não é cortado (fica uma linha no log).
- O motor 1.0.2 trata qualquer HRESULT de falha no texto do launcher como fatal ("text: ... HRESULT=..." e TerminateProcess).


## Título 1.44 — ComputeGlyphOrigins (01/11/2026)

- `IDWriteFactory4::ComputeGlyphOrigins`, slots 41 (run, origem, origens) e 42 (run, measuring mode, origem, transformação, origens). O motor chama o 42 em engine+0x5a9006 com a origem `D2D1_POINT_2F` por valor em r9.
- Origem de cada glifo = origem da baseline + soma dos avanços anteriores (ou dos avanços da fonte, se o run não trouxer avanços) + `advanceOffset`; y = baseline − `ascenderOffset`. Em bidi ímpar avança para a esquerda. A transformação, que só serve para arredondamento GDI, é ignorada.


## Título 1.45 — restante do DirectWrite (01/11/2026)

- A lista de métodos que o launcher 1.0.2 confere fica nas strings perto de engine .rdata 0x814a78 ("AddMappings" … "GetLineMetrics"), e todos agora têm implementação no host.
- Novos: `DetermineMinWidth` (layout 63, a maior soma de avanços entre espaços e quebras de linha), `SetLocaleName` (layout 41) e `CreateEllipsisTrimmingSign` (fábrica 20). Este último devolve um `IDWriteInlineObject` com métricas zeradas, sem quebra e com Draw vazio.


## Título 1.46 — cursor e TLS de DLL (01/11/2026)

- `GetSystemMetrics(13/14)` (SM_CXCURSOR/SM_CYCURSOR) = 32. O motor 1.0.2 usa só 11, 12, 13, 49 e 50.
- `runAsGuest` chama `forgetThreadBlock` em toda saída (retorno, ExitThread e parada), antes de o `ThreadBlock` ser destruído.
- Ao carregar uma DLL com TLS, o host só escreve na tabela de TLS de uma thread depois de confirmar que a página existe. Senão registra "<dll>: TLS da thread N nao recebido, a tabela de TLS dela em 0x... nao esta mapeada". A causa da página sumida (em 0x200b46000, na 1.45) ainda não está confirmada, e essa linha vai indicar qual thread era.


## Título 1.47 — shaders do launcher 1.0.2 (01/11/2026)

- `python host/tools/precompile_shaders.py <azimuth.exe> host/src/win32/CompiledShaders.inc [--dumps <pasta>]` (Windows, Python 64 bits): mantém as entradas existentes e acrescenta as do motor informado (0.9.7 ou 1.0.2, reconhecido pelo SHA-256), além dos pedidos gravados pelo console.
- O D3DCompile do host, quando não encontra o shader, grava `/user/data/azimuth-ps5/shaders/<chave>.hlsl` (o fonte como foi passado) e `<chave>.txt` (entry, target, flags1, flags2, nome), até 64 pedidos por execução.
- Para incluir shaders novos: baixar essa pasta pela porta 7070, rodar o script com `--dumps` e gerar o título de novo.


## Título 1.48 — shader provisório (01/11/2026)

- Pixel shader sem entrada pré-compilada: o host grava o pedido em shaders/ e devolve `placeholder ps_5_0` (preto), registrando "D3DCompile: shader provisorio (preto) no lugar de ...". Vertex e compute shaders sem entrada continuam falhando.
- Depois de uma execução: baixar /user/data/azimuth-ps5/shaders/ para console-reports/shaders-dumps, rodar `precompile_shaders.py ... --dumps console-reports/shaders-dumps` e gerar o título de novo.
- `guestConstantBlob` comporta 256 entradas (antes eram 16).

## Título 1.49 — VirtualProtect na imagem (01/11/2026)

- `VirtualProtect` em endereço da imagem do motor ou de uma DLL carregada agora funciona: o host aplica mprotect por bloco do kernel, somando os direitos das seções aos que foram pedidos, e guarda a proteção pedida de cada página para responder a proteção antiga.
- O motor 1.0.2 usa isso ao iniciar o jogo, para escrever o template de TLS do jogo num buffer da própria .rdata (engine+0x84cb20).
- Se o resultado deixar gravável e executável um bloco que não era, o host para com "VirtualProtect deixaria a imagem gravavel e executavel em ...".

## Título 1.50 — shaders de apresentação do jogo (01/11/2026)

- `plain`, `game` e `curved` (ps_5_0) do motor 1.0.2 foram compilados a partir dos despejos do console. São os shaders que levam a imagem do jogo para a tela.
- O código deles depende das configurações (HDR, sRGB). Ao mudar uma configuração dessas e a tela ficar preta, baixe shaders/ de novo e recompile com `--dumps`.

## Título 1.51 — controle pelo Windows.Gaming.Input (01/11/2026)

- O motor 1.0.2 lê o controle pelo `Windows.Gaming.Input.Gamepad`, não mais pelo XInput. O host implementa IGamepadStatics, a lista de controles e IGamepad (GetCurrentReading e put_Vibration) sobre o ScePad, em win32/Xinput.cpp e win32/gamepad_abi.c.
- No log do host, "Windows.Gaming.Input: controle do PS5 entregue ao motor como Gamepad" e "motor leu o controle pela primeira vez" confirmam que funcionou.
