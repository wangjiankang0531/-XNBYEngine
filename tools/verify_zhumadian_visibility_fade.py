#!/usr/bin/env python3
"""Compare real distance/visibility captures and actual shader fade endpoints."""
import argparse
import json
from pathlib import Path

from run_showcase_demo import run_showcase_process
from verify_badaling_demo import read_png_rgb


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--demo', required=True)
    parser.add_argument('--output-dir', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    images, telemetry = {}, {}
    variants = {
        'base': ['--zhumadian-textures=base', '--scenario-time=0'],
        'distance': ['--zhumadian-page-selection=distance', '--scenario-time=0'],
        'visible': ['--zhumadian-page-selection=visible', '--scenario-time=0'],
        'fade_start': ['--zhumadian-texture-fade-probe', '--scenario-time=0'],
        'fade_mid': ['--zhumadian-texture-fade-probe', '--scenario-time=0.175'],
        'fade_end': ['--zhumadian-texture-fade-probe', '--scenario-time=0.35'],
    }
    for name, extra in variants.items():
        image, stats = output/f'{name}.png', output/f'{name}.json'
        image.unlink(missing_ok=True)
        stats.unlink(missing_ok=True)
        command = ['--scene=industrial_park', '--preset=zhumadian', '--zhumadian-detail=full',
                   '--zhumadian-view=plant', '--frames=3', '--width=1440', '--height=900',
                   f'--screenshot={image}', f'--stats={stats}', *extra]
        run_showcase_process(Path(args.demo).resolve(), command, root, timeout=90, fallback_root=root)
        w, h, pixels = read_png_rgb(image)
        assert (w, h) == (1440, 900)
        images[name] = pixels[w*400:]
        telemetry[name] = json.loads(stats.read_text(encoding='utf-8'))
        assert telemetry[name]['zhumadian_stage_rendered_root_count'] == 2346
        assert telemetry[name]['zhumadian_texture_resident_pages'] <= 4
    def difference(a, b):
        return sum(abs(x-y) for p, q in zip(images[a], images[b]) for x, y in zip(p, q))
    def changed(name):
        return sum(max(abs(a-b) for a, b in zip(p, q)) > 3 for p, q in zip(images['base'], images[name]))
    old, new = changed('distance'), changed('visible')
    assert new > old, (old, new)
    assert difference('base', 'fade_start') / (len(images['base'])*3) < 0.02
    assert difference('visible', 'fade_end') / (len(images['visible'])*3) < 0.02
    progress = difference('base', 'fade_mid') / difference('base', 'visible')
    assert 0.35 < progress < 0.75, progress
    assert telemetry['distance']['zhumadian_texture_page_selection'] == 'distance'
    assert telemetry['visible']['zhumadian_texture_page_selection'] == 'projected_visibility'
    metrics = {'distance_changed_foreground_pixels': old, 'visible_changed_foreground_pixels': new,
               'visible_to_distance_detail_coverage_ratio': new/old, 'fade_mid_difference_fraction': progress}
    (output/'metrics.json').write_text(json.dumps(metrics, indent=2), encoding='utf-8')
    print('VISIBILITY / LINEAR-LIGHT FADE PASS:', metrics)


if __name__ == '__main__':
    main()
