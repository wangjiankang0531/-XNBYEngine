#!/usr/bin/env python3
"""Verify real native textures, camera-driven paging and same-camera detail."""
import argparse
import json
from pathlib import Path

from run_showcase_demo import run_showcase_process
from verify_badaling_demo import read_png_rgb


def edge_energy(pixels, width, height):
    return sum(sum(abs(pixels[i][c]-pixels[i+1][c]) + abs(pixels[i][c]-pixels[i+width][c])
                   for c in range(3))
               for i in range(width*400, width*(height-1)) if i % width < width-1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--demo', required=True)
    parser.add_argument('--output-dir', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    captures, stats = {}, {}
    for mode in ('base', 'paged'):
        image = output/f'plant_{mode}.png'
        telemetry = output/f'plant_{mode}.json'
        image.unlink(missing_ok=True)
        telemetry.unlink(missing_ok=True)
        command = ['--scene=industrial_park', '--preset=zhumadian', '--zhumadian-detail=full',
                   '--zhumadian-view=plant', f'--zhumadian-textures={mode}', '--frames=3',
                   '--width=1440', '--height=900', f'--screenshot={image}', f'--stats={telemetry}']
        if mode == 'paged':
            command.append('--zhumadian-texture-probe')
        run_showcase_process(Path(args.demo).resolve(), command, root, timeout=90, fallback_root=root)
        width, height, pixels = read_png_rgb(image)
        assert (width, height) == (1440, 900)
        captures[mode] = pixels
        stats[mode] = json.loads(telemetry.read_text(encoding='utf-8'))
        assert stats[mode]['zhumadian_stage_rendered_root_count'] == 2346
        assert stats[mode]['zhumadian_stage_triangles'] == 3055617
    paged = stats['paged']
    assert stats['base']['zhumadian_texture_resident_pages'] == 0
    assert paged['zhumadian_texture_paging_enabled']
    assert paged['zhumadian_texture_page_catalog_count'] == 113
    assert paged['zhumadian_texture_resident_pages'] == 4
    assert len(set(paged['zhumadian_texture_resident_page_ids'])) == 4
    assert paged['zhumadian_texture_resident_rgba_mip_bytes'] <= 89478480
    assert paged['zhumadian_texture_probe_passed']
    assert paged['zhumadian_texture_stale_decode_results'] >= 1
    assert paged['zhumadian_texture_page_upload_batches'] >= 4
    energies = {k: edge_energy(v, width, height) for k, v in captures.items()}
    changed = sum(max(abs(a-b) for a, b in zip(p, q)) > 3
                  for p, q in zip(captures['base'][width*400:], captures['paged'][width*400:]))
    # The same source photographs must gain visible detail, not merely report resident IDs.
    assert changed > 5000, changed
    assert energies['paged'] > energies['base'], energies
    metrics = {'foreground_edge_energy_ratio': energies['paged']/energies['base'],
               'changed_foreground_pixels': changed,
               'resident_rgba_mip_bytes': paged['zhumadian_texture_resident_rgba_mip_bytes']}
    (output/'metrics.json').write_text(json.dumps(metrics, indent=2), encoding='utf-8')
    print('TEXTURE PAGING PASS: native texels, camera swaps, base fallback, stale rejection;', metrics)


if __name__ == '__main__':
    main()
