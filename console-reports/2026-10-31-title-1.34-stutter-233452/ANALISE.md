# Captura 1.34 — 31/10/2026

Sessão PID 661, console 192.168.15.7. Coleta de 302.3 segundos, 21 snapshots HTTP e klog TCP; 30 janelas novas de aproximadamente dez segundos. Todas as identificações do título permaneceram em 1.34/PID 661. INI inicial/final idênticos: 2160p, MSAA 4x, graphics=high, frame_rate=60, frame_pacing=fixed, vsync=on. Usuário relatou melhora acentuada e engasgos leves restantes.

## Resultado

- Média das janelas: 59.15 FPS; mínimo 55.4, máximo 60.1.
- 17745 intervalos entre entradas de Present; 36 acima de 50 ms, incluindo 3 acima de 100 ms; máximo 138.39 ms.
- p99 por janela entre 17 e 40 ms. Não é um p99 global.
- Nenhum fatal do host nem ocorrência dos padrões pesquisados de GPU fault/crash no klog capturado. Todos os 665 acessos enviados ao VEH foram retomados pelo motor.

## Comparação observacional

| Captura | Janelas | Média FPS | >50 ms | >100 ms | Máximo ms |
|---|---:|---:|---:|---:|---:|
| 1.31 | 30 | 57,73 | 127 | 36 | 198,55 |
| 1.32 | 24 | 57,05 | 120 | 35 | 198,26 |
| 1.34 | 30 | 59,15 | 36 | 3 | 138,39 |

A sessão atual tem menos pausas que as capturas anteriores e concorda com a percepção do usuário. Rotas, cache e carga de streaming não foram controlados: não é benchmark A/B e não isola o ganho da 1.34 em relação à 1.33, que não teve captura. Inclui todas as janelas novas, inclusive trechos sem streaming; não se presume que o usuário dirigiu continuamente.

## Memória e streaming

WriteWatch: 384,166 chamadas mprotect, 309,478 reaplicações evitadas, 42,300 faixas agrupadas cobrindo 859,298 blocos. A etapa mprotect somou 17038.61 ms decorridos, máximo individual 1.71 ms. O tracking de escrita teve 341,165 atendimentos, 15048.15 ms decorridos, máximo 1.71 ms. Consultas somaram 651.02 ms, máximo 1.67 ms. Reset explícito não ocorreu; resets anexados à consulta são medidos na etapa consulta.

A ponte rastreada movimentou 1,857,404,928 bytes em 7,678 operações, zero fallback reduzido. Leitura: 3811.64 ms agregados, máximo por operação 6.83 ms. Cópia (inclui preparo): 532.02 ms, máximo 0.51 ms. Na captura 1.32 eram 4.493,03 ms para 1.616.941.056 bytes; agora 532,02 ms para 1.857.404.928 bytes. A redução observada é grande, mas não atribui sozinha cada pausa nem separa as mudanças 1.33/1.34.

Houve 72 ReadFile lentos amostrados; maior duração 35.491 ms. A amostragem limita eventos por janela e não estabelece um máximo absoluto. Caminhos/threads estão em leituras-lentas.csv.

A janela da maior pausa, timestamp 1793502896318, teve 58,5 FPS, máximo 138,39 ms, 10.239 mprotect e 9.108 faults; seus máximos individuais foram 1,55 ms para proteção/fault, 1,91 ms para leitura da ponte e 0,29 ms para cópia. Nenhuma operação individual medida explica 138 ms. Houve também 1 intervalos >50 ms em janelas sem I/O pela ponte rastreada: isso não exclui outras leituras, trabalho pendente ou memória.

Tempos são decorridos e somados entre threads, incluem desescalonamento e se sobrepõem: fault contém mprotect e cópia contém preparo/faults/VEH. Não somar como CPU exclusiva. Restam custos acumulados de tracking/memória, trabalho do motor, sincronização e possíveis esperas gráficas a discriminar por evento. Present curto não exclui espera assíncrona de GPU.

## Áudio e próximo diagnóstico

Foram 30,053 avisos `Could not update timestamps for skipped samples.` em stderr, cerca de 99.4/s; representam 91.3% das linhas novas. Msvcrt.cpp::toLog serializa stderr com mutex e enfileira cada linha no logger. Isso é uma carga concreta e uma pista secundária para medir/coalescer mensagens repetidas, mas não prova defeito audível nem causalidade das pausas. O motor original permanece inalterado.

Próximo diagnóstico recomendado: registrar eventos longos com timestamp/thread no caminho de apresentação, memória/commit/free/VEH e sincronização, para localizar trabalho no caminho crítico em vez de concluir por agregados de dez segundos. Se coalescer avisos repetidos, preservar primeira ocorrência e contadores/resumos e medir o efeito. Continuar priorizando a frequência de faults de escrita como custo residual medido; não tornar toda memória permanentemente suja, pois isso reintroduz o problema original.

## Método e evidências

Baseline descartado até timestamp 1793502645907; primeiro/último registros novos 1793502655922/1793502946395. A primeira janela nova pode incluir alguns segundos anteriores ao início da coleta. Downloads preservam snapshots completos; contadores analisados uma única vez no último snapshot, sem somar duplicatas. Intervalos são entre entradas de Present, não exibição física, e podem incluir loading/pausa. Não foram fornecidos marcadores exatos de engasgos pelo usuário.

Arquivos: amostras.csv, resumo.json, leituras-lentas.csv, eventos-leitura.txt, capture.json, host/title snapshots, klog.txt e INI/log inicial/final. Captura encerrada; nenhum monitor permanece ativo. Nenhum pacote novo ou configuração remota foi alterado nesta coleta.
