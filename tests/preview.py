"""Local UI acceptance server. Synthetic data only; never transmits RF.
Run: python3 tests/preview.py [port] [--empty|--unsynced|--storage-error|--history-error]
"""
import json
from datetime import datetime, timedelta, timezone
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse
import sys
import time
ROOT = Path(__file__).resolve().parents[1] / 'main'
DAY = 86400
OFFSET = 28800
flags = set(sys.argv[2:])
state = dict(power_known=True, power=False, brightness=0, temperature=4000,
             radio_ready=True, storage_ready=True, sample_loaded=True,
             power_transmissions=0, last_action='startup', last_error='',
             time_synced='--unsynced' not in flags,
             history_ready='--storage-error' not in flags,
             history_recording='--unsynced' not in flags,
             history_error='ESP_FAIL' if '--storage-error' in flags else '')
now = int(time.time())
today = (now + OFFSET) // DAY * DAY - OFFSET
records = []
if '--empty' not in flags:
    for i in range(36):
        start = today - i * DAY
        if i == 3:  # A reboot/offline day has no observations.
            continue
        for a, b, on, cause in [(0,8,0,0),(8,10 + i % 5,1,1),
                                 (10 + i % 5,18,0,1),(18,22,1,2),(22,24,0,1)]:
            end = min(start + b * 3600, now)
            if end >= start + a * 3600:
                records.append([start + a * 3600, end, on, cause])

active_record = next((r for r in records if r[1] == now), None)

def oldest(now):
    d = datetime.fromtimestamp(now, timezone(timedelta(hours=8)))
    try:
        d = d.replace(year=d.year-1)
    except ValueError:
        d = d.replace(year=d.year-1, day=28)
    return int(d.replace(hour=0, minute=0, second=0, microsecond=0).timestamp())

class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)
    def respond(self, obj, status=200):
        data=json.dumps(obj).encode()
        self.send_response(status)
        self.send_header('Content-Type','application/json')
        self.send_header('Cache-Control','no-store')
        self.send_header('Content-Length',str(len(data)))
        self.end_headers(); self.wfile.write(data)
    def do_GET(self):
        path=urlparse(self.path)
        if path.path == '/api/state':
            return self.respond(state)
        if path.path == '/api/history':
            if '--history-error' in flags:
                return self.respond({'error':'unavailable'},503)
            if active_record is not None and state['time_synced']:
                active_record[1]=int(time.time())
            q=parse_qs(path.query)
            a,b=int(q['from'][0]),int(q['to'][0])
            return self.respond(dict(time_synced=state['time_synced'], ready=state['history_ready'],
                recording=state['history_recording'], now=int(time.time()) if state['time_synced'] else 0,
                oldest=oldest(time.time()), error=state['history_error'], incomplete=False,
                records=[r for r in records if r[0]<b and r[1]>=a]))
        if path.path == '/three.module.min.js':
            self.path='/vendor/three.module.min.js'
        return super().do_GET()
    def do_POST(self):
        global active_record
        if urlparse(self.path).path != '/api/action':
            return self.respond({},404)
        action=self.rfile.read(int(self.headers.get('Content-Length','0'))).decode()
        target=action in ('on','sync_on')
        if action not in ('on','off','sync_on','sync_off'):
            return self.respond({},400)
        if action in ('on','off') and state['power']==target:
            state['last_action']='already_'+action
        else:
            state['power']=target;state['power_known']=True;state['last_action']=action
            state['brightness']=100 if target else 0
            if not action.startswith('sync_'):state['power_transmissions']+=1
            stamp=int(time.time())
            if state['time_synced'] and state['history_ready']:
                if active_record is not None: active_record[1]=stamp
                active_record=[stamp,stamp,int(target),2 if action.startswith('sync_') else 1]
                records.append(active_record)
        return self.respond(state)

if __name__ == '__main__':
    port=int(sys.argv[1]) if len(sys.argv)>1 else 8765
    print(f'SYNTHETIC API PREVIEW: http://127.0.0.1:{port}',flush=True)
    ThreadingHTTPServer(('127.0.0.1',port),Handler).serve_forever()
