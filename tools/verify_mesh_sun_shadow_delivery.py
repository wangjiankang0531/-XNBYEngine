#!/usr/bin/env python3
"""Audit actual shipped/entry captures and independently counted regression results."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
directory = root/'work/releases/mesh_sun_shadow_20261010'
path = directory/'manifest.json'
manifest = json.loads(path.read_text(encoding='utf-8'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


for item in manifest['runtime_files']:
    assert sha(directory/item['name']) == item['sha256'], item['name']
assert len(manifest['runtime_files']) == 7
for item in manifest['inputs']:
    assert sha(root/item['path']) == item['sha256'], item['path']
metrics = json.loads((root/'build_showcase_render_tests/mesh_sun_shadows/metrics.json').read_text(encoding='utf-8'))
for name in ('work/mesh_sun_shadow_delivery.png', 'work/mesh_sun_shadow_shipped.png',
             'work/mesh_sun_shadow_entry.png'):
    assert sha(root/name) == manifest['screenshot_sha256'], name
assert sha(root/'work/mesh_sun_shadow_entry_off.png') == metrics['image_sha256']['valve_off']
for name in ('fixture_135_mesh', 'fixture_315_mesh'):
    assert metrics['metrics'][name]['failures'] == 0
    assert metrics['metrics'][name]['ray_checked_samples'] > 3600
assert metrics['metrics']['photo']['unchanged_reference']
stats = json.loads((root/'work/mesh_sun_shadow_shipped.json').read_text(encoding='utf-8'))
assert stats['mesh_shadow_active'] and stats['mesh_shadow_rendered_passes'] == 1
assert stats['mesh_shadow_submitted_indices'] == stats['indices']
assert stats['mesh_shadow_map_side'] == 2048
log = (root/'work/mesh_sun_shadow_regression_20261010.log').read_bytes()
assert log.count(b'Test Passed.') == 95 and b'Test Failed.' not in log
assert b'ShowcaseDemo_MeshSunShadowsInteractive' in log
assert b'ShowcaseDemo_HuilianDomStreamingEndurance' in log
for name in ('work/mesh_sun_shadow_shipped.png', 'work/mesh_sun_shadow_shipped.json',
             'work/mesh_sun_shadow_entry.png', 'work/mesh_sun_shadow_entry_off.png'):
    manifest.setdefault('delivery_evidence', {})[name] = sha(root/name)
path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('MESH SHADOW SNAPSHOT / SHIPPED / ENTRY / OFF COMPARISON / 95 TEST EVIDENCE PASS')
