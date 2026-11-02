import datetime, json, pathlib, socket, threading, time, urllib.request
from zoneinfo import ZoneInfo

ip = '192.168.15.7'
stamp = datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=-3))).strftime('%Y-%m-%d-title-1.27-watch-%H%M%S')
folder = pathlib.Path(__file__).parent / stamp
folder.mkdir()
stop = threading.Event()
events = []
def klog():
    with (folder / 'klog.txt').open('ab') as out:
        while not stop.is_set():
            try:
                with socket.create_connection((ip,3232),timeout=5) as s:
                    events.append('klog connected')
                    s.settimeout(2)
                    while not stop.is_set():
                        try: data=s.recv(65536)
                        except socket.timeout: continue
                        if not data: break
                        out.write(data); out.flush()
            except OSError as e: events.append('klog: '+str(e))
            stop.wait(2)
def download(remote, local):
    url='http://'+ip+':7070/api/ftp/download?path='+remote
    with urllib.request.urlopen(url,timeout=20) as response: data=response.read()
    if data.lstrip().startswith(b'{"error"'): raise RuntimeError(data[:300])
    (folder/local).write_bytes(data)
    return data.decode('utf-8',errors='replace')

t=threading.Thread(target=klog,daemon=True);t.start()
print('REPORT '+str(folder.resolve()),flush=True)
started=time.monotonic()
try:
    for n in range(7):
        stop.wait(max(0,started+n*30-time.monotonic()))
        title=download('/user/data/azimuth-ps5/title.txt',f'title-{n:02}.txt')
        host=download('/user/data/azimuth-ps5/host.txt',f'host-{n:02}.txt')
        (folder/'title.txt').write_text(title,encoding='utf-8')
        (folder/'host.txt').write_text(host,encoding='utf-8')
        lines=[x for x in host.splitlines() if 'Desempenho:' in x or 'Tempo no host:' in x]
        print(f'SNAPSHOT {n} elapsed={time.monotonic()-started:.1f}s bytes={len(host)} '+title.splitlines()[0],flush=True)
        for line in lines[-4:]: print(line,flush=True)
        if not lines: print('\n'.join(host.splitlines()[-6:]),flush=True)
        if n in (0,6):
            download('/data/PC/FH2/data/logs/azimuth.log',f'azimuth-{n:02}.log')
            download('/data/PC/FH2/data/azimuth.ini',f'azimuth-{n:02}.ini')
except Exception as e:
    events.append('capture error: '+str(e));print(events[-1],flush=True)
finally:
    stop.set();t.join(timeout=7)
    (folder/'capture.json').write_text(json.dumps({'ip':ip,'elapsedSeconds':time.monotonic()-started,'events':events},indent=2),encoding='utf-8')
    print('DONE '+str(folder.resolve()),flush=True)
