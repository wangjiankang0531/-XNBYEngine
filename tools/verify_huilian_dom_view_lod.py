#!/usr/bin/env python3
"""Actual Vulkan view-driven DOM captures and interactive cache/refinement audit."""
import argparse
import hashlib
import json
from pathlib import Path
from run_showcase_demo import run_showcase_process
from verify_badaling_demo import read_png_rgb


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--demo', required=True)
    parser.add_argument('--output-dir', required=True)
    parser.add_argument('--travel', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    demo = Path(args.demo).resolve()
    common = ['--preset=huilian', '--huilian-view=dom-inspect']
    if args.travel:
        path = output/'travel.json'
        run_showcase_process(demo, [*common, '--huilian-dom-view-lod-probe',
                                   f'--stats={path}', '--width=1440', '--height=900'],
                             root, timeout=90, fallback_root=root)
        result = json.loads(path.read_text(encoding='utf-8'))
        assert result['passed'] and len(result['stations']) == 4
        assert result['stations'][0]['lod'] > 0
        assert [s['lod'] for s in result['stations'][1:]] == [0, 0, 0]
        assert result['stations'][1]['page'] != result['stations'][2]['page']
        assert result['stations'][3]['cache_hits'] > result['stations'][2]['cache_hits']
        for station in result['stations']:
            x, y, _ = station['focus']
            a, b, c, d = station['bounds']
            assert a < x < c and b < y < d
            assert station['cache_consistent'] and station['grid'] == 8
        assert result['gpu_budget_bytes'] == 357913936
        print('DOM VIEW LOD REAL INTERACTIVE COARSE / FINE / PAN / CACHE RETURN PASS')
        return

    def capture(name, enabled, height=350, pixels=900):
        image, stats = output/f'{name}.png', output/f'{name}.json'
        run_showcase_process(demo, [*common, f'--huilian-dom-view-lod={enabled}',
            f'--huilian-dom-inspection-height={height}', '--scenario-time=0', '--frames=3',
            '--width=1440', f'--height={pixels}', f'--screenshot={image}', f'--stats={stats}'],
            root, timeout=90, fallback_root=root)
        return json.loads(stats.read_text(encoding='utf-8'))

    old = capture('before', 'off')
    near = capture('after', 'on')
    far = capture('far', 'on', 6000)
    low_resolution = capture('low_resolution', 'on', 350, 400)
    assert old['terrain_huilian_dom_initial_lod'] == 1
    assert near['terrain_huilian_dom_initial_lod'] == 0
    assert far['terrain_huilian_dom_initial_lod'] == 2
    assert low_resolution['terrain_huilian_dom_initial_lod'] == 1
    assert near['terrain_huilian_dom_view_hit']
    assert abs(near['terrain_huilian_dom_view_focus'][1] - 900) < 0.01
    assert near['terrain_huilian_dom_resident_working_set_pages'] == [8, 8]
    assert near['terrain_huilian_dom_detail_resolution_m'] == [0.5, 0.5]
    assert near['terrain_huilian_dom_resident_gpu_budget_bytes'] == 357913936
    assert (old['vertices'], old['indices']) == (near['vertices'], near['indices'])
    w, h, before = read_png_rgb(output/'before.png')
    w2, h2, after = read_png_rgb(output/'after.png')
    assert (w, h) == (w2, h2) == (1440, 900)
    assert before[:w*80] == after[:w*80]

    def gradient(data, bounds):
        x0, y0, x1, y1 = bounds
        total, count = 0, 0
        for y in range(y0, y1):
            for x in range(x0, x1):
                i = y*w+x
                total += sum(abs(data[i][c] - data[i+1][c]) for c in range(3))
                count += 3
        return total/count

    metrics = {}
    for name, bounds in [('center', (480, 340, 960, 580)), ('foreground', (200, 650, 1200, 850))]:
        a, b = gradient(before, bounds), gradient(after, bounds)
        # The old camera-foot window already sharpened part of the foreground.
        # Require strong improvement at the previously uncovered center, and no
        # loss at that previously covered foreground rather than the same gain.
        assert b > a * (1.25 if name == 'center' else 1.0), (name, a, b)
        metrics[name] = {'before_gradient': a, 'after_gradient': b, 'ratio': b/a}
    hashes = {p.stem: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.glob('*.png')}
    (output/'metrics.json').write_text(json.dumps({'metrics': metrics, 'image_sha256': hashes}, indent=2), encoding='utf-8')
    print('DOM VIEW LOD ACTUAL PIXEL / SOURCE RESOLUTION / CAMERA / VIEWPORT PASS', metrics)


if __name__ == '__main__':
    main()
