# Captura da 1.32: engasgadas com tempos separados de I/O

30/10/2026. Console 192.168.15.7, versão 1.32, PID 421. Configuração inicial: 2160p, MSAA 4x, limite 60 FPS, frame pacing fixed, vsync on. Coletor ativo por 302.8 segundos. Nenhum binário ou configuração foi alterado.

O klog registra o encerramento do PID 421 pelo sistema no trecho final. Snapshots finais repetem o mesmo host.txt; não são novas amostras. A análise usa apenas 24 blocos de telemetria posteriores ao último timestamp do snapshot inicial. O primeiro desses intervalos pode sobrepor alguns segundos anteriores à captura. O monitor terminou.

## Resultado

- FPS médio das 24 janelas: 57.05, entre 52.7 e 60.2.
- 13,692 intervalos entre entradas de Present.
- 120 acima de 50 ms, incluindo 35 acima de 100 ms.
- Maior intervalo: 198.26 ms.
- Nenhuma FALHA FATAL no host; nenhum GPU_FAULT/App Crash/SIGSEGV/SIGILL/SIGABRT no trecho de klog capturado.

Os intervalos são entre chamadas de apresentação, não timestamps físicos de exibição. Histogramas são janelas aproximadas por concorrência entre atualização e relatório. Não há marcadores enviados pelo usuário para alinhar uma pausa percebida específica.

## O que o buffer maior mudou e o que não resolveu

A ponte de I/O rastreado registrou 6,640 operações e 1,616,941,056 bytes: média de 237.8 KiB por operação. Zero usos do fallback de 8 KiB. Isso confirma que o buffer maior está em uso. As engasgadas persistem; não cabe dizer que a 1.32 as resolveu.

| Etapa da ponte rastreada | Tempo decorrido acumulado | Maior operação |
|---|---:|---:|
| Leitura pread/read/fread | 1828.12 ms | 4.86 ms |
| Cópia para o destino | 4493.03 ms | 7.29 ms |

A cópia acumulou 2.46 vezes o tempo da leitura. Inclui faults, callbacks de reabertura de memória selada e desescalonamento. São tempos somados entre operações/threads, não tempo de CPU exclusivo. Não incluem a busca inicial de metadados nem a aquisição/alocação dos buffers. Cobrem somente a ponte para memória rastreada, não todas as leituras do jogo. Uma operação de 256 KiB também não equivale a um ReadFile completo.

Foram registradas 75 leituras lentas amostradas. A maior foi 23.833 ms para cerca de 4 MiB de GeoChunk3.minizip na thread 348. O logger limita as linhas por intervalo, portanto esse máximo não é um teto garantido de todas as leituras. Os nomes e eventos estão em leituras-lentas.csv.

## Memória rastreada continua como candidato

WriteWatch somou 943,622 chamadas mprotect e 263,391 reaplicações evitadas; fora de WriteWatch, houve apenas 133 chamadas. Essa contagem não mede o tempo gasto nas chamadas.

A janela do maior intervalo (timestamp 1793416127657) teve 55,8 FPS, intervalo de 198,26 ms, sete intervalos >50 ms (três >100), 46.768 mprotect do WriteWatch e quatro fora dele. A ponte nessa janela somou 116,27 ms na leitura e 252,31 ms na cópia, com máximos por operação de 1,86 e 2,56 ms.

Logo, não há uma operação isolada da ponte medida que corresponda à pausa de 198 ms. Ainda são possíveis sequências de operações no caminho crítico, contenção, trabalho de commit/reset/VEH fora da ponte, compilação de shaders ou espera por GPU. A associação entre streaming, cópia protegida e intervalos longos voltou a aparecer, mas não atribui cada pausa a mprotect.

## Próximo trabalho indicado pelos dados

Medir tempo e frequência separadamente em watchFault, reset/query e aplicação das proteções; preservar thread e timestamp dos eventos longos. Com isso, priorizar agrupamento seguro de alterações de proteção e preparação de cópias em lote, mantendo reset concorrente e a decisão do VEH para memória selada. O buffer maior remove a fragmentação de leitura, mas continua atendendo faults por bloco; esse custo não foi eliminado.

Comparação com a 1.31: a configuração inicial é igual, porém rota, estado dos caches, duração útil e carga de streaming não foram controlados. As contagens e os FPS não constituem benchmark A/B e não demonstram ganho ou regressão atribuível somente à versão. A nova separação de tempos é o principal avanço diagnóstico desta captura.

## Evidências

amostras-pid421.csv, leituras-lentas.csv, resumo.json, capture.json, snapshots host/title, klog.txt e cópias inicial/final do log e INI do motor. Campos e janelas agregados devem ser interpretados com as limitações acima.
