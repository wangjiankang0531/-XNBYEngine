"""Package only a verified native OSGB prototype, retaining prior releases."""
import hashlib
import json
import shutil
from pathlib import Path

root = Path(__file__).resolve().parents[1]
target = root/'work/releases/native_osgb_lod_20261010'
source = root/'build_showcase_render_tests/apps/showcase_demo/Release'
captures = root/'build_showcase_render_tests/apps/showcase_demo/native_lod'
log = root/'work/native_lod_regression_20261010.log'
evidence = log.read_bytes()
assert evidence.count(b'Test Passed.') == 107 and b'Test Failed.' not in evidence
final_log = root/'work/native_lod_final_native_20261010.log'
assert final_log.read_bytes().count(b'Test Passed.') == 2 and b'Test Failed.' not in final_log.read_bytes()
full_build = json.loads((root/'work/native_lod_full_suite_build.json').read_text(encoding='utf-8'))
final_build = json.loads((root/'work/native_lod_final_build.json').read_text(encoding='utf-8'))
metrics = json.loads((captures/'metrics.json').read_text(encoding='utf-8'))
travel = json.loads((captures/'travel.json').read_text(encoding='utf-8'))
assert metrics['passed'] and travel['passed'] and len(travel['stations']) == 6
target.mkdir(parents=True, exist_ok=True)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


assert final_build['exe_sha256'] == sha(source/'showcase_demo.exe')
assert full_build['passed'] == 107 and final_build['passed'] == 2
runtime = []
for path in [source/'showcase_demo.exe', *sorted(source.glob('*.dll'))]:
    dest = target/path.name
    shutil.copy2(path, dest)
    assert sha(dest) == sha(path)
    runtime.append({'name': dest.name, 'sha256': sha(dest), 'bytes': dest.stat().st_size})
assert len(runtime) == 7
for name in ('before', 'after', 'outside_foreground'):
    shutil.copy2(captures/f'{name}.png', root/f'work/native_lod_{name}.png')
inputs = [
    'apps/showcase_demo/main.cpp', 'apps/showcase_demo/zhumadian_native_pager.inl',
    'apps/showcase_demo/CMakeLists.txt', 'tests/CMakeLists.txt',
    'renderer/include/hongtu/renderer/native_geometry_lod.hpp', 'tests/src/renderer/test_native_geometry_lod.cpp',
    'tools/build_zhumadian_native_hierarchy.py', 'tools/convert_zhumadian_native_root.py',
    'tools/verify_zhumadian_native_lod.py', 'tools/test_zhumadian_native_frontier.py',
    'tools/osgb_exporter/main.cpp', 'work/osgb_exporter_build/Release/ht_osgb_exporter.exe',
    'tools/build_zhumadian_stage_asset.py', 'tools/build_zhumadian_full_stage.py',
    'data/showcase/zhumadian_native_hierarchy.json', 'data/showcase/zhumadian_full_stage_manifest.json',
    'assets/showcase/runtime/zhumadian/full/zhumadian_full.bin',
    'assets/showcase/runtime/zhumadian/full/zhumadian_full.gltf',
    'assets/showcase/runtime/zhumadian/full/zhumadian_full_atlas.jpg',
    'run_zhumadian_native_lod_demo.cmd', 'work/native_lod_regression_20261010.log',
    'work/native_lod_full_suite_build.json', 'work/native_lod_final_build.json',
    'work/native_lod_final_native_20261010.log',
    'work/native_lod_cache_recovery_20261010.json', 'work/native_lod_source_quality_20261010.json',
    'tools/verify_zhumadian_native_cache_recovery.py', 'tools/diagnose_zhumadian_native_quality.py',
    'tools/package_zhumadian_native_lod.py', 'tools/verify_zhumadian_native_lod_delivery.py',
    'build_showcase_render_tests/apps/showcase_demo/native_lod/metrics.json',
    'build_showcase_render_tests/apps/showcase_demo/native_lod/travel.json']
manifest = {'version': target.name, 'runtime_files': runtime, 'regression_passed': 107,
    'full_compatibility_suite_exe_sha256': full_build['exe_sha256'], 'final_native_gates_passed': 2,
    'regression_scope': '107 compatibility checks before native-only overview/capture-convergence polish; two final native render/travel gates on shipped binary.',
    'source_root_required': str(root), 'inputs': [{'path': p, 'sha256': sha(root/p)} for p in inputs],
    'screenshot_sha256': sha(root/'work/native_lod_after.png'),
    'scope': 'All 2346 base roots retained; actual descendant references decoded on demand, complete root frontier atomic replacement.',
    'native_gpu_payload_budget_bytes': 192*1024*1024, 'native_root_upload_limit_bytes': 32*1024*1024,
    'limits': 'Eight resident roots. Budget excludes full base/context/other textures and temporary CPU/upload memory. Dense deeper frontiers can retain previous depth. No morphing or UE5-quality completion.',
    'source_hierarchy_nodes': 237453, 'source_reachable_nodes': 235107,
    'source_triangles_always_available': 3055617,
    'runtime_dependency': 'Uses original source plus the local OSGB exporter, OSG plugins and Python/Pillow. This is a workspace stage, not a standalone installer.'}
(target/'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
(target/'README.md').write_text(
    '# Native OSGB view LOD prototype\n\n'
    'Run run_zhumadian_native_lod_demo.cmd from the original project.\n'
    'First reads show the whole coarse base while one worker converts and uploads visible descendants.\n'
    'Compare --zhumadian-geometry=base; change --zhumadian-native-tile=1 or --zhumadian-native-height=350.\n'
    'Right drag looks, left drag pans, WASD moves, Q/E changes height, Home resets.\n'
    'View center gets three request priorities; remaining slots use projected coverage.\n'
    'Eight fine roots / 192 MiB fine geometry+texture / 32 MiB root upload. These are not total VRAM limits.\n'
    'Dense deepest frontiers retain their ready parent; staged subnode uploads and morphing remain open.\n'
    'DOM-only Huilian inspection remains run_huilian_view_lod_demo.cmd.\n', encoding='utf-8')
print('NATIVE OSGB VERIFIED 107 COMPATIBILITY + 2 FINAL NATIVE GATE SNAPSHOT:', target)
