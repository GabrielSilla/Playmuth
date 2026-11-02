import csv,json,pathlib,re,sys
folder=pathlib.Path(sys.argv[1])
host=(folder/'host.txt').read_text(encoding='utf-8',errors='replace')
initial=(folder/'host-00.txt').read_text(encoding='utf-8',errors='replace')
baseline=max(map(int,re.findall(r'^(\d+) Desempenho:',initial,re.M)))
frames=[];memory=[];dropped=0
for line in host.splitlines():
    m=re.match(r'(\d+) ',line)
    if not m or int(m[1])<=baseline: continue
    m=re.match(r'(\d+) Quadro lento: thread (\d+), fim intervalo mono ns (\d+), intervalo ([\d.]+) ms, Present anterior ([\d.]+) ms, Present atual ([\d.]+) ms',line)
    if m:
        wall,thread,end,gap,previous,current=m.groups();end=int(end);gap=float(gap)
        frames.append(dict(timestamp=int(wall),thread=int(thread),begin_ns_approx=end-round(gap*1e6),end_ns=end,gap_ms=gap,previous_present_ms=float(previous),current_present_ms=float(current)))
    m=re.match(r'(\d+) Memoria lenta: (\w+), thread (\d+), inicio mono ns (\d+), fim mono ns (\d+), duracao ms ([\d.]+), endereco (0x[0-9a-f]+), bytes pedidos (\d+)',line)
    if m:
        wall,api,thread,begin,end,duration,address,size=m.groups()
        memory.append(dict(log_timestamp=int(wall),api=api,thread=int(thread),begin_ns=int(begin),end_ns=int(end),duration_ms=float(duration),address=address,requested_bytes=int(size)))
    m=re.search(r'Memoria lenta: (\d+) eventos descartados',line)
    if m:dropped+=int(m[1])
pairs=[]
for f in frames:
    for m in memory:
        overlap=min(f['end_ns'],m['end_ns'])-max(f['begin_ns_approx'],m['begin_ns'])
        if overlap>0:pairs.append(dict(frame_timestamp=f['timestamp'],frame_thread=f['thread'],frame_gap_ms=f['gap_ms'],memory_thread=m['thread'],same_thread=f['thread']==m['thread'],api=m['api'],memory_duration_ms=m['duration_ms'],overlap_ms=overlap/1e6,memory_begin_ns=m['begin_ns'],memory_end_ns=m['end_ns']))
for name,rows,fields in [('quadros-lentos.csv',frames,['timestamp','thread','begin_ns_approx','end_ns','gap_ms','previous_present_ms','current_present_ms']),('memoria-lenta.csv',memory,['log_timestamp','api','thread','begin_ns','end_ns','duration_ms','address','requested_bytes']),('sobreposicoes.csv',pairs,['frame_timestamp','frame_thread','frame_gap_ms','memory_thread','same_thread','api','memory_duration_ms','overlap_ms','memory_begin_ns','memory_end_ns'])]:
    with (folder/name).open('w',newline='',encoding='utf-8') as out:
        writer=csv.DictWriter(out,fields);writer.writeheader();writer.writerows(rows)
summary={'sampledSlowFrames':len(frames),'memoryEvents':len(memory),'droppedMemoryEvents':dropped,'framesWithMemoryOverlap':len(set(p['frame_timestamp'] for p in pairs)),'sameThreadOverlapPairs':sum(p['same_thread'] for p in pairs),'maxMemoryMs':max((m['duration_ms'] for m in memory),default=0),'maxPreviousPresentMs':max((f['previous_present_ms'] for f in frames),default=0),'maxCurrentPresentMs':max((f['current_present_ms'] for f in frames),default=0)}
(folder/'correlacao.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(json.dumps(summary,indent=2))
print('LONGEST SAMPLED FRAMES')
for f in sorted(frames,key=lambda f:f['gap_ms'],reverse=True)[:5]:print(json.dumps(f))
print('LONGEST MEMORY EVENTS')
for m in sorted(memory,key=lambda m:m['duration_ms'],reverse=True)[:5]:print(json.dumps(m))
