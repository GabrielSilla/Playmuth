# Engasgadas na 1.31 a 60 FPS — 30/10/2026

Captura de 303.4 s no console 192.168.15.7. PID 420 permaneceu o mesmo nos snapshots 00–20. Configuração inicial: 2160p, MSAA 4x, limite 60 FPS, frame_pacing=fixed, vsync=on. Polling HTTP de aproximadamente 15 s e klog TCP contínuo. Nenhum binário ou configuração foi alterado. A captura terminou; não há monitor ativo.

## Resultado medido

30 janelas novas de aproximadamente dez segundos, extraídas de host-20.txt após o último timestamp do snapshot inicial. O primeiro intervalo pode incluir segundos anteriores ao início do coletor. Histogramas são janelas aproximadas, pois as contagens são trocadas enquanto Present pode continuar em outra thread.

- Média das amostras de FPS: 57.73; mínimo 53.0; máximo 60.1.
- 17,318 intervalos entre chamadas de apresentação.
- 127 intervalos acima de 50 ms, dos quais 36 acima de 100 ms.
- Maior intervalo: 198.55 ms.
- Zero FALHA FATAL no host e zero GPU_FAULT/App Crash/SIGSEGV/SIGILL/SIGABRT no klog capturado.

São intervalos entre entradas de Present, não timestamps de exibição física. Os percentis disponíveis são por janela, arredondados para cima em milissegundos. Não se pode calcular um p99 global exato a partir deles.

## Padrão que se repetiu

| Janela (timestamp do host) | FPS | Maior intervalo | Acima de 50 / 100 ms | mprotect WriteWatch | mprotect sem WriteWatch |
|---|---:|---:|---:|---:|---:|
| 1793415017728, regular | 60,1 | 16,77 ms | 0 / 0 | 169 | 2 |
| 1793414967644, engasgadas frequentes | 53,0 | 151,06 ms | 12 / 3 | 87.466 | 0 |

Na segunda janela, ReadFile somou 167 ms/s e VirtualAlloc2 141 ms/s em tempo decorrido agregado; SRW exclusivo somou 235 ms/s. Esses valores somam threads e incluem desescalonamento/esperas, portanto não representam tempo de CPU nem duração de um frame individual.

Em toda a captura, WriteWatch registrou 1,050,984 chamadas mprotect e 270,748 reaplicações evitadas. Fora de WriteWatch foram 222 chamadas. A contagem não mede o custo de cada chamada, mas há janelas com engasgadas e nenhuma chamada fora de WriteWatch: a proteção comum agrupada não explica sozinha as pausas restantes. O caminho de memória rastreada merece prioridade. A correlação não é causalidade por evento: cada intervalo longo ainda não tem um timestamp próprio no log.

## Arquivos e thread envolvidos

Foram capturadas 103 linhas amostradas de ReadFile lento. A maior duração foi 53.622 ms lendo GeoChunk2.minizip na thread 348. Leituras de aproximadamente 4 MB de GeoChunk2.minizip e GeoChunk3.minizip apareceram repetidamente na faixa de 44–54 ms. Também há leituras de arquivos de carros. Os nomes e os tempos estão em leituras-lentas.csv.

O marcador “com cache” indica que o arquivo usa o mecanismo de cache; não comprova cache hit. Em Files.cpp, leituras de pelo menos 512 KiB usam o caminho direto. A duração de ReadFile inclui a implementação do host, inclusive cópia e falhas de proteção; não é uma medição isolada do SSD.

## Alvo concreto encontrado no código

WriteWatch.cpp::watchedPread usa um buffer intermediário de 8 KiB para destinos rastreados. Files.cpp::readCached e ReadFile repetem a chamada até completar o pedido. Uma leitura de 4 MiB para memória rastreada pode se dividir em cerca de 512 pread/cópias. As escritas no destino ainda participam do tracking por bloco de 16 KiB e da reabertura de regiões seladas pelo VEH.

Próximo ajuste candidato: reduzir essa fragmentação com um buffer maior fora da pilha pequena das fibras, cuidando de reentrada e do ciclo de vida. Medir separadamente o tempo do pread, da cópia e do atendimento de faults/proteções; se a proteção dominar, otimizar a preparação das escritas em lote preservando reset concorrente e a decisão do VEH para memória somente leitura. Não tornar indiscriminadamente páginas graváveis nem voltar ao retorno sempre sujo.

Ainda não se excluem custo de compilação de shaders, waits do driver/GPU ou outros pontos do motor. Esta captura identifica uma associação repetida entre streaming e memória rastreada, além de um custo concreto da ponte de I/O, sem demonstrar que ele explique todas as pausas. A configuração e o cenário diferem da captura da 1.30 (30 FPS e AA off); não atribuir diferenças entre as capturas apenas à versão.

## Evidências

- amostras-pid420.csv: FPS, intervalos e contadores por janela; campo vazio significa função fora do ranking, não custo zero.
- leituras-lentas.csv: eventos de I/O posteriores ao snapshot inicial, amostrados pelo limite do logger.
- resumo.json e capture.json: resultados agregados, duração e snapshots.
- host-00.txt a host-20.txt, title-*.txt, klog.txt e azimuth-00/20.log/.ini.
