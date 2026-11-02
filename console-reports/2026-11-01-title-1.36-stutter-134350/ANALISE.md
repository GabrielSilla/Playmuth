# Captura inicial 1.36: transição e gameplay — 01/11/2026

Título 1.36/PID 903 em todos os snapshots. Console 192.168.15.7. 302.1 segundos de coleta, 30 janelas novas de ~10 s; INI inicial/final idênticos: 2160p/MSAA 4x/high/60/fixed/vsync. Conexão klog ativa, sem erros de captura. Captura encerrada, nenhum monitor ativo.

A primeira parte inclui transição de carregamento/menu e direção ainda sem marcador exato. Após a confirmação do usuário, foi salvo marker-driving.json; o recorte conservador tem 14 janelas, média de 59,29 FPS, mínimo médio de 57,3, dez intervalos >50 ms e máximo 92,25 ms. Não rotular todas as pausas anteriores ao marcador como loading: o usuário pode já estar dirigindo. O mínimo médio não é FPS instantâneo. Ver amostras-dirigindo.csv e resumo-dirigindo.json.

## Medições e interpretação

Média das janelas: 59.24 FPS. Mínimo/máximo MÉDIOS por janela: 55.6/60.1; não são mínimo/máximo instantâneos do jogo. Em 17772 intervalos entre entradas de Present houve 33 >50 ms (incluindo 4 >100 ms), máximo 194.67 ms. Picos e irregularidade de frames continuam reais mesmo com média perto de 60.

Nenhum fatal do host ou padrão pesquisado de GPU fault/crash no klog capturado. Os 999 acessos enviados ao VEH foram retomados pelo motor.

Disputa da tabela de handles: 50.389 ms decorridos somados, máximo 0.086610 ms. Isso mede somente obtenção das partições da tabela, NÃO waitLock, espera dos objetos, tarefas do motor ou GPU. A tabela não aparece como uma espera individual compatível com as pausas grandes medidas.

WriteWatch: 435,367 mprotect, 383,865 faults de tracking; tempos inclusivos decorridos de 18990.21 ms e 16624.85 ms. Não somar: fault inclui mprotect e operações simultâneas em outras threads. Máximos individuais: mprotect 2.03 ms, fault 1.65 ms, consulta 2.21 ms.

Ponte de leitura: 12,331 operações/2,983,129,088 bytes, leitura 7058.35 ms (max 5.33), cópia 690.10 ms (max 0.54), sem buffer reduzido. ReadFile lentos amostrados: 61, máximo amostrado 24.834 ms (não é teto garantido de todas as leituras). CRT rastreada continua zerada. Avisos XMA: 22849 ocorrências, 22819 compactadas; não há prova de causa das pausas por esse aviso.

## Eventos com timestamp

25 quadros longos AMOSTRADOS (limite de uma linha/s por swap chain), todos na thread 392; durações dos Present anterior/atual até 0.05/0.05 ms. Esses tempos curtos não excluem espera assíncrona de GPU ou espera por tarefas fora de Present. O histograma acima conta todos os intervalos, por isso não precisa coincidir com as linhas amostradas.

944 eventos de memória >2 ms preservados: {'VirtualAlloc2': 160, 'VirtualFree': 772, 'VirtualProtect': 12}. Máximo 13.225198 ms. Foram descartados 605 por slots ocupados: a lista é incompleta e não permite concluir ausência de custo quando não há evento. O overflow reforça a necessidade de melhorar a capacidade/drenagem antes de atribuir cada pausa.

15 quadros amostrados têm sobreposição temporal com alguma operação de memória registrada; 0 pares na mesma thread. A coincidência em outras threads não prova que elas bloquearam a apresentação. Limite do início aproximado do quadro: intervalo arredondado a 0,01 ms (erro até aproximadamente 5 microssegundos). Os timestamps de memória vêm da execução, não do prefixo de log escrito depois pelo watchdog. Relógios de PC e console não foram presumidos sincronizados.

A pista residual mais concreta é a atividade de liberação/commit nas threads de streaming, especialmente VirtualFree, somada ao custo de tracking; falta demonstrar a dependência da thread 392. Não atribuir cada pausa a uma syscall, nem extrapolar as médias para mínimos instantâneos. Próximo diagnóstico: amostragem de FPS em 100 ms solicitada pelo usuário e espera/trabalho no caminho que alimenta a thread de apresentação, com mais capacidade para eventos de memória.

## Evidências

amostras.csv, resumo.json, quadros-lentos.csv, memoria-lenta.csv, sobreposicoes.csv, correlacao.json, leituras-lentas.csv, snapshots completos, INI/log inicial/final, capture.json e klog.txt. Parsers: analyze-stutter.py e correlate-stutter-events.py. Comparações com outras rotas/versões não constituem A/B controlado. Não houve alteração de INI nem instalação remota durante a coleta.
