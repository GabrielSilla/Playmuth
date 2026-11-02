# Teste da 1.29: falha durante loading

Versão confirmada: 1.29, PID 415, console 192.168.15.7. O usuário relatou loading prolongado seguido de falha. Não houve confirmação de entrada no mundo. Os 30 FPS registrados neste teste são de carregamento, não demonstram ganho no mundo.

O log do motor nesta sessão confirma 1440p, AA 4x, frame pacing fixed, limite 30 e vsync ligado. Não é a configuração 2160p/AA off usada na captura da 1.27. Não atribuir diferenças de FPS exclusivamente ao binário.

## Primeiro erro, prioritário

host-failure.txt:5313: SIGSEGV na thread 348, RIP 0x180044549, acesso a 0x104ac70000, RSP 0x7ec9ef638. RDI=0x60000. Logo depois aparece "nova falha dentro do tratador de falhas; thread parada". O handler tenta ler 16 palavras de RDI para detalhar o erro, bastando o ponteiro estar acima de 0x10000. Esse critério não garante memória válida. A sequência é compatível com uma segunda falha nessa leitura; ainda não há RIP separado da falha interna para comprová-lo. O primeiro erro perdeu parte do diagnóstico e a thread ficou estacionada, enquanto outras threads continuaram apresentando loading.

## Parada posterior

host-failure.txt:6814: outro SIGSEGV, thread 380, RIP 0x10013c39a0, escrita a 0x10362c1000. Diagnóstico final: si_code=2, trap=0xc, err=0x7. O host encerrou a execução do motor e o título ficou aguardando fechamento pelo menu. Fim do título com código 0 é o encerramento do wrapper após a parada do motor; não significa teste aprovado.

## Write watch e limites

O rastreamento novo chegou a devolver subconjuntos das páginas: uma amostra teve 12 consultas, 19.660.800 bytes em intervalos e 1.164 páginas devolvidas; outra teve 13 consultas, 11.075.584 bytes e 644 páginas. Isso demonstra que o retorno deixou de ser sempre sujo nesses intervalos. Não prova correção integral nem ganho de FPS. As amostras posteriores chegaram a zero consultas enquanto o loading continuava.

É uma regressão de estabilidade em relação às execuções anteriores que chegaram ao mundo. A relação exata entre o primeiro acesso inválido e o novo rastreamento ainda precisa ser determinada: falta registrar o estado do bloco (rastreado, proteções solicitadas/aplicadas, committed/writable/dirty) no ponto da primeira falha. Não tornar a página gravável incondicionalmente, pois isso poderia esconder uma falha real.

Prioridade seguinte: impedir leituras inseguras no diagnóstico de falhas e preservar trap/err e metadados de write watch antes de tentar detalhamento. Depois identificar a instrução em 0x180044549 no módulo correspondente e confrontar seu destino com a proteção solicitada pelo motor. Não usar somente o último SIGSEGV como causa inicial.

Evidências: host-failure.txt, title-failure.txt, azimuth-failure.log, snapshots periódicos e klog.txt nesta pasta.
