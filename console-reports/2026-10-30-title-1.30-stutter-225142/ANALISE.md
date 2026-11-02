# Engasgadas na 1.30 — captura de 30/10/2026

Captura de 301,5 s em 192.168.15.7: klog contínuo e snapshots HTTP a cada 15 s. Versão 1.30. O host produz as métricas de desempenho aproximadamente a cada 10 s; polling mais frequente não aumenta essa resolução. Nenhum binário ou configuração foi modificado pelo coletor.

## Execuções separadas

- PID 418, snapshots 00–17: sessão principal em 2160p, AA off, limite 30 FPS, fixed frame pacing e vsync on. O relatório host-17.txt é a última cópia dessa sessão.
- PID 419, snapshots 18–20: nova execução. O INI final mudou para 2160p, AA MSAA 4x e limite 60 FPS. O INI final está em UTF-16LE. Os números 17,7 e 60,1 dessa nova execução não entram nas estatísticas abaixo. O klog registra encerramento do PID 418 pelo sistema/menu e início posterior de outro processo; não se deve interpretar isso como crash sem evidência adicional.

## Resultado da sessão principal

25 amostras novas desde o primeiro snapshot: média 29,036 FPS, mínimo 26,6, máximo 30,1. O primeiro intervalo pode incluir segundos anteriores à conexão do coletor. Houve duas amostras consecutivas de 26,6 FPS, além de trechos a 30 FPS. O usuário não enviou marcadores individuais das engasgadas durante a captura, portanto não há alinhamento com um evento percebido específico.

Não há FALHA FATAL no relatório do PID 418, nem GPU_FAULT/App Crash/SIGSEGV/SIGILL/SIGABRT no klog capturado. As exceções vetorizadas registradas foram retomadas pelo motor. Não confundir esses eventos esperados de memória selada com crashes.

## Pista mais forte: carregamento e gestão de memória

Na primeira amostra a 26,6 FPS (timestamp do host 1793413845341), o tempo decorrido agregado das funções apresentou:

| Função | ms por segundo, somando threads | Chamadas/s | Tempo médio por chamada |
|---|---:|---:|---:|
| AcquireSRWLockExclusive | 424 | 132.744 | 3,19 us |
| ReadFile | 223 | 54 | 4,16 ms |
| VirtualAlloc2 | 206 | 131 | 1,57 ms |
| VirtualFree | 156 | 136 | 1,15 ms |
| AcquireSRWLockShared | 145 | 650 | 223,75 us |

Nesse mesmo intervalo, WriteWatch teve 1.211 consultas, 1.038.155.776 bytes em intervalos consultados e 37.608 páginas devolvidas. O motor retomou 56 exceções. Nos trechos a 30 FPS, caiu bastante a atividade de WriteWatch e as funções de leitura/alocação deixaram de aparecer entre as mais caras ou tiveram custo menor.

A associação favorece investigar streaming de cenário, alocação/liberação e contenção durante essas operações. Não prova causalidade: os tempos incluem esperas e desescalonamento, somam várias threads e não medem o custo de um frame individual. Função ausente do top 12 não significa custo zero. Present curto também não exclui espera assíncrona pela GPU.

No código, Memory.cpp::commit aplica mprotect por bloco de 16 KiB e a gestão da tabela de regiões usa uma trava compartilhada. Esses caminhos são candidatos à instrumentação; medir custo de map/zero/protect e contenção antes de escolher uma otimização. Não reduzir a correção do WriteWatch para ganhar FPS.

## Limites que impedem apontar um culpado exato

A telemetria atual mostra médias de 10 s e não traz máximo/p95/p99 dos intervalos entre frames. Uma travada isolada pode ficar escondida numa média próxima de 30 FPS. O log nominal de ReadFile registra apenas as primeiras 40 leituras lentas da execução (Files.cpp); os nomes de arquivos lentos disponíveis nesta captura pertencem ao carregamento anterior, não identificam os arquivos que causariam as engasgadas posteriores. Não atribuir o problema a disco ou shaders apenas por esses nomes.

Próxima medição útil: intervalos entre frames (máximo, percentis e contagem acima de 50/100 ms), eventos lentos de I/O durante toda a execução com limite por intervalo, thread envolvida e duração de commit/protect/free. Correlacionar esses dados no mesmo relógio e na mesma configuração.

## Evidências

- amostras-pid418.csv: as 25 amostras e métricas extraídas; campo vazio significa função ausente do ranking.
- host-00.txt a host-17.txt: sessão principal; host-18.txt a host-20.txt: nova sessão.
- title-*.txt, capture.json, klog.txt, azimuth-00/20.log e azimuth-00/20.ini.

A captura terminou. Não há monitor ativo após esse procedimento.
