"""Start the packaged local viewer and open its URL without shell policy changes."""
import argparse
import json
import subprocess
import sys
import time
import urllib.request
import webbrowser
from pathlib import Path

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--port',type=int,default=8767);parser.add_argument('--no-browser',action='store_true');args=parser.parse_args()
    root=Path(__file__).resolve().parents[1];url=f'http://127.0.0.1:{args.port}'
    def healthy():
        try:
            with urllib.request.urlopen(url+'/api/health',timeout=1) as r:return json.load(r)['application']=='hongtu.landscape.web.v1'
        except Exception:return False
    if not healthy():
        logs=root/'work/web_logs';logs.mkdir(parents=True,exist_ok=True);stamp=time.strftime('%Y%m%d_%H%M%S')
        with (logs/f'{stamp}.log').open('wb') as stream:
            process=subprocess.Popen([sys.executable,str(root/'tools/landscape_web_server.py'),f'--port={args.port}'],cwd=root,stdout=stream,stderr=stream,creationflags=subprocess.CREATE_NO_WINDOW if sys.platform=='win32' else 0)
        (logs/f'{stamp}.pid').write_text(str(process.pid))
        for _ in range(40):
            if healthy():break
            if process.poll() is not None:raise RuntimeError(f'网页服务启动失败，请查看 {logs}')
            time.sleep(.15)
        else:raise RuntimeError(f'网页服务尚未就绪，请查看 {logs}')
    print('XNBY landscape viewer:',url)
    if not args.no_browser:webbrowser.open(url)

if __name__=='__main__':main()
