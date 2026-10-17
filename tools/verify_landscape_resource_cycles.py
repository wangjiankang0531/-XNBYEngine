"""Verify actual Vulkan descriptor reuse and VMA allocation stability across LOD travel."""
import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run', action='store_true')
    parser.add_argument('--cycles', type=int, default=6)
    parser.add_argument('--exe', default='ht_landscape_editor_0_9.exe')
    args = parser.parse_args()
    assert 2 <= args.cycles <= 8
    package = ROOT / 'work/releases/landscape_apps_20261011'
    exe = package / args.exe
    out = ROOT / 'work/landscape_resource_checks'
    out.mkdir(exist_ok=True)
    path = out / 'travel.json'
    if args.run:
        env = os.environ.copy()
        env.pop('HT_NATIVE_PYTHON', None)
        env['USERPROFILE'] = str(out / 'no_global_python_profile')
        command = [str(exe), '--scene=zhumadian', '--preset=zhumadian',
                   '--zhumadian-view=close', '--zhumadian-native-height=100',
                   '--zhumadian-native-probe', f'--zhumadian-native-probe-cycles={args.cycles}',
                   '--width=1280', '--height=720', f'--stats={path}']
        # Every run must succeed on its first attempt. The renderer produces the
        # stats only after rendering the actual station views and GPU submissions.
        with (out / 'travel.log').open('w', encoding='utf-8') as log:
            subprocess.run(command, cwd=out, env=env, check=True,
                           timeout=900 * args.cycles + 120, stdout=log, stderr=subprocess.STDOUT)
    data = json.loads(path.read_text(encoding='utf-8'))
    assert data['passed'] and data['cycles'] == data['requested_cycles'] == args.cycles
    stations = data['stations']
    assert len(stations) == 8 * args.cycles
    ends, depths = [], []
    previous_allocations = 0
    previous_frees = 0
    for cycle in range(args.cycles):
        current = stations[cycle * 8:(cycle + 1) * 8]
        for stage, station in enumerate(current):
            assert station['cycle'] == cycle and station['stage'] == stage
            assert station['settled'] and not station['pending'] and not station['partial_root_drawn']
            assert station['peak_concurrent_payload_bytes'] <= station['budget_bytes'] == 512 * 1048576
            assert station['max_upload_bytes'] <= 32 * 1048576
            assert all(r['picking_triangles'] == r['triangles'] for r in station['resident'])
            resource = station['resources']
            assert resource['descriptor_stats_available'] and resource['descriptor_free_failures'] == 0
            assert resource['descriptor_allocated_sets'] - resource['descriptor_freed_sets'] == resource['descriptor_live_sets']
            assert resource['descriptor_allocated_sets'] >= previous_allocations
            assert resource['descriptor_freed_sets'] >= previous_frees
            previous_allocations, previous_frees = resource['descriptor_allocated_sets'], resource['descriptor_freed_sets']
            assert resource['allocator_allocation_bytes'] > 0 and resource['allocator_allocation_count'] > 0
        tier = lambda stage: next((r['depth'] for r in current[stage]['resident'] if r['tile'] == 141), 0)
        depth = [tier(stage) for stage in range(8)]
        # Known parent quality persists across cycles. A revisited 350m view may
        # already select depth 5; inspect actual approach/retreat improvement.
        assert depth[2] >= 6 and depth[2] > depth[1]
        assert depth[4] == depth[2] and depth[5] < depth[4] and depth[6] == depth[2]
        depths.append(depth)
        end = current[-1]
        assert not end['resident'] and end['payload_bytes'] == end['staging_payload_bytes'] == 0
        assert end['picking_cpu_bytes'] == current[0]['picking_cpu_bytes']
        assert end['resources']['descriptor_freed_sets'] > current[0]['resources']['descriptor_freed_sets']
        ends.append(end['resources'])
    # Compare exact live objects/allocations, not a made-up percentage of the
    # global driver heap. VMA block capacity may retain reusable warm allocations.
    for end in ends[1:]:
        for key in ['descriptor_live_sets', 'descriptor_pool_count',
                    'allocator_allocation_count', 'allocator_allocation_bytes']:
            assert end[key] == ends[0][key], (key, ends)
    summary = {'passed': True, 'exe_sha256': hashlib.sha256(exe.read_bytes()).hexdigest(),
               'cycles': args.cycles, 'stations': len(stations), 'frames': data['frames'],
               'duration_seconds': data['duration_seconds'], 'center_depths': depths,
               'far_resources': ends, 'peak_payload_bytes': stations[-1]['peak_concurrent_payload_bytes'],
               'max_upload_bytes': stations[-1]['max_upload_bytes'],
               'scope': 'actual repeated native route; exact descriptor/VMA live allocation plateau; '
                        'driver usage recorded but shared with other applications; no total-memory/endurance claim'}
    (out / 'metrics.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print('ACTUAL NATIVE LOD / DESCRIPTOR RELEASE / REPEATED VMA PLATEAU PASS', args.cycles)


if __name__ == '__main__':
    main()
