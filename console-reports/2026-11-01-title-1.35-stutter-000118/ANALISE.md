# Consulta ao log da 1.35 — 01/11/2026

Console respondeu em 192.168.15.7, título 1.35/PID 662. O usuário confirmou que o jogo já estava fechado. Os 21 snapshots repetiram o arquivo host, sem novas métricas (inicial e final idênticos); os cinco minutos de consulta NÃO são cinco minutos de jogo nem um benchmark. Nenhum monitor permanece ativo.

O histórico preservado contém 25 linhas de métricas CRT, todas com zero operações/bytes de memcpy, memmove e memset rastreados. A otimização da 1.35 não foi usada por essas chamadas naquela sessão; não se deve insistir nesse caminho como causa sem novas evidências. O relatório final histórico de áudio registra 1.463 ocorrências do aviso XMA, 1.462 compactadas, mostrando o filtro ativo, sem provar ganho nas pausas.

A última janela histórica mostra 59,6 FPS, máximo de 48,10 ms entre Present e zero intervalos >50 ms nessa janela específica. Não representa toda a sessão e não contradiz as engasgadas relatadas pelo usuário. Não há captura da rota de jogo neste turno para comparar FPS global da 1.35/1.34.

Próxima alteração: reduzir a serialização da tabela global de handles e a segunda aquisição de waitLock na sinalização; manter a sincronização dos estados e acrescentar tempos de operações lentas de memória e timestamps de quadros longos. Dados agregados de esperas medem tempo decorrido, não CPU nem causalidade por frame.

Evidências: snapshots host/title, INI/log do motor inicial/final, klog.txt, capture.json e resumo.json.
