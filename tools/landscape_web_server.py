"""Serve only the Zhumadian viewer/data and schedule one bounded native converter.

Local manual test server. Public deployments should serve dist + prepared data
behind their existing HTTPS/authentication gateway; no upload or write endpoint.
"""
import argparse
import concurrent.futures
import http.server
import json
import os
import re
import subprocess
import threading
from pathlib import Path
from urllib.parse import unquote,urlsplit

ROOT=Path(__file__).resolve().parents[1]
POOL=concurrent.futures.ThreadPoolExecutor(max_workers=1)
JOBS={};LOCK=threading.Lock()
def convert(tile,depth,python):
    command=[str(python),'-X','utf8',str(ROOT/'tools/convert_zhumadian_native_chunks.py'),f'--tile={tile}',f'--depth={depth}']
    result=subprocess.run(command,cwd=ROOT,creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0,
                          stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
    folder=ROOT/f'work/zhumadian_native_chunk_cache/tile_{tile}_depth_{depth}'
    if result.returncode:
        failure=folder/'failure.json'
        reason=json.loads(failure.read_text(encoding='utf-8')).get('reason','conversion failed') if failure.exists() else result.stdout.decode('utf-8',errors='replace')[-4000:]
        raise RuntimeError(reason)
    marker=json.loads((folder/'complete.json').read_text(encoding='utf-8'))
    if not marker.get('complete_frontier'):raise RuntimeError('frontier incomplete')
    return marker

def bundle_present(tile,depth,marker):
    folder=ROOT/f'work/zhumadian_native_chunk_cache/tile_{tile}_depth_{depth}'
    if not (folder/'complete.json').is_file():return False
    for part in marker.get('parts',[]):
        if not re.fullmatch(r'part_\d{3}',part['directory']):return False
        for file in part['files']:
            if file['name'] not in {'geometry.gltf','geometry.bin','atlas.jpg'}:return False
            path=folder/part['directory']/file['name']
            if not path.is_file() or path.stat().st_size!=file['bytes']:return False
    return bool(marker.get('parts'))

class Handler(http.server.SimpleHTTPRequestHandler):
    root=ROOT; python=None; layers=None
    def do_GET(self):
        route=urlsplit(self.path).path
        if route.startswith('/api/lod/'):
            match=re.fullmatch(r'/api/lod/(\d{1,4})/(\d{1,2})',route)
            if not match:return self.send_error(400)
            tile,depth=map(int,match.groups())
            if not 1<=tile<=2346 or not 1<=depth<=12:return self.send_error(400)
            key=(tile,depth)
            with LOCK:
                previous=JOBS.get(key)
                if previous and previous.done():
                    try:present=bundle_present(tile,depth,previous.result())
                    except Exception:present=True  # Surface the real conversion error.
                    if not present:JOBS.pop(key)
                # Keep the worker queue bounded. Browser requests are serial.
                if key not in JOBS:
                    if sum(not job.done() for job in JOBS.values())>=2:
                        self.send_response(202);self.end_headers();return
                    JOBS[key]=POOL.submit(convert,tile,depth,self.python)
                    if len(JOBS)>256:
                        for old in list(JOBS):
                            if len(JOBS)<=256:break
                            if old!=key and JOBS[old].done():JOBS.pop(old)
                job=JOBS[key]
            if not job.done():self.send_response(202);self.end_headers();return
            try:data=json.dumps(job.result()).encode('utf-8')
            except Exception as error:data=str(error).encode('utf-8');self.send_response(422)
            else:self.send_response(200)
            self.send_header('Content-Type','application/json; charset=utf-8');self.send_header('Content-Length',str(len(data)));self.send_header('Cache-Control','no-store');self.end_headers();self.wfile.write(data);return
        if route=='/api/health':
            data=b'{"application":"hongtu.landscape.web.v1","scene":"zhumadian"}'
            self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data);return
        return super().do_GET()
    def translate_path(self,path):
        route=unquote(urlsplit(path).path).lstrip('/')
        if '\\' in route or '..' in route.split('/') or '\x00' in route:return str(ROOT/'work/web_not_found')
        if route in ('','index.html','viewer.js','THREE-LICENSE.txt'):
            return str(self.root/'apps/landscape_web/dist'/(route or 'index.html'))
        if route=='layers/default.json':return str(self.layers)
        if route=='layers/sample.json':return str(self.root/'projects/驻马店图层示例.json')
        if route=='assets/landscape/icons.png' or route=='data/showcase/zhumadian_native_hierarchy.json':return str(self.root/route)
        if re.fullmatch(r'assets/showcase/runtime/zhumadian/(full|context)/[\w.-]+\.(gltf|bin|jpg)',route):return str(self.root/route)
        if re.fullmatch(r'native/tile_\d+_depth_\d+/part_\d{3}/(geometry\.(gltf|bin)|atlas\.jpg)',route):
            return str(self.root/'work/zhumadian_native_chunk_cache'/route.removeprefix('native/'))
        return str(ROOT/'work/web_not_found')
    def end_headers(self):
        self.send_header('X-Content-Type-Options','nosniff');self.send_header('Cache-Control','no-cache');super().end_headers()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',default='127.0.0.1');parser.add_argument('--port',type=int,default=8765)
    parser.add_argument('--layers',type=Path);parser.add_argument('--python',type=Path)
    args=parser.parse_args();Handler.layers=args.layers or ROOT/'projects/驻马店图层.json'
    Handler.python=args.python or ROOT/'runtime/python/python.exe'
    if not Handler.python.exists():Handler.python=Path.home()/'.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
    server=http.server.ThreadingHTTPServer((args.host,args.port),Handler)
    print(f'XNBY LANDSCAPE WEB http://{args.host}:{args.port}  (Ctrl+C to stop)',flush=True)
    try:server.serve_forever()
    except KeyboardInterrupt:pass
    finally:server.server_close();POOL.shutdown(wait=False,cancel_futures=True)
if __name__=='__main__':main()
