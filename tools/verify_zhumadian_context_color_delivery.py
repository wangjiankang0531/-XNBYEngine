#!/usr/bin/env python3
"""Check the packaged color-fusion runtime and independently counted CTest evidence."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
directory = root/'work/releases/zhumadian_context_color_20261009'
path = directory/'manifest.json'
manifest = json.loads(path.read_text(encoding='utf-8'))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


assert len(manifest['runtime_files']) == 7
for entry in manifest['runtime_files']:
    assert digest(directory/entry['name']) == entry['sha256']
for key, name in (('source_main', 'apps/showcase_demo/main.cpp'),
                  ('context_manifest', 'data/showcase/zhumadian_context.json'),
                  ('context_color_manifest', 'data/showcase/zhumadian_context_color.json'),
                  ('context_color_builder', 'tools/build_zhumadian_context_color.py'),
                  ('context_metrics', 'build_showcase_render_tests/zhumadian_context_color/metrics.json'),
                  ('context_travel', 'build_showcase_render_tests/zhumadian_context_color/travel.json')):
    assert digest(root/name) == manifest[key+'_sha256']
for name in ('work/zhumadian_context_color_delivery.png', 'work/zhumadian_context_color_shipped.png',
             'work/zhumadian_context_color_entry.png'):
    assert digest(root/name) == manifest['screenshot_sha256'], name
travel = json.loads((root/'work/zhumadian_context_color_shipped_travel.json').read_text(encoding='utf-8'))
assert travel['passed'] and travel['stations'][1]['drawn_base_indices'] == 10223619
log = root/'work/zhumadian_context_color_regression_20261009.log'
evidence = log.read_bytes()
assert evidence.count(b'Test Passed.') == 90 and b'Test Failed.' not in evidence
assert b'ShowcaseDemo_ZhumadianContextColorRender' in evidence
assert b'ShowcaseDemo_HuilianDomStreamingEndurance' in evidence
for key, name in (
        ('shipped_capture', 'work/zhumadian_context_color_shipped.png'),
        ('shipped_travel', 'work/zhumadian_context_color_shipped_travel.json'),
        ('beauty_entry_capture', 'work/zhumadian_context_color_entry.png'),
        ('regression_log', 'work/zhumadian_context_color_regression_20261009.log'),
        ('context_color_entry', 'run_zhumadian_context_color_demo.cmd'),
        ('beauty_entry', 'run_zhumadian_beauty_demo.cmd')):
    manifest[key+'_sha256'] = digest(root/name)
manifest['regression'] = {'passed': 90, 'total': 90, 'scope': '84 Showcase cases and six indexed geometry cases'}
path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('COLOR SNAPSHOT / SHIPPED / BEAUTY ENTRY / 90 TEST EVIDENCE HASHES PASS')
