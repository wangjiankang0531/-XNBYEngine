"""Freeze the verified native/satellite/terrain combination as the user entry."""
import hashlib
import json
import shutil
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = root/'work/releases/native_osgb_lod_20261010'
target = root/'work/releases/native_osgb_context_20261010'
checks = root/'work/native_context_checks'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


metrics = json.loads((checks/'metrics.json').read_text(encoding='utf-8'))
travel = json.loads((checks/'travel.json').read_text(encoding='utf-8'))
assert metrics['passed'] and len(metrics['views']) == 7
assert travel['passed'] and len(travel['stations']) == 6
final_build = json.loads((root/'work/native_lod_final_build.json').read_text(encoding='utf-8'))
assert sha(source/'showcase_demo.exe') == final_build['exe_sha256']
for filename in ('zhumadian_context.json', 'zhumadian_context_color.json', 'zhumadian_context_fusion.json'):
    doc = json.loads((root/'data/showcase'/filename).read_text(encoding='utf-8'))
    for record in doc.get('artifacts', []) + doc.get('sources', []) + doc.get('inputs', []) + doc.get('geometry_inputs', []):
        assert sha(root/record['path']) == record['sha256'], record['path']
target.mkdir(parents=True, exist_ok=True)
files = []
for path in [source/'showcase_demo.exe', *sorted(source.glob('*.dll'))]:
    shutil.copy2(path, target/path.name)
    files.append({'name': path.name, 'bytes': path.stat().st_size, 'sha256': sha(path)})
assert len(files) == 7
shutil.copy2(checks/'overview_integrated.png', root/'work/zhumadian_integrated_overview.png')
shutil.copy2(checks/'close_integrated.png', root/'work/zhumadian_integrated_close.png')
shutil.copy2(checks/'edge_integrated.png', root/'work/zhumadian_integrated_edge.png')
inputs = ['work/native_context_engine_source_20261010/main.cpp',
          'work/native_context_engine_source_20261010/zhumadian_native_pager.inl',
          'work/native_context_engine_source_20261010/CMakeLists.txt',
          'work/native_context_engine_source_20261010/native_geometry_lod.hpp',
          'tools/verify_zhumadian_native_context.py',
          'tools/package_zhumadian_integrated.py', 'tools/verify_zhumadian_integrated_delivery.py',
          'run_zhumadian_integrated_demo.cmd', 'run_zhumadian_native_lod_demo.cmd',
          'run_zhumadian_geometry_only_comparison.cmd',
          'data/showcase/zhumadian_context.json', 'data/showcase/zhumadian_context_color.json',
          'data/showcase/zhumadian_context_fusion.json', 'data/showcase/zhumadian_native_hierarchy.json',
          'assets/showcase/runtime/zhumadian/context/context_fused.gltf',
          'assets/showcase/runtime/zhumadian/context/context_fused.bin',
          'assets/showcase/runtime/zhumadian/context/context_colormatched.jpg',
          'work/native_context_checks/metrics.json', 'work/native_context_checks/travel.json',
          'work/native_lod_final_build.json', 'work/native_lod_full_suite_build.json',
          'work/native_lod_regression_20261010.log', 'work/native_lod_final_native_20261010.log']
manifest = {'version': target.name, 'runtime_files': files,
    'inputs': [{'path': p, 'sha256': sha(root/p)} for p in inputs],
    'engine_sha256': final_build['exe_sha256'], 'engine_changed': False,
    'profile': 'full native OSGB + actual Sentinel-2 matched satellite + fused display terrain',
    'profile_views_verified': 7, 'profile_travel_stations_verified': 6,
    'compatibility_scope': 'Unchanged engine binary: prior 107 compatibility tests, two final native gates; new combined seven-view and six-station gates.',
    'overview_sha256': sha(checks/'overview_integrated.png'), 'close_sha256': sha(checks/'close_integrated.png'),
    'source_roots': 2346, 'satellite_native_resolution_m': 10, 'context_extent_km': [40, 40],
    'limits': 'Photogrammetry has its original source gaps and reconstruction noise. Satellite is native 10 m. Display terrain is fitted, not surveyed. Visible seams, deeper dense LOD and performance acceptance remain open.',
    'workspace_required': str(root), 'prior_native_manifest_sha256': sha(source/'manifest.json')}
(target/'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
(target/'README.md').write_text(
    '# Integrated Zhumadian stage\n\n'
    'Run run_zhumadian_integrated_demo.cmd or run_zhumadian_native_lod_demo.cmd from the original workspace.\n'
    'Overview starts with actual satellite and display terrain around the complete photogrammetry footprint.\n'
    'F8 cycles overview, oblique, detailed close, plant and exterior edge.\n'
    'Native detail decodes asynchronously in close views; the terrain and satellite remain visible.\n'
    'WASD moves, Q/E changes height, right drag looks, left drag pans, Home resets, Esc exits.\n'
    'The explicitly named run_zhumadian_geometry_only_comparison.cmd retains the old no-background diagnostic.\n'
    'This stage requires the source workspace and decoder dependencies; it is not a standalone installer.\n'
    'Source gaps, native 10 m satellite sampling and visible seams remain known quality limits.\n', encoding='utf-8')
print('INTEGRATED NATIVE / SATELLITE / TERRAIN SNAPSHOT:', target)
