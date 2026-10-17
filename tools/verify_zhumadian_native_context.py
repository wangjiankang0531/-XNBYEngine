"""Prove satellite/terrain and native detail coexist in actual Vulkan views and travel."""
import argparse
import hashlib
import json
from pathlib import Path

from run_showcase_demo import run_showcase_process
from verify_badaling_demo import read_png_rgb
from verify_zhumadian_native_lod import validate

CONTEXT = ['--zhumadian-context', '--zhumadian-context-color=matched', '--zhumadian-context-geometry=fused']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--demo', required=True)
    parser.add_argument('--output-dir', required=True)
    parser.add_argument('--travel', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    out = Path(args.output_dir).resolve()
    out.mkdir(parents=True, exist_ok=True)
    demo = Path(args.demo).resolve()
    common = ['--preset=zhumadian', '--zhumadian-detail=full', '--zhumadian-geometry=native',
              '--zhumadian-native-height=100', '--scenario-time=0', '--width=1440', '--height=900']
    if args.travel:
        run_showcase_process(demo, [*common, *CONTEXT, '--zhumadian-view=close', '--zhumadian-native-tile=141',
            '--zhumadian-native-height=350', '--zhumadian-native-probe', f'--stats={out/"travel.json"}'],
            root, timeout=420, fallback_root=root)
        result = json.loads((out/'travel.json').read_text(encoding='utf-8'))
        assert result['passed'] and len(result['stations']) == 6
        for sample in result['stations']:
            validate(sample)
            assert sample['prefix_indices'] == 663552+1837680
        assert result['stations'][0]['stale'] > 0
        print('NATIVE + SATELLITE/FUSED TERRAIN SIX-STATION INTERACTIVE TRAVEL PASS')
        return

    metrics = {}
    for label, view, tile in [('overview', 'overview', 141), ('oblique', 'oblique', 141),
                              ('close', 'close', 141), ('remote', 'close', 1), ('plant', 'plant', 141),
                              ('edge', 'edge', 141), ('boundary', 'boundary', 141)]:
        frames, hashes = {}, {}
        # Boundary and exterior edge are mandatory context-only stress views.
        # Compare the previous coarse underlay in both, then the fused underlay.
        for context in (False, True):
            for mask in (False, True):
                name = f'{label}_{"integrated" if context else "before"}'+('_mask' if mask else '')
                image, telemetry = out/f'{name}.png', out/f'{name}.json'
                options = [*common, f'--zhumadian-view={view}', f'--zhumadian-native-tile={tile}',
                           '--frames=1', f'--screenshot={image}', f'--stats={telemetry}']
                if context:
                    options.extend(CONTEXT)
                elif label in ('boundary', 'edge'):
                    options.extend(['--zhumadian-context', '--zhumadian-context-color=matched',
                                    '--zhumadian-context-geometry=coarse'])
                if mask:
                    options.append('--zhumadian-coverage-mask')
                run_showcase_process(demo, options, root, timeout=220, fallback_root=root)
                stats = json.loads(telemetry.read_text(encoding='utf-8'))
                assert stats['zhumadian_stage_rendered_root_count'] == 2346
                native = stats['zhumadian_native_lod']
                validate(native)
                assert native['settled'] and not native['pending']
                if context:
                    assert stats['zhumadian_context_active'] and stats['zhumadian_context_fused']
                    assert stats['zhumadian_context_color_matched']
                    assert stats['zhumadian_context']['imagery']['native_resolution_m'] == 10
                    assert native['prefix_indices'] == 663552+1837680
                    assert (stats['vertices'], stats['indices']) == (9584599, 11668083)
                    if label == 'close':
                        assert any(r['tile'] == 141 and r['depth'] >= 5 for r in native['resident'])
                    if label == 'remote':
                        assert any(r['tile'] == 1 and r['depth'] >= 3 for r in native['resident'])
                w, h, pixels = read_png_rgb(image)
                assert (w, h) == (1440, 900)
                frames[(context, mask)] = pixels
                hashes[name] = hashlib.sha256(image.read_bytes()).hexdigest()
        old_mask, new_mask = frames[(False, True)], frames[(True, True)]
        before, after = frames[(False, False)], frames[(True, False)]
        source = sum(a == (255, 255, 255) for a in old_mask)
        lost_opaque = sum(a == (255, 255, 255) and b != (255, 255, 255) for a, b in zip(old_mask, new_mask))
        preserved = sum(a == b for a, b, m in zip(before, after, old_mask) if m == (255, 255, 255))
        changed = sum(a != b for a, b in zip(before, after))
        assert source > 1000 and lost_opaque == 0, (label, source, lost_opaque)
        assert preserved == source, (label, 'background changed opaque native source', source-preserved)
        # At the 100 m close view the real source fills the frame. An underlay
        # must preserve that image rather than become visible over the rooftops.
        if source < len(old_mask)*.99:
            assert changed > 1000, (label, 'background absent despite visible source gaps')
        metrics[label] = {'source_opaque_pixels': source, 'lost_source_opaque_pixels': lost_opaque,
                         'preserved_source_opaque_pixels': preserved, 'changed_background_pixels': changed,
                         'image_sha256': hashes}
        print('NATIVE + CONTEXT VIEW PASS:', label, source, changed, flush=True)
    result = {'passed': True, 'views': metrics, 'source_roots': 2346,
              'background': 'Actual local Sentinel-2 native 10 m / Skadi HGT display-fitted terrain',
              'limit': 'Combined coverage/opacity checks do not establish seamless edges or surveyed heights.'}
    (out/'metrics.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print('NATIVE + SATELLITE/FUSED TERRAIN SEVEN-VIEW COVERAGE AND SOURCE PRESERVATION PASS')


if __name__ == '__main__':
    main()
