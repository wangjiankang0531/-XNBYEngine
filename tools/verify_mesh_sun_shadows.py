#!/usr/bin/env python3
"""Verify rasterized sun shadows against independent ground-to-box ray intersections."""
import argparse
import hashlib
import json
import math
from pathlib import Path
from run_showcase_demo import run_showcase_process
from verify_badaling_demo import read_png_rgb


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x-y for x, y in zip(a, b))


def unit(a):
    length = math.sqrt(dot(a, a))
    return tuple(v/length for v in a)


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def hits_box(origin, direction):
    near, far = 0.0001, float('inf')
    for value, step, low, high in zip(origin, direction, (-1, -1, 0), (1, 1, 3)):
        if abs(step) < 1e-10:
            if not low <= value <= high:
                return False
            continue
        a, b = sorted(((low-value)/step, (high-value)/step))
        near, far = max(near, a), min(far, b)
    return near < far


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--demo', required=True)
    parser.add_argument('--output-dir', required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    demo = Path(args.demo).resolve()
    hashes, metrics = {}, {}

    def capture(name, arguments):
        image, stats = output/f'{name}.png', output/f'{name}.json'
        image.unlink(missing_ok=True)
        stats.unlink(missing_ok=True)
        run_showcase_process(demo, [*arguments, '--scenario-time=0', '--frames=3', '--width=1440', '--height=900',
                                    f'--screenshot={image}', f'--stats={stats}'], root, timeout=90, fallback_root=root)
        width, height, pixels = read_png_rgb(image)
        assert (width, height) == (1440, 900)
        hashes[name] = hashlib.sha256(image.read_bytes()).hexdigest()
        return pixels, json.loads(stats.read_text(encoding='utf-8'))

    camera = (0, -8, 32)
    forward = unit(sub((0, 0, 0), camera))
    right = unit(cross(forward, (0, 0, 1)))
    up = cross(right, forward)
    tangent = math.tan(math.radians(22))

    def sample(pixels, point):
        delta = sub(point, camera)
        depth = dot(delta, forward)
        x = round((0.5+dot(delta, right)/(2*depth*tangent*1.6))*1440)
        y = round((0.5-dot(delta, up)/(2*depth*tangent))*900)
        if not 2 <= x < 1438 or not 2 <= y < 898:
            return None
        return sum(pixels[(y+dy)*1440+x+dx][0] for dy in (-1, 0, 1) for dx in (-1, 0, 1))/9

    for enabled, azimuth in ((False, 135), (True, 135), (True, 315)):
        name = f'fixture_{azimuth}_'+('mesh' if enabled else 'off')
        pixels, stats = capture(name, ['--preset=overview', '--mesh-shadow-probe', f'--sun-azimuth={azimuth}',
                                      '--sun-elevation=45', *(['--mesh-sun-shadows'] if enabled else [])])
        assert stats['mesh_shadow_probe'] and stats['mesh_sun_shadows'] == enabled
        assert stats['vertices'] == 52 and stats['indices'] == 78
        assert stats['mesh_shadow_rendered_passes'] == int(enabled)
        assert stats['mesh_shadow_active'] == enabled
        assert stats['mesh_shadow_submitted_indices'] == (78 if enabled else 0)
        sun = (math.cos(math.radians(azimuth))/math.sqrt(2),
               math.sin(math.radians(azimuth))/math.sqrt(2), 1/math.sqrt(2))
        checked, shadowed, failures = 0, 0, []
        for gy in range(-36, 37):
            for gx in range(-36, 37):
                point = (gx*.25, gy*.25, .001)
                if any(hits_box((point[0]+dx, point[1]+dy, .001),
                                sub(camera, (point[0]+dx, point[1]+dy, .001)))
                       for dx in (-.15, 0, .15) for dy in (-.15, 0, .15)):
                    continue
                predictions = {hits_box((point[0]+dx, point[1]+dy, .001), sun)
                               for dx in (-.4, 0, .4) for dy in (-.4, 0, .4)}
                if len(predictions) != 1:
                    continue  # Exclude silhouette/PCF/bias transition boundaries.
                expected_shadow = predictions.pop() and enabled
                value = sample(pixels, point)
                if value is None:
                    continue
                checked += 1
                shadowed += int(expected_shadow)
                if (expected_shadow and value > 20) or (not expected_shadow and value < 235):
                    failures.append((point, expected_shadow, value))
        assert checked > 3600 and (not enabled or shadowed > 20), (name, checked, shadowed)
        assert not failures, (name, len(failures), failures[:8])
        if azimuth == 135:
            assert sample(pixels, (-4.5, 3.5, .001)) > 235, 'Effect box cast an opaque shadow'
        metrics[name] = {'ray_checked_samples': checked, 'shadowed_samples': shadowed, 'failures': len(failures)}

    before, old_stats = capture('valve_off', ['--preset=huilian', '--huilian-view=valve'])
    after, new_stats = capture('valve_mesh', ['--preset=huilian', '--huilian-view=valve', '--mesh-sun-shadows'])
    assert old_stats['vertices'] == new_stats['vertices'] and old_stats['indices'] == new_stats['indices']
    assert new_stats['mesh_shadow_rendered_passes'] == 1 and new_stats['mesh_shadow_map_side'] == 2048
    changed = sum(a != b for a, b in zip(before, after))
    assert changed > 1000
    assert before[:1440*160] == after[:1440*160], 'Sun shadow changed sky pixels'
    metrics['valve'] = {'changed_pixels': changed, 'same_mesh_vertices': new_stats['vertices']}
    _, photo_stats = capture('photo_mesh', ['--preset=zhumadian', '--zhumadian-detail=full',
                                           '--zhumadian-view=plant', '--zhumadian-geometry=foreground',
                                           '--zhumadian-context', '--zhumadian-context-color=matched',
                                           '--zhumadian-context-geometry=fused', '--mesh-sun-shadows'])
    assert hashes['photo_mesh'] == 'a2bd96723637d1f42067fdf3c5c8f34059918a5be194232cf113ff7d349b003c'
    assert not photo_stats['mesh_shadow_active'] and photo_stats['mesh_shadow_submitted_indices'] == 0
    metrics['photo'] = {'unchanged_reference': True, 'shadow_geometry_submissions': 0}
    result = {'image_sha256': hashes, 'metrics': metrics,
              'policy': 'known opaque cube ray test, opposing sun, excluded transparent caster, real imported equipment'}
    (output/'metrics.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print('MESH SUN SHADOW / INDEPENDENT RAYS / SUN REVERSAL / TRANSPARENT EXCLUSION / EQUIPMENT PASS', result)


if __name__ == '__main__':
    main()
