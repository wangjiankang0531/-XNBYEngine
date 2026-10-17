"""Verify and promote the tested 0.8 surface-picking package; retain 0.7 evidence."""
import hashlib
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / 'work/releases/landscape_apps_20261011'
CHECKS = ROOT / 'work/landscape_surface_checks'


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def record(path, base):
    return {'path': path.relative_to(base).as_posix(), 'sha256': digest(path), 'bytes': path.stat().st_size}


def main():
    exe = PACKAGE / 'ht_landscape_editor_0_8.exe'
    exe_hash = digest(exe)
    for name in ['delivery_metrics.json', 'near_metrics.json', 'base_metrics.json', 'far_metrics.json',
                 'styles/metrics.json', 'styles/ui_roundtrip_metrics.json', 'styles/unicode_cli.json']:
        gate = read(CHECKS / name)
        assert gate['passed'] and gate['exe_sha256'] == exe_hash, name
    delivery = read(CHECKS / 'delivery_metrics.json')
    assert delivery['cold_repeats'] >= 3 and len(delivery['runs']) >= 5
    assert delivery['shader_compiler']['uses_packaged_compiler']
    assert delivery['shader_compiler']['sha256'] == digest(PACKAGE / 'runtime/shaderc/glslc.exe')
    travel = read(CHECKS / 'native_travel.json')
    assert travel['passed'] and len(travel['stations']) == 8
    base_bytes = travel['stations'][0]['picking_cpu_bytes']
    depths = []
    for station in travel['stations']:
        assert station['picking_enabled'] and station['base_picking_triangles'] == 3668177
        assert station['settled'] and not station['pending'] and not station['partial_root_drawn']
        assert station['peak_concurrent_payload_bytes'] <= station['budget_bytes'] == 512 * 1048576
        assert station['max_upload_bytes'] <= 32 * 1048576
        assert all(r['picking_triangles'] == r['triangles'] for r in station['resident'])
        assert station['picking_cpu_bytes'] >= base_bytes
        depths.append(next((r['depth'] for r in station['resident'] if r['tile'] == 141), 0))
    assert depths[1] == 4 and depths[2] >= 6 and depths[4] >= 6
    assert depths[5] < depths[4] and depths[6] > depths[5]
    end = travel['stations'][-1]
    assert not end['resident'] and end['payload_bytes'] == 0 and end['picking_cpu_bytes'] == base_bytes
    assert end['staging_cancellations'] > 0
    unit_log = ROOT / 'work/landscape_surface_unit_tests_20261012.log'
    unit_text = unit_log.read_text(encoding='utf-8')
    assert '0 tests failed out of 41' in unit_text and '0 tests failed out of 3' in unit_text
    actual_doc = ROOT / 'work/landscape_apps_checks/ui_final6.json'
    assert read(CHECKS / 'styles/ui_roundtrip.json') == read(actual_doc)
    assert (CHECKS / 'web_roundtrip.jpg').is_file()
    near = read(CHECKS / 'near_metrics.json')
    timings = sorted(s['query_ms'] for s in near['samples'])
    summary = {'passed': True, 'exe_sha256': exe_hash, 'focused_tests': 44,
               'native_travel_seconds': travel['duration_seconds'], 'frames': travel['frames'],
               'center_depths': depths, 'base_picking_cpu_bytes': base_bytes,
               'near_picking_cpu_bytes': near['picking_cpu_bytes'],
               'peak_fine_payload_bytes': end['peak_concurrent_payload_bytes'],
               'max_upload_bytes': end['max_upload_bytes'], 'fine_pick_samples': near['fine_hits'],
               'max_difference_from_coarse_m': near['max_base_difference_m'],
               'max_sample_query_ms': timings[-1], 'cold_starts': delivery['cold_repeats'],
               'web_document_equal': True, 'web_screenshot': 'web_roundtrip.jpg',
               'actual_windows_input_version': '0.6',
               'windows_0_8_input': 'not completed: Computer Use app approval timed out',
               '0_8_scope': '44 focused tests; packaged real frames; independent visible triangle reference; '
                            'UI-saved document reopen; browser import; eight-station travel; cold starts',
               'user_default_document_sha256': digest(PACKAGE / 'projects/驻马店图层.json')}
    (CHECKS / 'release_metrics.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    previous = read(PACKAGE / 'package.json')
    if previous['version'] == '0.7':
        for source, target in [('package.json', 'package_0_7.json'), ('acceptance.md', 'acceptance_0_7.md'),
                               ('README.md', 'README_0_7.md')]:
            assert not (PACKAGE / target).exists(), target
            shutil.copy2(PACKAGE / source, PACKAGE / target)
    else:
        assert previous['version'] == '0.8'
    assert digest(PACKAGE / 'ht_landscape_editor_0_7.exe') == '98b89bb4d886fef0818911d1f238bce65d6b91bafd5568ef34603a5cbf90be6f'
    reports = PACKAGE / 'verification_0_8'
    reports.mkdir(exist_ok=True)
    names = ['release_metrics.json', 'delivery_metrics.json', 'near_metrics.json', 'base_metrics.json',
             'far_metrics.json', 'native_travel.json', 'near.json', 'base.json', 'far.json',
             'near.png', 'base.png', 'far.png', 'web_roundtrip.jpg', 'styles/metrics.json',
             'styles/ui_roundtrip_metrics.json', 'styles/unicode_cli.json', 'styles/ui_roundtrip.json',
             'styles/ui_saved.png']
    for name in names:
        target = reports / name
        target.parent.mkdir(exist_ok=True)
        shutil.copy2(CHECKS / name, target)
    shutil.copy2(unit_log, reports / 'unit_tests.log')
    for n in ['00_base.log', '01_base.log', '02_base.log', '03_far.log', '04_near.log']:
        shutil.copy2(CHECKS / n, reports / n)
    shutil.copy2(ROOT / 'docs/project/landscape_apps_acceptance_20261012.md', PACKAGE / 'acceptance.md')
    shutil.copy2(ROOT / 'docs/project/landscape_apps_readme_0_8.md', PACKAGE / 'README.md')
    for file in [ROOT / 'run_landscape_editor.cmd', PACKAGE / 'run_editor.cmd']:
        file.write_bytes(file.read_bytes().replace(b'ht_landscape_editor_0_7.exe', b'ht_landscape_editor_0_8.exe'))
    files = [exe, PACKAGE / 'run_editor.cmd', PACKAGE / 'run_web.cmd', PACKAGE / 'acceptance.md',
             PACKAGE / 'README.md', *sorted(PACKAGE.glob('*.dll')),
             *sorted((PACKAGE / 'runtime/shaderc').glob('*')),
             PACKAGE / 'data/showcase/zhumadian_native_hierarchy.json',
             PACKAGE / 'data/showcase/zhumadian_context_fusion.json',
             PACKAGE / 'apps/landscape_web/dist/viewer.js']
    manifest = dict(previous, version='0.8', prepared=True, verified=True, verified_at='2026-10-12',
                    active_executable=exe.name, files=[record(p, PACKAGE) for p in files],
                    evidence=[record(p, PACKAGE) for p in sorted(reports.rglob('*')) if p.is_file()],
                    portable_shader_compiler=True, surface_pick='BVH of exact displayed complete frontiers',
                    verification_scope=summary['0_8_scope'], previous_manifest='package_0_7.json')
    (PACKAGE / 'package.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    print('VERIFIED 0.8 SURFACE DELIVERY PROMOTED', exe_hash)


if __name__ == '__main__':
    main()
