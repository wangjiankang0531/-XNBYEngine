"""Audit the new snapshot and actual shipped/entry captures without touching prior releases."""
import hashlib
import json
from pathlib import Path
from verify_zhumadian_native_lod import validate

root = Path(__file__).resolve().parents[1]
directory = root/'work/releases/native_osgb_lod_20261010'
path = directory/'manifest.json'
manifest = json.loads(path.read_text(encoding='utf-8'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


for item in manifest['runtime_files']:
    assert sha(directory/item['name']) == item['sha256']
assert len(manifest['runtime_files']) == 7
for item in manifest['inputs']:
    assert sha(root/item['path']) == item['sha256'], item['path']
for name in ('work/native_lod_after.png', 'work/native_lod_shipped.png', 'work/native_lod_entry.png'):
    assert sha(root/name) == manifest['screenshot_sha256'], name
metrics = json.loads((root/'build_showcase_render_tests/apps/showcase_demo/native_lod/metrics.json').read_text(encoding='utf-8'))
assert sha(root/'work/native_lod_entry_base.png') == metrics['before_sha256']
assert metrics['passed'] and metrics['source_pixels'][1] >= metrics['source_pixels'][0]*.98
assert metrics['viewport_depths'][1] > metrics['viewport_depths'][0]
for name in ('work/native_lod_shipped.json', 'work/native_lod_entry.json'):
    stats = json.loads((root/name).read_text(encoding='utf-8'))
    assert stats['zhumadian_native_recursive_streaming']
    native = stats['zhumadian_native_lod']
    validate(native)
    assert any(r['tile'] == 141 and r['depth'] >= 5 for r in native['resident'])
    assert native['settled'] and not native['pending']
travel = json.loads((root/'work/native_lod_shipped_travel/travel.json').read_text(encoding='utf-8'))
assert travel['passed'] and len(travel['stations']) == 6
for station in travel['stations']:
    validate(station)
log = (root/'work/native_lod_regression_20261010.log').read_bytes()
assert log.count(b'Test Passed.') == 107 and b'Test Failed.' not in log
final_log = (root/'work/native_lod_final_native_20261010.log').read_bytes()
assert final_log.count(b'Test Passed.') == 2 and b'Test Failed.' not in final_log
final_build = json.loads((root/'work/native_lod_final_build.json').read_text(encoding='utf-8'))
assert final_build['passed'] == 2 and manifest['final_native_gates_passed'] == 2
assert final_build['exe_sha256'] == sha(directory/'showcase_demo.exe')
manifest['delivery_evidence'] = {name: sha(root/name) for name in (
    'work/native_lod_shipped.png', 'work/native_lod_entry.png', 'work/native_lod_entry_base.png',
    'work/native_lod_shipped.json', 'work/native_lod_entry.json', 'work/native_lod_shipped_travel/travel.json',
    'work/native_lod_source_quality_20261010.json')}
path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('NATIVE OSGB SNAPSHOT / SHIPPED / ENTRY / BASE / SHIPPED TRAVEL / 107 COMPATIBILITY + 2 FINAL GATE AUDIT PASS')
