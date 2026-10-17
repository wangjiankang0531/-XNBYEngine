#!/usr/bin/env python3
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
directory = root/'work/releases/dom_view_lod_20261010'
manifest_path = directory/'manifest.json'
manifest = json.loads(manifest_path.read_text(encoding='utf-8'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


for item in manifest['runtime_files']:
    assert sha(directory/item['name']) == item['sha256']
assert len(manifest['runtime_files']) == 7
for item in manifest['inputs']:
    assert sha(root/item['path']) == item['sha256'], item['path']
for name in ('work/dom_view_lod_after.png', 'work/dom_view_lod_shipped.png', 'work/dom_view_lod_entry.png'):
    assert sha(root/name) == manifest['screenshot_sha256'], name
metrics = json.loads((root/'build_showcase_render_tests/huilian_dom_view_lod/metrics.json').read_text(encoding='utf-8'))
assert sha(root/'work/dom_view_lod_entry_off.png') == metrics['image_sha256']['before']
assert metrics['metrics']['center']['ratio'] > 1.25 and metrics['metrics']['foreground']['ratio'] > 1
travel = json.loads((root/'work/dom_view_lod_shipped_travel.json').read_text(encoding='utf-8'))
assert travel['passed'] and len(travel['stations']) == 4
assert travel['stations'][0]['lod'] == 2 and travel['stations'][1]['lod'] == 0
assert travel['stations'][3]['cache_hits'] > travel['stations'][2]['cache_hits']
for station in travel['stations']:
    assert station['cache_consistent'] and station['grid'] == 8
for name in ('work/dom_view_lod_shipped.json', 'work/dom_view_lod_entry.json'):
    stats = json.loads((root/name).read_text(encoding='utf-8'))
    assert stats['terrain_huilian_dom_initial_lod'] == 0 and stats['terrain_huilian_dom_view_hit']
    assert stats['terrain_huilian_dom_resident_working_set_pages'] == [8, 8]
    assert stats['terrain_huilian_dom_resident_gpu_budget_bytes'] == 357913936
log = (root/'work/dom_view_lod_regression_20261010.log').read_bytes()
assert log.count(b'Test Passed.') == 101 and b'Test Failed.' not in log
manifest['delivery_evidence'] = {p: sha(root/p) for p in (
    'work/dom_view_lod_shipped.png', 'work/dom_view_lod_entry.png', 'work/dom_view_lod_entry_off.png',
    'work/dom_view_lod_shipped.json', 'work/dom_view_lod_entry.json', 'work/dom_view_lod_shipped_travel.json')}
manifest_path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('VIEW DOM SNAPSHOT / SHIPPED / ENTRY / OFF / SHIPPED TRAVEL / 101 TEST AUDIT PASS')
