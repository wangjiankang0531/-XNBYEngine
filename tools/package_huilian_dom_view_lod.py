#!/usr/bin/env python3
"""Snapshot the verified view-driven DOM prototype without modifying old releases."""
import hashlib
import json
import shutil
from pathlib import Path

root = Path(__file__).resolve().parents[1]
target = root/'work/releases/dom_view_lod_20261010'
source = root/'build_showcase_render_tests/apps/showcase_demo/Release'
log = root/'work/dom_view_lod_regression_20261010.log'
evidence = log.read_bytes()
assert evidence.count(b'Test Passed.') == 101 and b'Test Failed.' not in evidence
assert b'ShowcaseDemo_HuilianDomViewLodTravel' in evidence and b'DomViewLod.' in evidence
target.mkdir(parents=True, exist_ok=True)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


runtime = []
for path in [source/'showcase_demo.exe', *sorted(source.glob('*.dll'))]:
    dest = target/path.name
    shutil.copy2(path, dest)
    assert sha(dest) == sha(path)
    runtime.append({'name': dest.name, 'sha256': sha(dest), 'bytes': dest.stat().st_size})
assert len(runtime) == 7
captures = root/'build_showcase_render_tests/huilian_dom_view_lod'
for name in ('before', 'after'):
    shutil.copy2(captures/f'{name}.png', root/f'work/dom_view_lod_{name}.png')
inputs = [
    'apps/showcase_demo/main.cpp', 'apps/showcase_demo/CMakeLists.txt', 'tests/CMakeLists.txt',
    'renderer/include/hongtu/renderer/dom_view_lod.hpp', 'tests/src/renderer/test_dom_view_lod.cpp',
    'tools/verify_huilian_dom_view_lod.py', 'run_huilian_view_lod_demo.cmd',
    'assets/showcase/runtime/huilian_dom_gpu_manifest.json', 'data/showcase/huilian_dom_pyramid_quality.json',
    'build_showcase_render_tests/huilian_dom_view_lod/metrics.json',
    'build_showcase_render_tests/huilian_dom_view_lod/travel.json',
    'work/dom_view_lod_regression_20261010.log',
]
manifest = {'version': target.name, 'runtime_files': runtime, 'regression_passed': 101,
    'source_root_required': str(root), 'inputs': [{'path': p, 'sha256': sha(root/p)} for p in inputs],
    'screenshot_sha256': sha(root/'work/dom_view_lod_after.png'),
    'scope': 'Huilian native DOM view footprint / pixel LOD / 8x8 page window; no OSGB spike repair',
    'resident_dom_gpu_budget_bytes': 357913936,
    'memory_limit': 'Four resident mip atlases; replacement/upload and CPU decoding need temporary memory.',
    'source_resolution_limit_m': 0.5}
(target/'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
(target/'README.md').write_text(
    '# View-driven native DOM prototype\n\n'
    'Run run_huilian_view_lod_demo.cmd in the original project.\n'
    'Compare --huilian-dom-view-lod=off; vary --huilian-dom-inspection-height=350 or 6000.\n'
    'Right drag looks, left drag pans, W/A/S/D move and Q/E change altitude.\n'
    'Native DOM is 0.5 m; no finer source is fabricated.\n'
    'The central 8x8 page window still has coarse overview outside it.\n'
    'Resident DOM GPU budget is 341.33 MiB plus temporary decoding/upload/replacement memory.\n'
    'This requires original project assets and is not a standalone distribution.\n'
    'Zhumadian spikes, recursive OSGB LOD and complete UE5 quality remain open.\n', encoding='utf-8')
print('PACKAGED', target)
