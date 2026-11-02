# Gameplay 1.37 — um minuto com amostras de 100 ms (01/11/2026)

Título 1.37/PID 904, console 192.168.15.7, gameplay informado pelo usuário. Coleta HTTP de ~60 s, encerrada em 62.39 s após finalização dos downloads/klog. Cinco snapshots, 600 amostras novas de aproximadamente 100 ms (60 s de janelas), seis janelas novas do diagnóstico antigo de ~10 s. Mesmo PID durante toda a coleta; INI inicial/final idênticos: 2160p/MSAA 4x/high/60/fixed/vsync. Sem alteração remota de configuração. Monitor encerrado.

## Quedas curtas confirmadas

- Média ponderada pelo tempo das janelas curtas: 59.05 FPS.
- Pior janela: **10.00 FPS**, um quadro em 100.001 ms, timestamp de log 1793554677967, fim monotônico 270157251665664 ns.
- Houve 19 janelas com no máximo quatro quadros (cerca de 40 FPS ou menos), incluindo janelas de 10, 20 e 30 FPS. Janelas consecutivas podem pertencer à mesma travada: não são necessariamente 19 eventos independentes.
- Maior intervalo entre entradas de Present: **85.169 ms**. Maior idade do último quadro na hora de amostrar: 55.928 ms. Sem janela de zero quadros nesta coleta.
- Amostrador regular: janelas de 99.933 a 100.076 ms. O mínimo de FPS não veio de uma janela longa provocada por desescalonamento do amostrador.

**10 FPS aqui significa um quadro naquele décimo de segundo, não que o jogo ficou um segundo ou o minuto inteiro a 10 FPS.** Contagem em 100 ms é quantizada (aproximadamente 10 FPS por quadro), por isso ler junto do número de quadros, duração da janela e intervalos. Não é taxa física de scanout. Contagens são entradas de Present sem TEST, podendo incluir chamadas que depois falhem.

O diagnóstico antigo registrou média de 59.18 e mínimo médio de 57.9 FPS por janela longa. Esses mínimos longos escondem as quedas curtas. O histograma longo contou 10 intervalos >50 ms e 0 >100, máximo 85.17 ms. A diferença entre a média curta e a longa vem das fronteiras temporais, duração real e contadores diferentes (o antigo usa aproximadamente dez segundos). Não extrapolar nenhuma média para um mínimo instantâneo.

## Causas: o que os dados permitem afirmar

Tabela de handles: 18.735 ms somados de disputa e máximo individual 0.10758 ms. Isso não cobre waitLock ou espera nas tarefas/objetos. Não há espera individual da tabela com escala semelhante ao quadro de 85 ms.

WriteWatch: 134,084 mprotect, 119,676 faults; tempos inclusivos decorridos 5868.86/5207.39 ms. Custos continuam relevantes, mas somados entre threads e sobrepostos, não CPU exclusiva nem duração de uma pausa. A ponte de leitura teve máximo por operação 3.63 ms, cópia 0.38 ms. CRT rastreada segue zerada.

Eventos de memória >2 ms preservados: 352, distribuídos como {'VirtualFree': 292, 'VirtualAlloc2': 51, 'VirtualProtect': 9}, máximo 11.261541 ms. O buffer descartou 277 eventos: a lista é incompleta. Dos 8 quadros longos amostrados, 4 têm alguma sobreposição temporal com memória, todos em outras threads (0 pares na mesma thread). Portanto liberar/reciclar memória nas threads de streaming é uma pista concreta, mas não está demonstrado que bloqueou o produtor do frame específico. Falta observar o caminho de espera/trabalho que alimenta a thread 392 e separar espera pela trava de memória da liberação no kernel.

Present anterior/atual ficaram até 0.06/0.06 ms nos quadros longos amostrados. Tempos curtos dentro da chamada não excluem esperas assíncronas por GPU ou trabalho/esperas fora de Present. Nenhum fatal do host ou padrão de GPU fault/crash pesquisado apareceu. Os 238 acessos despachados foram retomados pelo motor.

## Evidências

`fps-100ms.csv` e `resumo-100ms.json` são as medidas novas pedidas. `janelas-ate-4-quadros.csv` destaca as quedas fortes. `amostras.csv`/`resumo.json` são os agregados antigos; `quadros-lentos.csv`, `memoria-lenta.csv`, `sobreposicoes.csv` e `correlacao.json` preservam os eventos. INI/log inicial/final, snapshots completos, capture.json e klog.txt também salvos.

Baseline de 100 ms descartado até fim monotônico 270103351663153 ns; só registros posteriores foram contados. Os agregados antigos podem conter segundos anteriores ao início da captura, devido às suas janelas mais longas. Não comparar rotas/versões como benchmark A/B sem controle.
