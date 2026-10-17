"""Read-only source inventory and requirement extraction for the 2026-10-12 migration."""
import json
import os
import subprocess
import zipfile
from pathlib import Path
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'work/migration_20261012'
OUT.mkdir(parents=True, exist_ok=True)

records = []
for entry in ROOT.iterdir():
    count = size = 0
    if entry.is_dir():
        for folder, dirs, files in os.walk(entry, followlinks=False):
            dirs[:] = [d for d in dirs if not (Path(folder)/d).is_symlink()]
            for name in files:
                try:
                    size += (Path(folder)/name).stat().st_size
                    count += 1
                except OSError:
                    pass
    else:
        count, size = 1, entry.stat().st_size
    records.append({'name': entry.name, 'files': count, 'bytes': size})
(OUT/'inventory.json').write_text(json.dumps(records, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(sorted(records, key=lambda r: -r['bytes']), ensure_ascii=False, indent=2))

document = Path('D:/Project/Zhumadian/Document/Require/需求说明书261012.docx')
with zipfile.ZipFile(document) as archive:
    root = ET.fromstring(archive.read('word/document.xml'))
    ns = {'w': 'http://schemas.openxmlformats.org/wordprocessingml/2006/main'}
    lines = [''.join(p.itertext()) for p in []]
    lines = [''.join(t.text or '' for t in p.findall('.//w:t', ns)) for p in root.findall('.//w:p', ns)]
    media = [i for i in archive.infolist() if i.filename.startswith('word/media/')]
(OUT/'requirements.txt').write_text('\n'.join(lines), encoding='utf-8')
print('REQUIREMENTS', len(lines), 'paragraphs', len(media), 'media objects')

env = os.environ.copy()
env.update(GCM_INTERACTIVE='never', GIT_TERMINAL_PROMPT='0')
try:
    result = subprocess.run(['git', '-c', 'credential.interactive=never', 'credential', 'fill'],
        input='protocol=https\nhost=github.com\n\n', text=True, capture_output=True, env=env, timeout=15)
    fields = dict(line.split('=', 1) for line in result.stdout.splitlines() if '=' in line)
    print('GIT_CREDENTIAL_AVAILABLE', bool(fields.get('password')))
except Exception as error:
    print('GIT_CREDENTIAL_CHECK', type(error).__name__)
