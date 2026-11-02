import csv, json, pathlib, re, sys
folder=pathlib.Path(sys.argv[1])
initial=(folder/'host-00.txt').read_text(encoding='utf-8',errors='replace')
baseline=max(map(int,re.findall(r'^(\d+) Desempenho:',initial,re.M)))
host=(folder/'host.txt').read_text(encoding='utf-8',errors='replace')
rows=[];current=None
for line in host.splitlines():
    m=re.match(r'(\d+) Desempenho: ([\d.]+) quadros/s',line)
    if m:
        current={'timestamp':int(m[1]),'fps':float(m[2])}
        if current['timestamp']>baseline: rows.append(current)
        continue
    if current is None or current['timestamp']<=baseline: continue
    patterns=[
      (r'Intervalos entre Present: (\d+) amostras, max ([\d.]+) ms, p95 ([\d>]+) ms, p99 ([\d>]+) ms, >50 ms (\d+), >100 ms (\d+)', ['intervals','max_ms','p95_ms','p99_ms','over50','over100']),
      (r'Protecao WriteWatch: (\d+) chamadas mprotect, (\d+) reaplicacoes', ['watch_mprotect','watch_skipped']),
      (r'Protecao sem WriteWatch: \d+ blocos, (\d+) chamadas', ['nonwatch_mprotect']),
      (r'I/O rastreado: (\d+) operacoes \((\d+) fread\), (\d+) bytes, leitura ([\d.]+) ms \(max ([\d.]+)\), copia ([\d.]+) ms \(max ([\d.]+)\), buffer reduzido (\d+)', ['io_operations','io_stdio','io_bytes','io_read_ms','io_read_max_ms','io_copy_ms','io_copy_max_ms','io_fallbacks']),
      (r'WriteWatch: (\d+) consultas, (\d+) bytes consultados, (\d+) paginas devolvidas, (\d+) resets', ['watch_queries','watch_bytes','watch_pages','watch_resets']),
      (r'Excecoes vetorizadas: (\d+) acessos invalidos, (\d+) retomados', ['veh_accesses','veh_resumed'])]
    patterns.extend([
      (r'Escritas CRT rastreadas: memcpy (\d+) ops, (\d+) bytes \| memmove (\d+) ops, (\d+) bytes \| memset (\d+) ops, (\d+) bytes', ['crt_copy_calls','crt_copy_bytes','crt_move_calls','crt_move_bytes','crt_set_calls','crt_set_bytes']),
      (r'Avisos de audio: (\d+) repetidos XMA, (\d+) compactados', ['audio_occurrences','audio_suppressed'])])
    patterns.append((r'Disputa de handles: (\d+) esperas, ([\d.]+) ms, max ([\d.]+) ms', ['handle_contentions','handle_wait_ms','handle_wait_max_ms']))
    patterns.append((r'Liberacao direta: (\d+) unmap, (\d+) release, (\d+) reservas, (\d+) bytes liberados, (\d+) releases agrupados', ['direct_unmap_calls','direct_release_calls','direct_reserve_calls','direct_released_bytes','direct_grouped_releases']))
    for pattern,keys in patterns:
        match=re.search(pattern,line)
        if match:
            for key,value in zip(keys,match.groups()):
                current[key]=value if value.startswith('>') else float(value) if '.' in value else int(value)
    if 'Etapas WriteWatch:' in line:
        for stage,calls,total,peak in re.findall(r'(mprotect|fault|consulta|reset|preparo) (\d+) ops, ([\d.]+) ms, max ([\d.]+) ms',line):
            current[stage+'_calls']=int(calls);current[stage+'_ms']=float(total);current[stage+'_max_ms']=float(peak)
        m=re.search(r'lotes (\d+), blocos agrupados (\d+)',line)
        if m: current['batches']=int(m[1]);current['batch_blocks']=int(m[2])
rows=[r for r in rows if 'preparo_max_ms' in r and 'intervals' in r]
assert rows, 'No complete new windows'
keys=list(dict.fromkeys(k for r in rows for k in r))
with (folder/'amostras.csv').open('w',newline='',encoding='utf-8') as f:
    w=csv.DictWriter(f,keys);w.writeheader();w.writerows(rows)
newlines=[]
for line in host.splitlines():
    m=re.match(r'(\d+) ',line)
    if m and int(m[1])>baseline: newlines.append(line)
new='\n'.join(newlines)
summary={'samples':len(rows),'baselineTimestamp':baseline,'firstTimestamp':rows[0]['timestamp'],'lastTimestamp':rows[-1]['timestamp'], 'averageFps':sum(r['fps'] for r in rows)/len(rows),'minFps':min(r['fps'] for r in rows),'maxFps':max(r['fps'] for r in rows)}
for key in keys:
    if key in ('timestamp','fps','p95_ms','p99_ms'):continue
    summary[key]=max(r.get(key,0) for r in rows) if key.endswith('_max_ms') or key=='max_ms' else sum(r.get(key,0) for r in rows)
summary['xmaTimestampWarnings']=new.count('Could not update timestamps for skipped samples.')
summary['newHostLines']=len(newlines)
slow=[]
for line in newlines:
    m=re.match(r'(\d+) ReadFile lento: (\d+) bytes em (\d+), (\d+) us, (.*); thread (\d+)',line)
    if m: slow.append(dict(zip(['timestamp','bytes','offset','microseconds','detail','thread'],m.groups())))
with (folder/'leituras-lentas.csv').open('w',newline='',encoding='utf-8') as f:
    w=csv.DictWriter(f,['timestamp','bytes','offset','microseconds','detail','thread']);w.writeheader();w.writerows(slow)
summary['slowReads']=len(slow)
summary['maxReadFileMs']=max((int(r['microseconds'])/1000 for r in slow),default=0)

summary['fatalHost']=len(re.findall(r'SIGSEGV|FALHA registradores|Falha fatal',new,re.I))
klog=(folder/'klog.txt').read_text(encoding='utf-8',errors='replace')
summary['klogSuspects']=[line for line in klog.splitlines() if re.search(r'GPU_FAULT|page fault|fatal|crash|killed',line,re.I)]
(folder/'eventos-memoria.txt').write_text('\n'.join(line for line in newlines if 'Memoria lenta:' in line),encoding='utf-8')
(folder/'quadros-lentos.txt').write_text('\n'.join(line for line in newlines if 'Quadro lento:' in line),encoding='utf-8')
(folder/'resumo.json').write_text(json.dumps(summary,indent=2,ensure_ascii=False),encoding='utf-8')
(folder/'eventos-leitura.txt').write_text('\n'.join(line for line in newlines if 'ReadFile' in line and 'Tempo no host:' not in line and 'Desempenho:' not in line),encoding='utf-8')
print(json.dumps(summary,indent=2,ensure_ascii=False))
print('WORST WINDOWS')
for r in sorted(rows,key=lambda r:r['max_ms'],reverse=True)[:4]: print(json.dumps(r))
