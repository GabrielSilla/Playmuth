# Notas sobre o runtime do RADV

O teste do título 0.4 está em
`console-reports/2026-10-27-title-0.4-gpu-entry/`. A falha é uma leitura de
`0x70` no sistema, não a busca de instrução no RADV observada no payload.
O título 0.5 testa primeiro o carregador normal e coleta o módulo do RIP.
Ainda não há diagnóstico definitivo da causa.

Atualização: o título 0.5 passou no console usando
`sceKernelLoadStartModule`, com resultado da entrada 0, resolução dos exports,
negociação ICD versão 5 e descarregamento com retorno 0. Isso valida o caminho
normal de carregamento nesse contexto; não identifica a função exata que
falhava no caminho bruto. Evidência em
`console-reports/2026-10-27-title-0.5-gpu-module/`. O próximo teste é criar
a instância e enumerar os dispositivos, antes do dispositivo lógico.

Atualização seguinte: a instância e a enumeração também passaram no PS5,
com um dispositivo "PlayStation 5 GPU (RADV NAVI21)", Vulkan 1.4.354.
Relatórios em `console-reports/2026-10-27-title-0.5-gpu-instance/`.
Ainda falta criar o dispositivo lógico e verificar execução de comandos.

No teste completo seguinte, `vkCreateDevice` retornou 0 e o buffer foi
criado/mapeado, mas o título fechou com erro depois da mensagem anterior
a `vkQueueSubmit`. O 0.5 não separava no log essa chamada da espera pela
fence, portanto não se sabe qual delas deixou de retornar. Não houve
comparação do buffer. Evidência em
`console-reports/2026-10-27-title-0.5-gpu-submit/`. O 0.6 adiciona as etapas
separadas, retornos, SIGSYS e heartbeat sem o mutex do logger. A execução de
comandos no PS5 continua sem validação.

O teste do 0.6 separou as chamadas: `vkQueueSubmit` retornou 0 em 0,05 ms,
e o relatório terminou após a entrada em `vkWaitForFences` (timeout de 2 s).
Não houve retorno, heartbeat nem sinal capturado. Evidência em
`console-reports/2026-10-28-title-0.6-gpu-submit/`. Isso localiza a última
chamada, sem estabelecer a causa do fechamento ou a conclusão do comando.
O título 0.7 troca a espera por consultas a `vkGetFenceStatus` com intervalo
de 1 ms e prazo de 2 s aplicado entre chamadas que retornam. Em caso de
pendência/erro, usa `_exit(5)` sem liberar recursos pendentes ou entrar em
`vkDeviceWaitIdle`. Também confirma o início da thread do watchdog.

O 0.7 confirmou início da thread do watchdog, submissão com retorno 0 e
primeira consulta a `vkGetFenceStatus` com retorno 1 (`VK_NOT_READY`).
O registro termina antes de outra consulta. Não há confirmação da conclusão
nem da causa: a próxima operação seria uma pausa de 1 ms. Evidência em
`console-reports/2026-10-28-title-0.7-gpu-submit/`. O 0.8 confere essa pausa
antes de carregar RADV e testa uma submissão sem command buffers antes do
preenchimento. Registra também antes/depois da primeira pausa na consulta.

O 0.8 passou na pausa inicial e na submissão sem command buffers (fence
sinalizada). O envio com preenchimento retornou 0, a primeira consulta deu
`VK_NOT_READY` e a pausa seguinte retornou 0. O relatório terminou antes
de outro resultado. Evidência em `console-reports/2026-10-28-title-0.8-gpu-submit/`.
O 0.9 acrescenta um command buffer vazio e corrige a dependência de memória
para leitura do preenchimento na CPU, com `vkCmdPipelineBarrier` de
`TRANSFER_WRITE` para `HOST_READ`. Isso segue os
[exemplos da Khronos](https://docs.vulkan.org/guide/latest/synchronization_examples.html);
a ausência dessa barreira não foi estabelecida como causa do fechamento.

O 0.9 concluiu o envio sem command buffers e o command buffer vazio. O
preenchimento com a barreira continuou sem conclusão registrada; a memória
era tipo 2, heap 0, flags 0x6, em `0x4000000000`. Relatórios em
`console-reports/2026-10-28-title-0.9-gpu-submit/`. O 0.10 prioriza um tipo
compatível com flags 0x7 (local da GPU, visível e coerente para a CPU), que
nesse dispositivo é o tipo 3, heap 1. Conserva fallback para flags 0x6.
É uma hipótese de teste; não se determinou a causa do fechamento.

Uma alternativa de integração foi encontrada no projeto local PS5X360,
em `.deps/references/PS5_Vulkan/tools/radv-link.sh`: RADV ligado
estaticamente ao eboot com a camada `libps5platform.a`. O projeto PS5_Vulkan
documenta limitações do carregamento dinâmico em títulos em seu README e em
`docs/M5_REFERENCE.md`. Isso é evidência externa ao Azimuth_PS5; essa rota
ainda não foi implementada nem testada neste projeto.

Referência local consultada: `C:/Projects/PS5X360/.deps/references/PS5_Vulkan`.
A receita de build do PS5X360 fixa PS5_Vulkan em
`3f3ee69607013b345d2baa6d6a37c86745649a08`, PS5_Mesa em
`0b2d6d1a61d9bbf89cf8beb88a696144f67c61f8` e PS5_PayloadSDK em
`95c08f27386fc698f6bbe21dde3030140a41d10b`. O arquivo de proveniência do
RADV local informa SHA-256
`968c81157acf08093e3c229a247414ed58b5c69f0a57f8f1d1110f375f63a92f`.
Nenhum desses projetos foi modificado durante a investigação.
