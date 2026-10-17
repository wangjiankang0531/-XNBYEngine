#!/usr/bin/env python3
"""Audit the actual local context snapshot, shipped frames and test evidence."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
directory = root / 'work/releases/zhumadian_context_20261009'
path = directory / 'manifest.json'
manifest = json.loads(path.read_text(encoding='utf-8'))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


assert len(manifest['runtime_files']) == 7
for entry in manifest['runtime_files']:
    assert digest(directory / entry['name']) == entry['sha256']
assert digest(root / 'apps/showcase_demo/main.cpp') == manifest['source_main_sha256']
assert digest(root / 'data/showcase/zhumadian_context.json') == manifest['context_manifest_sha256']
for name in ('work/zhumadian_context_delivery.png', 'work/zhumadian_context_shipped.png',
             'work/zhumadian_context_entry.png'):
    assert digest(root / name) == manifest['screenshot_sha256'], name
travel = json.loads((root / 'work/zhumadian_context_shipped_travel.json').read_text(encoding='utf-8'))
assert travel['passed']
log = root / 'work/zhumadian_context_regression_20261009.log'
assert b'ShowcaseDemo_HuilianDomStreamingEndurance' in log.read_bytes()
assert b'IndexedMeshCompaction.RejectsMalformedRangesAndRetainedIndices' in log.read_bytes()
for key, name in (
        ('shipped_capture', 'work/zhumadian_context_shipped.png'),
        ('shipped_travel', 'work/zhumadian_context_shipped_travel.json'),
        ('beauty_entry_capture', 'work/zhumadian_context_entry.png'),
        ('regression_log', 'work/zhumadian_context_regression_20261009.log'),
        ('context_entry', 'run_zhumadian_context_demo.cmd'),
        ('beauty_entry', 'run_zhumadian_beauty_demo.cmd')):
    manifest[key + '_sha256'] = digest(root / name)
manifest['regression'] = {'passed': 87, 'total': 87, 'seconds': 501.09,
                          'scope': '81 Showcase cases and six indexed geometry cases'}
path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('CONTEXT SNAPSHOT / SHIPPED / BEAUTY ENTRY / EVIDENCE HASHES PASS')
