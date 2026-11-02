# Retorno da 1.28 — 30/10/2026

Baixados title.txt e host.txt de 192.168.15.7. Versão 1.28, PID 414. Uma segunda leitura devolveu o mesmo fim de relatório; não há evidência de novas amostras entre os downloads.

A última amostra registra 11,7 FPS e 3.435.184 chamadas ao host/s. Há somente uma amostra final a esse patamar; as anteriores incluem carregamento/transição (30,1, 27,8 e 20,6 FPS). Não calcular média de mundo estabilizado com essas fases misturadas. O relato do usuário confirma persistência do problema.

No último intervalo de aproximadamente dez segundos, GetWriteWatch recebeu 123.931 consultas, somando 59.584.217.088 bytes em intervalos consultados e devolvendo 14.546.928 endereços de páginas; todas as consultas solicitaram reset. São intervalos potencialmente repetidos, não 59,6 GB de memória distinta, bytes copiados ou tráfego efetivo medido. O host preenche a lista de endereços e o motor a percorre.

Comparação de código: host/src/win32/Misc.cpp devolve todas as páginas. No decompilado original, FUN_1400db650 (GetWriteWatch na linha 173725) marca gerações das páginas devolvidas. A função com a chamada na linha 173833 segue para LAB_1400db936 quando recebe páginas; só quando não recebe nenhuma tenta selar os intervalos com PAGE_READONLY via FUN_1400db770. O caminho de repetição dobra um contador até 0x400. A resposta sempre suja impede o caminho limpo. Isso sustenta uma hipótese de manutenção/invalidação excessiva; ainda não prova que ela domine o tempo de frame.

Próxima correção candidata: rastreamento real de escrita em MEM_WRITE_WATCH, integrado a VirtualAlloc/VirtualProtect/VirtualFree e ao tratador de falhas. A granularidade física de proteção do PS5 é 16 KiB; é possível marcar conservadoramente as quatro páginas Windows de 4 KiB do bloco escrito. Exige preservar proteções solicitadas pelo motor, separar falha legítima de acesso de falha de rastreamento, tratar reset concorrente sem perder escritas e remover o rastreamento em decommit/free. Não basta retornar zero, comparar conteúdo com snapshot ou alterar apenas GetWriteWatch: escrita seguida de restauração do valor também deve ser registrada. O handler não pode depender de mutex que a thread interrompida possa estar segurando.

Nenhum novo binário foi gerado nesta análise. As otimizações da 1.28 não resolveram o problema conforme o teste do usuário. O foco passa da redução genérica de custo de espera para a semântica do controle de escrita; ainda é necessária medição de CPU por thread para estabelecer o caminho crítico.
