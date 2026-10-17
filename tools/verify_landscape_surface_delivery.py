"""Run packaged Vulkan frames and independently check the displayed pick surfaces."""
import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cold-repeats', type=int, default=3)
    parser.add_argument('--exe', default='ht_landscape_editor_0_8.exe')
    parser.add_argument('--output', default='work/landscape_surface_checks')
    args = parser.parse_args()
    package = ROOT / 'work/releases/landscape_apps_20261011'
    out = ROOT / args.output
    out.mkdir(exist_ok=True)
    exe = package / args.exe
    env = os.environ.copy()
    env.pop('HT_NATIVE_PYTHON', None)
    env['USERPROFILE'] = str(out / 'no_global_python_profile')
    shader_temp = out / '着色器 临时目录'
    shader_temp.mkdir(exist_ok=True)
    env['TMP'] = env['TEMP'] = str(shader_temp)
    records = []
    for view in [*(['base'] * args.cold_repeats), 'far', 'near']:
        command = [str(exe), '--width=1280', '--height=720', '--scenario-time=0',
                   f'--layers={ROOT / "work/landscape_apps_checks/ui_final6.json"}',
                   '--zhumadian-view=' + ('overview' if view == 'far' else 'close'),
                   '--landscape-pick-probe', f'--screenshot={out / (view + ".png")}',
                   f'--stats={out / (view + ".json")}']
        if view == 'base':
            command.append('--zhumadian-geometry-cold')
        log = out / f'{len(records):02d}_{view}.log'
        with log.open('w', encoding='utf-8') as stream:
            # No automatic retry: an unexpected renderer failure is a failed gate.
            subprocess.run(command, cwd=out, env=env, check=True, timeout=700,
                           stdout=stream, stderr=subprocess.STDOUT)
        subprocess.run([sys.executable, str(ROOT / 'tools/verify_landscape_surface_pick.py'),
                        '--view=' + view, '--exe=' + args.exe, '--output=' + args.output],
                       cwd=out, env=env, check=True, timeout=180)
        stats = json.loads((out / (view + '.json')).read_text(encoding='utf-8'))
        assert Path(stats['shader_compiler']).resolve() == (package / 'runtime/shaderc/glslc.exe').resolve()
        records.append({'view': view, 'passed': True, 'log': str(log.relative_to(ROOT))})
    summary = {'passed': True, 'exe_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
               'runs': records, 'portable_cwd': str(out), 'global_python_disabled': True,
               'reference': 'independent original glTF triangles; full visible frontiers',
               'cold_repeats': args.cold_repeats}
    compiler = package / 'runtime/shaderc/glslc.exe'
    summary['shader_compiler'] = {'path': str(compiler.relative_to(package)),
                                  'sha256': hashlib.sha256(compiler.read_bytes()).hexdigest(),
                                  'uses_packaged_compiler': True}
    (out / 'delivery_metrics.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print('PACKAGED SURFACE PICK / COLD BASE / FAR / FINE DELIVERY PASS')


if __name__ == '__main__':
    main()
