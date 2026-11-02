# Crash da 1.39 após ~10 minutos de jogo

Captura em 01/11/2026, console 192.168.15.7, PID 932, título "Azimuth Launcher title 1.39". Configuração do usuário: 4K, MSAA 4x, 60 FPS; jogo liso, sem engasgos, até a queda.

## A queda

- `Commit: memoria direta esgotada` (três vezes), e na mesma hora `SIGSEGV` na thread 380 do jogo em `0x100126f599` (VA do jogo `0x14125f599`): escrita no endereço 0.
- As últimas chamadas da thread 380 terminam em `VirtualAlloc GetLastError`: o `VirtualAlloc` devolveu NULL e o jogo escreveu pelo ponteiro nulo, sem conferir. No Xbox One essa alocação nunca falha.
- Antes da queda o jogo estava a 60 FPS, com o p99 dos intervalos em 24 ms e nenhum intervalo acima de 50 ms. Não houve engasgo nem outra falha.

## Memória direta ao longo da sessão

| Tempo | Blocos grandes do host | Maior trecho livre contíguo |
|---|---|---|
| 0 s | 128 MiB | 12096 MiB |
| 62 s | 3596 MiB | 3392 MiB |
| 193 s | 4779 MiB | 1088 MiB |
| 279 s | 5034 MiB | 512 MiB |
| 419 s | 5222 MiB | 192 MiB |
| 631 s (queda) | 5323 MiB | 8 → 3 MiB |

O alocador do host estabilizou em ~5,2 GB a partir dos ~4 minutos. O maior trecho contíguo livre continuou caindo até 3 MiB.

## Causa provável

O `back()` (commit de regiões só de endereço, em `Memory.cpp`) pedia **um** trecho físico contíguo do tamanho de toda a faixa sem memória. Com o jogo alocando e liberando sem parar (streaming do mundo aberto, 1,7 GB liberados a cada 10 s no fim), a memória direta se fragmenta. Um commit de alguns MiB então falha, ainda que haja memória livre no total. O log não tinha o total livre, então um vazamento real não está descartado, mas o host estável em ~5,2 GB aponta para fragmentação.

## Correção (1.40)

- O commit tenta primeiro o trecho contíguo inteiro, como antes. Só se isso falhar, ele preenche a faixa com os maiores pedaços livres, cada um com a sua identidade de alocação, como o `unback` espera.
- A linha de memória passa a mostrar a **memória direta em uso** (soma das faixas alocadas, via `sceKernelDirectMemoryQuery`). Isso separa fragmentação de esgotamento.

## Evidências

`host.txt`, `host-anterior.txt`, `title.txt` e `azimuth.log` nesta pasta.
