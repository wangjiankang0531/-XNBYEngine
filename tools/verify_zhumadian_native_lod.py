"""Verify actual Vulkan native-frontier refinement with complete base fallback."""
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
    out = Path(args.output_dir).resolve()
    out.mkdir(parents=True, exist_ok=True)
    common = ['--preset=zhumadian', '--zhumadian-detail=full', '--zhumadian-view=close',
              '--width=1280', '--height=720', '--scenario-time=0']
    demo = Path(args.demo).resolve()
    if args.travel:
        run_showcase_process(demo, [*common, '--zhumadian-geometry=native', '--zhumadian-native-tile=141',
            '--zhumadian-native-height=350', '--zhumadian-native-probe', f'--stats={out/"travel.json"}'],
            root, timeout=420, fallback_root=root)
        result = json.loads((out/'travel.json').read_text(encoding='utf-8'))
        assert result['passed'] and len(result['stations']) == 6
        for sample in result['stations']:
            validate(sample)
        assert result['stations'][0]['stale'] > 0
        assert result['stations'][3]['resident'] and any(r['tile'] == 1 for r in result['stations'][3]['resident'])
        print('NATIVE OSGB INTERACTIVE STALE / APPROACH / REFINE / OUTSIDE OLD ROI / RETURN / FAR PASS')
        return

    def capture(name, mode='native', tile=141, height=100, cold=False, mask=False, pixels=720, view='close'):
        image, stats = out/f'{name}.png', out/f'{name}.json'
        options = [*common, f'--zhumadian-view={view}', f'--zhumadian-geometry={mode}', f'--zhumadian-native-tile={tile}',
            f'--zhumadian-native-height={height}', '--frames=1', f'--height={pixels}',
            f'--screenshot={image}', f'--stats={stats}']
        if cold:
            options.append('--zhumadian-geometry-cold')
        if mask:
            options.append('--zhumadian-coverage-mask')
        run_showcase_process(demo, options, root, timeout=220, fallback_root=root)
        return json.loads(stats.read_text(encoding='utf-8'))

    before = capture('before', 'base')
    cold = capture('cold', cold=True)
    after = capture('after')
    near = after['zhumadian_native_lod']
    validate(near)
    assert any(r['tile'] == 141 and r['depth'] >= 5 for r in near['resident'])
    assert all(r['frontier'] > 0 for r in near['resident']) and near['settled'] and not near['pending']
    assert hashlib.sha256((out/'before.png').read_bytes()).digest() == hashlib.sha256((out/'cold.png').read_bytes()).digest()
    assert cold['zhumadian_native_lod']['drawn_child_indices'] == 0
    assert (before['vertices'], before['indices']) == (after['vertices'], after['indices'])
    assert (out/'before.png').read_bytes() != (out/'after.png').read_bytes()
    capture('far_base', 'base', height=6000)
    far = capture('far', height=6000)['zhumadian_native_lod']
    validate(far)
    assert not far['resident'] and not far['pending'] and far['requests'] == 0
    assert (out/'far_base.png').read_bytes() == (out/'far.png').read_bytes()
    capture('overview_base', 'base', view='overview')
    overview = capture('overview', view='overview')['zhumadian_native_lod']
    validate(overview)
    assert not overview['resident'] and overview['requests'] == 0
    assert (out/'overview_base.png').read_bytes() == (out/'overview.png').read_bytes()
    assert (out/'overview.png').read_bytes() != (out/'after.png').read_bytes()
    remote = capture('outside_foreground', tile=1)['zhumadian_native_lod']
    validate(remote)
    assert any(r['tile'] == 1 and r['depth'] >= 3 for r in remote['resident'])
    foreground = json.loads((root/'data/showcase/zhumadian_geometry_foreground.json').read_text(encoding='utf-8'))
    assert 1 not in {r['tile_id'] for r in foreground['replacements']}
    coarse_pixels = capture('viewport_400', height=350, pixels=400)['zhumadian_native_lod']
    fine_pixels = capture('viewport_720', height=350)['zhumadian_native_lod']
    depth = lambda sample: next(r['depth'] for r in sample['resident'] if r['tile'] == 141)
    assert depth(fine_pixels) > depth(coarse_pixels)
    capture('before_mask', 'base', mask=True)
    mask_stats = capture('after_mask', mask=True)['zhumadian_native_lod']
    validate(mask_stats)
    w, h, a = read_png_rgb(out/'before_mask.png')
    _, _, b = read_png_rgb(out/'after_mask.png')
    source_before = sum(rgb[0] > 200 for rgb in a)
    source_after = sum(rgb[0] > 200 for rgb in b)
    assert source_after >= source_before * .98, (source_before, source_after)
    metrics = {'passed': True, 'resident': near['resident'], 'source_pixels': [source_before, source_after],
        'viewport_depths': [depth(coarse_pixels), depth(fine_pixels)],
        'source_base_triangles': 3055617, 'native': near,
        'before_sha256': hashlib.sha256((out/'before.png').read_bytes()).hexdigest(),
        'after_sha256': hashlib.sha256((out/'after.png').read_bytes()).hexdigest(),
        'limit': 'Source-derived tessellation heuristic; 8 roots / 192 MiB native GPU payload / 32 MiB atomic root upload.'}
    (out/'metrics.json').write_text(json.dumps(metrics, indent=2), encoding='utf-8')
    print('NATIVE OSGB RENDER / ATOMIC COLD FALLBACK / FULL BASE / FAR / OUTSIDE ROI / VIEWPORT PASS')


def validate(sample):
    assert sample['catalog_roots'] == 2346 and sample['catalog_source_nodes'] == 237453
    assert sample['payload_bytes'] <= sample['budget_bytes']
    assert sample['peak_concurrent_payload_bytes'] <= sample['budget_bytes']
    assert sample['max_upload_bytes'] <= sample['upload_limit_bytes'] == 32*1024*1024
    assert len(sample['resident']) <= sample['resident_limit'] == 8
    assert sample['drawn_base_indices'] + sample['removed_parent_indices'] == sample['base_indices'] + sample['prefix_indices']
    assert sample['drawn_child_indices'] == sum(r['triangles']*3 for r in sample['resident'])


if __name__ == '__main__':
    main()
