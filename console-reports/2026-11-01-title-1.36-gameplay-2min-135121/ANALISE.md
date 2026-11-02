# Dois minutos de gameplay 1.36 — 01/11/2026

Título 1.36/PID 903 em todos os snapshots. Console 192.168.15.7. 121.5 segundos de coleta, 12 janelas novas de ~10 s; INI inicial/final idênticos: 2160p/MSAA 4x/high/60/fixed/vsync. Conexão klog ativa, sem erros de captura. Captura encerrada, nenhum monitor ativo.

Coleta solicitada pelo usuário enquanto dirigia. O aviso de overlay a 47 FPS foi recebido próximo ao fim; marker-47fps-host.txt preserva um download adicional, cerca de dez segundos após a coleta principal. Esse snapshot não foi misturado às métricas dos dois minutos. O marcador é aproximado e não sincroniza a hora exata do quadro observado. O host 1.36 não mede FPS em 100 ms, portanto não confirma nem refuta o valor instantâneo de 47 pelo seu mínimo médio de dez segundos.

## Medições e interpretação

Média das janelas: 59.59 FPS. Mínimo/máximo MÉDIOS por janela: 58.8/60.1; não são mínimo/máximo instantâneos do jogo. Em 7151 intervalos entre entradas de Present houve 12 >50 ms (incluindo 0 >100 ms), máximo 73.75 ms. Picos e irregularidade de frames continuam reais mesmo com média perto de 60.

Nenhum fatal do host ou padrão pesquisado de GPU fault/crash no klog capturado. Os 214 acessos enviados ao VEH foram retomados pelo motor.

Disputa da tabela de handles: 27.946 ms decorridos somados, máximo 0.059554 ms. Isso mede somente obtenção das partições da tabela, NÃO waitLock, espera dos objetos, tarefas do motor ou GPU. A tabela não aparece como uma espera individual compatível com as pausas grandes medidas.

WriteWatch: 197,019 mprotect, 176,189 faults de tracking; tempos inclusivos decorridos de 8390.65 ms e 7448.23 ms. Não somar: fault inclui mprotect e operações simultâneas em outras threads. Máximos individuais: mprotect 1.55 ms, fault 1.55 ms, consulta 1.36 ms.

Ponte de leitura: 5,695 operações/1,391,120,384 bytes, leitura 3333.52 ms (max 3.62), cópia 378.09 ms (max 1.12), sem buffer reduzido. ReadFile lentos amostrados: 34, máximo amostrado 16.979 ms (não é teto garantido de todas as leituras). CRT rastreada continua zerada. Avisos XMA: 8872 ocorrências, 8860 compactadas; não há prova de causa das pausas por esse aviso.

## Eventos com timestamp

12 quadros longos AMOSTRADOS (limite de uma linha/s por swap chain), todos na thread 392; durações dos Present anterior/atual até 0.02/0.03 ms. Esses tempos curtos não excluem espera assíncrona de GPU ou espera por tarefas fora de Present. O histograma acima conta todos os intervalos, por isso não precisa coincidir com as linhas amostradas.

532 eventos de memória >2 ms preservados: {'VirtualAlloc2': 91, 'VirtualFree': 439, 'VirtualProtect': 2}. Máximo 12.064435 ms. Foram descartados 568 por slots ocupados: a lista é incompleta e não permite concluir ausência de custo quando não há evento. O overflow reforça a necessidade de melhorar a capacidade/drenagem antes de atribuir cada pausa.

5 quadros amostrados têm sobreposição temporal com alguma operação de memória registrada; 0 pares na mesma thread. A coincidência em outras threads não prova que elas bloquearam a apresentação. Limite do início aproximado do quadro: intervalo arredondado a 0,01 ms (erro até aproximadamente 5 microssegundos). Os timestamps de memória vêm da execução, não do prefixo de log escrito depois pelo watchdog. Relógios de PC e console não foram presumidos sincronizados.

A pista residual mais concreta é a atividade de liberação/commit nas threads de streaming, especialmente VirtualFree, somada ao custo de tracking; falta demonstrar a dependência da thread 392. Não atribuir cada pausa a uma syscall, nem extrapolar as médias para mínimos instantâneos. Próximo diagnóstico: amostragem de FPS em 100 ms solicitada pelo usuário e espera/trabalho no caminho que alimenta a thread de apresentação, com mais capacidade para eventos de memória.

## Evidências

amostras.csv, resumo.json, quadros-lentos.csv, memoria-lenta.csv, sobreposicoes.csv, correlacao.json, leituras-lentas.csv, snapshots completos, INI/log inicial/final, capture.json e klog.txt. Parsers: analyze-stutter.py e correlate-stutter-events.py. Comparações com outras rotas/versões não constituem A/B controlado. Não houve alteração de INI nem instalação remota durante a coleta.
