#!/usr/bin/env python3
"""Verify real-frame seam reduction, source coverage and source-color preservation."""
import argparse
import hashlib
import json
import statistics
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
    variants = {
        'source': [], 'matched': ['--zhumadian-context-color=matched'],
        'source_mask': ['--zhumadian-coverage-mask'],
        'matched_mask': ['--zhumadian-context-color=matched', '--zhumadian-coverage-mask'],
        'overview_source': ['--zhumadian-view=overview'],
        'overview_matched': ['--zhumadian-view=overview', '--zhumadian-context-color=matched'],
    }
    images, stats, hashes = {}, {}, {}
    for name, extra in variants.items():
        image, telemetry = output/f'{name}.png', output/f'{name}.json'
        image.unlink(missing_ok=True); telemetry.unlink(missing_ok=True)
        run_showcase_process(Path(args.demo).resolve(), [
            '--preset=zhumadian', '--zhumadian-detail=full', '--zhumadian-view=plant',
            '--zhumadian-geometry=foreground', '--zhumadian-context', '--scenario-time=0',
            '--frames=3', '--width=1440', '--height=900',
            f'--screenshot={image}', f'--stats={telemetry}', *extra,
        ], root, timeout=100, fallback_root=root)
        w, h, images[name] = read_png_rgb(image)
        assert (w, h) == (1440, 900)
        stats[name] = json.loads(telemetry.read_text(encoding='utf-8'))
        hashes[name] = hashlib.sha256(image.read_bytes()).hexdigest()
        assert stats[name]['zhumadian_stage_rendered_root_count'] == 2346
        assert stats[name]['zhumadian_context_color_matched'] == ('matched' in name)
        assert stats[name]['vertices'] == 15974513 and stats[name]['indices'] == 16854885
    assert hashes['source'] == 'b60c4555626268fc73d4942abb82d7cdd230355e68f6ac1b272ed1525ef57992'
    assert hashes['overview_source'] == 'aae64f0726eddfc439e859d665fd6c06f9339f554a1698ad4a84732ec63e4a73'
    assert hashes['source_mask'] == hashes['matched_mask']
    mask = images['source_mask']
    source, matched = images['source'], images['matched']
    preserved = sum(a == b for a, b, m in zip(source, matched, mask) if m == (255, 255, 255))
    source_count = sum(m == (255, 255, 255) for m in mask)
    assert preserved == source_count, 'Color correction changed opaque source equipment pixels'
    seams = [[], []]
    for i, m in enumerate(mask):
        if m != (255, 255, 255):
            continue
        y, x = divmod(i, 1440)
        neighbors = [yy*1440+xx for yy, xx in ((y, x-4), (y, x+4), (y-4, x), (y+4, x))
                     if 0 <= yy < 900 and 0 <= xx < 1440 and mask[yy*1440+xx] == (0, 0, 0)]
        if not neighbors:
            continue
        for values, pixels in zip(seams, (source, matched)):
            values.append(min(sum(abs(a-b) for a, b in zip(pixels[i], pixels[j]))/3 for j in neighbors))
    before, after = (statistics.mean(values) for values in seams)
    assert len(seams[0]) > 1000
    assert after < before, ('Actual-frame source/context seam did not improve', before, after)
    result = {'image_sha256': hashes, 'source_opaque_pixels': source_count,
              'unchanged_source_opaque_pixels': preserved, 'boundary_samples': len(seams[0]),
              'mean_boundary_rgb_delta_before': before, 'mean_boundary_rgb_delta_after': after}
    (output/'metrics.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print('CONTEXT COLOR / SOURCE PRESERVATION PASS', result)


if __name__ == '__main__':
    main()
