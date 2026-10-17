#!/usr/bin/env python3
"""Audit the shipped fusion snapshot, captures, travel and full CTest evidence."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
directory = root/'work/releases/zhumadian_context_fusion_20261009'
path = directory/'manifest.json'
manifest = json.loads(path.read_text(encoding='utf-8'))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


assert len(manifest['runtime_files']) == 7
for entry in manifest['runtime_files']:
    assert digest(directory/entry['name']) == entry['sha256']
for key, name in (
        ('source_main', 'apps/showcase_demo/main.cpp'),
        ('context_manifest', 'data/showcase/zhumadian_context.json'),
        ('context_color_manifest', 'data/showcase/zhumadian_context_color.json'),
        ('context_color_builder', 'tools/build_zhumadian_context_color.py'),
        ('context_fusion_manifest', 'data/showcase/zhumadian_context_fusion.json'),
        ('context_fusion_builder', 'tools/build_zhumadian_context_fusion.py'),
        ('context_metrics', 'build_showcase_render_tests/zhumadian_context_fusion/metrics.json'),
        ('context_travel', 'build_showcase_render_tests/zhumadian_context_fusion/travel.json')):
    assert digest(root/name) == manifest[key+'_sha256'], name
for name in ('work/zhumadian_context_fusion_delivery.png', 'work/zhumadian_context_fusion_shipped.png',
             'work/zhumadian_context_fusion_entry.png'):
    assert digest(root/name) == manifest['screenshot_sha256'], name
travel = json.loads((root/'work/zhumadian_context_fusion_shipped_travel.json').read_text(encoding='utf-8'))
assert travel['passed'] and travel['stations'][1]['drawn_base_indices'] == 11668083
metrics = json.loads((root/'build_showcase_render_tests/zhumadian_context_fusion/metrics.json').read_text(encoding='utf-8'))
assert len(metrics['views']) == 5
for view in metrics['views'].values():
    assert view['lost_source_coverage_pixels'] == 0
    assert view['source_opaque_pixels'] == view['unchanged_source_opaque_pixels']
evidence = (root/'work/zhumadian_context_fusion_regression_20261009.log').read_bytes()
assert evidence.count(b'Test Passed.') == 93 and b'Test Failed.' not in evidence
assert b'ShowcaseDemo_ZhumadianContextFusionRender' in evidence
assert b'ShowcaseDemo_HuilianDomStreamingEndurance' in evidence
for key, name in (
        ('shipped_capture', 'work/zhumadian_context_fusion_shipped.png'),
        ('shipped_travel', 'work/zhumadian_context_fusion_shipped_travel.json'),
        ('entry_capture', 'work/zhumadian_context_fusion_entry.png'),
        ('regression_log', 'work/zhumadian_context_fusion_regression_20261009.log'),
        ('context_fusion_entry', 'run_zhumadian_context_fusion_demo.cmd')):
    manifest[key+'_sha256'] = digest(root/name)
manifest['regression'] = {'passed': 93, 'total': 93, 'scope': '87 Showcase cases and six indexed geometry cases'}
path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('FUSION SNAPSHOT / SHIPPED / ENTRY / FIVE VIEW / 93 TEST EVIDENCE PASS')
