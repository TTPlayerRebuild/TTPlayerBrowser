"""Exercise real WebView2 and IE against an in-process loopback fixture only."""
import argparse
import http.server
import json
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time
import urllib.parse

events = []
page = r'''<!doctype html><html><head><meta http-equiv="X-UA-Compatible" content="IE=edge"><meta charset="utf-8"><title>TTPlayer bridge fixture</title></head>
<body style="font:18px sans-serif;background:#eef4fa;color:#123;padding:24px"><h1>Music window integration fixture</h1><p id="status">Testing window.external bridge...</p>
<script>
function report(s){document.getElementById('status').innerText=s;var image=new Image();image.src='/report?message='+encodeURIComponent(s);window.reportImage=image;}
setTimeout(function(){try{
 var p=window.external.ttplayer;
 if(location.search.indexOf('second')>=0){p.EndAddSongs(0,1,1);report('SECOND_OK');return;}
 if(location.search.indexOf('foreign')>=0){p.AddSoundToPlay('http://fixture/forbidden.mp3','','forbidden',1);report('FOREIGN_UNEXPECTED_ACCESS');return;}
 if(location.search.indexOf('redirect')>=0){location.href=location.href.replace('127.0.0.1','localhost').replace('redirect','foreign');return;}
 for(var i=1;i<=6;i++){if(window.external['ttplayer'+(i==1?'':i)].GetPlayerInfo(1)!='5.7.9')throw Error('version');}
 p.BeginAddSongs();p.AddSong('http://fixture/one.mp3','first artist','first title');p.AddSong('http://fixture/two.mp3','second artist','second title');p.EndAddSongs(0,1,1);
 p.AddSoundToPlayByTid('http://fixture/tid.mp3','third artist','third title','web-tid',1);
 p.AddDownloadInfoByTid('http://fixture/download.mp3','download artist','web-download','download-tid',1);
 p.BeginAddSongs();p.AddSong('http://fixture/stale.mp3','stale artist','stale title');
 report('BRIDGE_OK');
}catch(e){report('ERROR '+(e.message||e.description||String(e)));}},300);
</script></body></html>'''

class Handler(http.server.BaseHTTPRequestHandler):
    def handle(self):
        try:
            super().handle()
        except (ConnectionResetError, BrokenPipeError):
            # Navigation cancellation and controller shutdown abort HTTP reads.
            pass
    def do_GET(self):
        if self.path.startswith('/report'):
            events.append(urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query).get('message', [''])[0])
            self.send_response(204); self.end_headers(); return
        data=page.encode('utf-8');self.send_response(200);self.send_header('Content-Type','text/html; charset=utf-8')
        self.send_header('Cache-Control','no-store');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    def log_message(self,*args): pass

def main():
    parser=argparse.ArgumentParser();parser.add_argument('build');parser.add_argument('--output',required=True);args=parser.parse_args()
    build=Path(args.build).resolve();output=Path(args.output).resolve();output.mkdir(parents=True,exist_ok=True)
    # A fresh runtime containing only the host probe and merged DLL ensures an
    # old loader left in a developer's build directory cannot hide a regression.
    runtime=output/'runtime-bin';runtime.mkdir(exist_ok=False)
    for name in ('ttpbrowser_runtime_tests.exe','ttpbrowser.dll'):
        shutil.copy2(build/name,runtime/name)
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
    failures=0
    for engine,name in ((2,'webview2'),(1,'ie'),(0,'auto-fallback')):
        events.clear();target=output/name;target.mkdir(exist_ok=True)
        env=os.environ.copy()
        if engine==0:
            absent=target/'absent-runtime'
            if absent.exists():raise RuntimeError('Fallback fixture must not contain a Runtime')
            env['WEBVIEW2_BROWSER_EXECUTABLE_FOLDER']=str(absent)
        result=subprocess.run([str(runtime/'ttpbrowser_runtime_tests.exe'),str(runtime/'ttpbrowser.dll'),f'http://127.0.0.1:{server.server_port}/fixture',str(engine),str(target)],cwd=runtime,env=env,capture_output=True,text=True,timeout=45)
        (target/'results.txt').write_text(result.stdout+'\n'+result.stderr,encoding='utf-8');(target/'page-events.json').write_text(json.dumps(events,indent=2),encoding='utf-8')
        print(f'Engine {engine}: exit {result.returncode}\n{result.stdout}\nPage events: {events}',flush=True)
        failures += result.returncode!=0 or 'BRIDGE_OK' not in events or 'SECOND_OK' not in events or 'FOREIGN_UNEXPECTED_ACCESS' in events
    server.shutdown();return int(failures>0)
if __name__=='__main__':raise SystemExit(main())
