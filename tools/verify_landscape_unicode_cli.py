"""Regression for a Chinese/spaced layer path on Windows with a non-UTF8 ACP."""
import hashlib
import argparse
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', default='ht_landscape_editor_0_7.exe')
    parser.add_argument('--output', default='work/landscape_apps_checks')
    args = parser.parse_args()
    package = ROOT / 'work/releases/landscape_apps_20261011'
    out = ROOT / args.output
    out.mkdir(parents=True, exist_ok=True)
    exe = package / args.exe
    document = (ROOT / 'work/landscape_apps_checks/ui_final6.json').read_bytes()
    source = out / '中文 路径' / '标绘图层.json'
    source.parent.mkdir(parents=True, exist_ok=True)
    source.write_bytes(document)
    result = out / 'unicode_roundtrip.json'
    subprocess.run([str(exe), '--validate-layers', f'--layers={source}', f'--stats={result}'],
                   cwd=out, check=True, timeout=20)
    assert json.loads(result.read_text(encoding='utf-8')) == json.loads(document.decode('utf-8'))
    (out / 'unicode_cli.json').write_text(json.dumps({
        'passed': True, 'input_path': str(source), 'roundtrip_equal': True,
        'exe_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
    }, ensure_ascii=False, indent=2), encoding='utf-8')
    print('CHINESE / SPACED WINDOWS LAYER PATH PASS')


if __name__ == '__main__':
    main()
