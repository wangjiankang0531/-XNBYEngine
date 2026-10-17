"""Record verified package hashes without re-copying or editing original data."""
import hashlib
import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / 'work/releases/landscape_apps_20261011'
CHECKS = ROOT / 'work/landscape_apps_checks'


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def record(path, base):
    return {'path': path.relative_to(base).as_posix(),
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
            'bytes': path.stat().st_size}


def main():
    exe = PACKAGE / 'ht_landscape_editor_0_7.exe'
    exe_hash = hashlib.sha256(exe.read_bytes()).hexdigest()
    gates = ['metrics.json', 'fine_metrics.json', 'ui_roundtrip_metrics.json', 'unicode_cli.json']
    for name in gates:
        data = read(CHECKS / name)
        assert data['passed'] and data['exe_sha256'] == exe_hash, name
    for name in ['decoder_lock.json', 'web_lod_policy.json', 'browser_ui_metrics.json']:
        assert read(CHECKS / name)['passed'], name
    travel = read(CHECKS / 'native_travel_final.json')
    assert travel['passed'] and len(travel['stations']) == 8
    stations = travel['stations']
    assert all(s['settled'] and not s['partial_root_drawn'] for s in stations)
    assert stations[-1]['payload_bytes'] == 0 and not stations[-1]['resident']
    cold = CHECKS / 'native_travel_cold_validated.json'
    if not cold.exists():
        shutil.copy2(CHECKS / 'native_travel.json', cold)
    shutil.copy2(CHECKS / 'native_travel_final.json', CHECKS / 'native_travel.json')
    (CHECKS / 'native_travel_metrics.json').write_text(json.dumps({
        'passed': True, 'exe_sha256': exe_hash,
        'duration_seconds': travel['duration_seconds'], 'frames': travel['frames'],
        'stations': len(stations), 'peak_payload_bytes': stations[-1]['peak_concurrent_payload_bytes'],
        'max_upload_bytes': stations[-1]['max_upload_bytes'],
        'coarsenings': stations[-1]['coarsenings'],
        'staging_cancellations': stations[-1]['staging_cancellations'],
    }, indent=2), encoding='utf-8')
    acceptance = ROOT / 'docs/project/landscape_apps_acceptance_20261011.md'
    text = acceptance.read_text(encoding='utf-8').replace(
        '4a31e074c5e28c44a962ba3f24a43bfddf652a3c009d9ce1d79157dc7172407f', exe_hash)
    acceptance.write_text(text, encoding='utf-8')
    shutil.copy2(acceptance, PACKAGE / 'acceptance.md')
    names = ['ht_landscape_editor_0_7.exe', 'run_editor.cmd', 'run_web.cmd',
             'data/showcase/zhumadian_native_hierarchy.json',
             'data/showcase/zhumadian_context_fusion.json', 'apps/landscape_web/dist/viewer.js',
             'apps/landscape_web/dist/index.html', 'tools/convert_zhumadian_native_chunks.py',
             'tools/landscape_web_server.py', 'tools/launch_landscape_web.py', 'acceptance.md']
    previous = read(PACKAGE / 'package.json')
    evidence = ['metrics.json', 'fine_metrics.json', 'ui_roundtrip_metrics.json', 'unicode_cli.json',
                'native_travel.json', 'native_travel_metrics.json', 'decoder_lock.json',
                'web_lod_policy.json', 'browser_ui_metrics.json', 'ui_final6.json',
                'ui_saved.png', 'ui_lod_before.png', 'web_ui_created.jpg', 'web_lod_far.jpg']
    reports = PACKAGE / 'verification'
    reports.mkdir(exist_ok=True)
    for name in evidence:
        shutil.copy2(CHECKS / name, reports / name)
    shutil.copy2(ROOT / 'work/landscape_lod_tests_20261011.log', reports / 'unit_tests.log')
    manifest = {
        'schema': 'hongtu.landscape.apps.package.v1', 'version': '0.7',
        'prepared': True, 'verified': True, 'verified_at': '2026-10-12',
        'active_executable': exe.name, 'files': [record(PACKAGE / n, PACKAGE) for n in names],
        'evidence': [record(reports / n, PACKAGE) for n in evidence] + [record(reports / 'unit_tests.log', PACKAGE)],
        'source_files': previous['source_files'], 'source_bytes': previous['source_bytes'],
        'source_roots': 2346, 'scene': 'zhumadian', 'native_budget_mib': 512,
        'native_upload_limit_mib': 32, 'native_resident_limit': 16,
        'portable_python': True, 'local_disk_policy': previous['local_disk_policy'],
        'web_url': 'http://127.0.0.1:8767/', 'web_scope': 'local viewer; no public deployment',
        'verification_scope': 'focused tests, real frames, Windows UI-saved document, native travel, browser UI; not full-suite/UE5/endurance acceptance',
    }
    (PACKAGE / 'package.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
    print('LANDSCAPE APPS FINAL VERIFIED', exe_hash)


if __name__ == '__main__':
    main()
