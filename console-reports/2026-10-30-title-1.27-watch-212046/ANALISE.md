# Captura da versão 1.27 em 2160p

Captura em 30/10/2026, console 192.168.15.7, duração de 182,2 segundos. PID 411, versão confirmada pelo title.txt. O monitor foi encerrado ao terminar a captura. Nenhum binário ou configuração do console foi alterado.

## Configuração confirmada

- Resolução 2160; swapchain 3840 × 2160.
- frame_rate=30; frame_pacing=fixed; vsync=on.
- antialiasing=off; graphics=default.
- RADV carregado de /app0/radv/libvulkan.prx.

## Resultado

As 11 últimas amostras estabilizadas de host.txt tiveram média de 11,62 FPS, mínimo de 10,8 e máximo de 12,3. A média foi de 3,32 milhões de chamadas ao host por segundo. Foram excluídas as amostras iniciais de carregamento/menu e transição, que chegaram a 30 FPS. Essa seleção não constitui benchmark de cena controlada.

Na última amostra: 11,2 FPS, Present de 0,03 ms, SyncInterval=1. A thread 400 fez aproximadamente 1,46 milhão de chamadas/s e teve 24% do tempo contabilizado dentro do host. Durante o trecho estabilizado, esse indicador ficou em 24–25%, contra aproximadamente 34% registrado na captura anterior da 1.25. GetTickCount64 deixou de aparecer entre as entradas mais caras apresentadas, mas isso não significa custo zero.

A captura anterior da 1.25 em 1440p registrou aproximadamente 10,5 FPS de média, entre 9,6 e 12,2. As cenas e resoluções não foram controladas: não é possível atribuir a diferença de média à versão. O problema de desempenho persiste. O relato do usuário de desempenho idêntico em 720p e 1440p, somado ao resultado em 2160p, enfraquece a hipótese de gargalo dominado pelo número de pixels.

Não foram encontrados GPU_FAULT, App Crash, SIGSEGV, SIGILL ou SIGABRT no trecho de klog capturado. Isso não exclui stalls ou outros problemas não registrados.

## Limites da telemetria

Present curto não exclui espera assíncrona pela GPU. O percentual de tempo no host mede tempo decorrido, incluindo desescalonamento; não mede utilização real de CPU por thread. O tempo restante tampouco pode ser interpretado como CPU efetivamente executando código do jogo. Tempos agregados de espera podem superar 1000 ms/s porque somam várias threads.

## Pista prioritária: rastreamento de escrita

O ring da thread 400 contém GetWriteWatch. Em host/src/win32/Misc.cpp:92, a implementação devolve todas as páginas consultadas como modificadas, limitada pela capacidade do buffer. ResetWriteWatch é um stub.

No arquivo local de referência C:/Users/Gabriel/Documents/Codex/2026-10-22/car/outputs/azimuth-analysis/azimuth-decompiled.c, as duas chamadas a GetWriteWatch encontradas com rg (linhas 173725 e 173833) alimentam o controle de gerações por página. Um dos caminhos distingue explicitamente o retorno sem páginas modificadas. A resposta conservadora do host pode impedir esse caminho e provocar processamento repetido. Ainda não foi medido o volume de páginas nem demonstrado que esse custo explica os FPS.

Próxima medição recomendada: frequência, tamanho dos intervalos, quantidade de páginas retornadas, flags/reset e chamadores de GetWriteWatch, correlacionados com tempo real de CPU por thread. Não retornar artificialmente zero páginas: isso pode ocultar alterações reais e comprometer a correção do jogo.

## Câmera lenta

O próprio azimuth.ini documenta que frame_pacing=fixed avança a simulação por passos fixos e desacelera quando os frames atrasam. frame_pacing=real-time é uma opção documentada para manter a velocidade da simulação com frames atrasados, sujeitos a engasgos. Isso é um teste separado para o sintoma de câmera lenta, não uma correção do baixo FPS. A configuração não foi alterada.

## Evidências

- host-00.txt a host-06.txt e host.txt: snapshots e último estado.
- title-00.txt a title-06.txt e title.txt: identidade da execução.
- azimuth-00.log / azimuth-06.log e respectivos .ini: logs e configuração.
- klog.txt: captura TCP da porta 3232.
- capture.json: duração e eventos do coletor.

O log do motor contém sessões históricas; a análise desta execução usa PID 411. Os relógios do console e do coletor não devem ser tratados como sincronizados.
