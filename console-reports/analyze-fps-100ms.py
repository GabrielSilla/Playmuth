import csv,json,pathlib,re,sys
folder=pathlib.Path(sys.argv[1])
pattern=re.compile(r'(\d+) FPS 100ms: ([\d.]+) fps, (\d+) quadros, janela ([\d.]+) ms, max intervalo ([\d.]+) ms, sem novo quadro ([\d.]+) ms, fim mono ns (\d+), ultimo quadro mono ns (\d+)')
initial=(folder/'host-00.txt').read_text(encoding='utf-8',errors='replace')
baseline=max((int(m[7]) for m in pattern.finditer(initial)),default=0)
host=(folder/'host.txt').read_text(encoding='utf-8',errors='replace')
keys=['timestamp','fps','frames','window_ms','max_interval_ms','no_new_frame_ms','end_mono_ns','last_frame_mono_ns']
rows=[]
for m in pattern.finditer(host):
    if int(m[7])<=baseline:continue
    values=m.groups();rows.append(dict(zip(keys,[int(values[0]),float(values[1]),int(values[2]),float(values[3]),float(values[4]),float(values[5]),int(values[6]),int(values[7])])))
assert rows,'No new 100 ms records; requires title 1.37 with game running'
with (folder/'fps-100ms.csv').open('w',newline='',encoding='utf-8') as out:
    w=csv.DictWriter(out,keys);w.writeheader();w.writerows(rows)
summary={'samples':len(rows),'baselineMonoNs':baseline,'minWindowFps':min(r['fps'] for r in rows),'below50Samples':sum(r['fps']<50 for r in rows),'zeroFrameSamples':sum(r['frames']==0 for r in rows),'maxIntervalMs':max(r['max_interval_ms'] for r in rows),'maxNoNewFrameMs':max(r['no_new_frame_ms'] for r in rows),'maxSamplerWindowMs':max(r['window_ms'] for r in rows),'averageFpsByElapsedTime':1000*sum(r['frames'] for r in rows)/sum(r['window_ms'] for r in rows)}
(folder/'resumo-100ms.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(json.dumps(summary,indent=2))
