#!/usr/bin/env python3
"""Package only after the complete regression evidence has passed."""
import hashlib
import json
import shutil
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = root/'build_showcase_render_tests/apps/showcase_demo/Release'
target = root/'work/releases/mesh_sun_shadow_20261010'
evidence = root/'work/mesh_sun_shadow_regression_20261010.log'
log = evidence.read_bytes()
assert log.count(b'Test Passed.') == 95 and b'Test Failed.' not in log
assert b'ShowcaseDemo_MeshSunShadowsInteractive' in log and b'ShowcaseDemo_HuilianDomStreamingEndurance' in log
target.mkdir(parents=True, exist_ok=True)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


files = []
for path in [source/'showcase_demo.exe', *sorted(source.glob('*.dll'))]:
    dest = target/path.name
    shutil.copy2(path, dest)
    assert sha(path) == sha(dest)
    files.append({'name': dest.name, 'sha256': sha(dest), 'bytes': dest.stat().st_size})
assert len(files) == 7
image = root/'build_showcase_render_tests/mesh_sun_shadows/valve_mesh.png'
shutil.copy2(image, root/'work/mesh_sun_shadow_delivery.png')
shutil.copy2(root/'build_showcase_render_tests/mesh_sun_shadows/valve_off.png', root/'work/mesh_sun_shadow_before.png')
sources = [
    'apps/showcase_demo/main.cpp', 'apps/showcase_demo/mesh_sun_shadow.h', 'apps/showcase_demo/CMakeLists.txt',
    'tools/verify_mesh_sun_shadows.py', 'data/showcase/industrial_park.scene.json',
    'data/showcase/huilian_equipment_material_manifest.json',
    'assets/showcase/runtime/huilian_wz_check_valve_runtime.glb',
    'assets/showcase/runtime/huilian_production_water_pump_p0401_runtime.glb',
    'build_showcase_render_tests/mesh_sun_shadows/metrics.json',
    'run_mesh_shadow_demo.cmd', 'work/mesh_sun_shadow_regression_20261010.log',
]
manifest = {'version': target.name, 'source_root_required': str(root), 'runtime_files': files,
            'screenshot_sha256': sha(image), 'regression': {'passed': 95, 'total': 95},
            'inputs': [{'path': name, 'sha256': sha(root/name)} for name in sources],
            'scope': 'single local 2048-square sun map, actual opaque geometry, 3x3 PCF, no global cascades',
            'photo_policy': 'baked photogrammetry stays unchanged and submits zero shadow geometry'}
(target/'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
(target/'README.md').write_text(
    '# Local mesh sun-shadow prototype\n\n'
    'Run run_mesh_shadow_demo.cmd from the original project.\n'
    'Use --mesh-sun-shadows=off for the same-camera previous analytic-shadow comparison.\n'
    'Right drag looks around; left drag pans. Arrow keys change the sun direction live.\n'
    'The map follows the selected preset focus, with 70..700 m half extent.\n'
    'This is not cascaded shadows for a complete 10 km park, point-light shadows or final UE5 quality.\n'
    'The package requires the original project assets/data; it is not a standalone distribution.\n', encoding='utf-8')
print('Packaged local mesh shadow runtime:', target)
