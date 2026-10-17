#!/usr/bin/env python3
"""Derive a bounded geographic color correction; preserve all original imagery."""
import argparse
import array
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / 'data/showcase/zhumadian_context_color.json'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build():
    sys.path.insert(0, str(ROOT / 'work/geospatial_deps'))
    import numpy as np
    from PIL import Image, ImageFilter
    full = ROOT / 'assets/showcase/runtime/zhumadian/full'
    context = ROOT / 'assets/showcase/runtime/zhumadian/context'
    doc = json.loads((full / 'zhumadian_full.gltf').read_text(encoding='utf-8'))
    original = json.loads((ROOT / 'data/showcase/zhumadian_context.json').read_text(encoding='utf-8'))
    primitive = doc['meshes'][0]['primitives'][0]
    binary = full / doc['buffers'][0]['uri']

    def attribute(name, width):
        accessor = doc['accessors'][primitive['attributes'][name]]
        view = doc['bufferViews'][accessor['bufferView']]
        return np.memmap(binary, dtype='<f4', mode='r',
                         offset=view.get('byteOffset', 0)+accessor.get('byteOffset', 0),
                         shape=(accessor['count'], width))[::17]

    positions, normals, uv = attribute('POSITION', 3), attribute('NORMAL', 3), attribute('TEXCOORD_0', 2)
    print('SOURCE SAMPLE HEIGHT', np.percentile(positions[:, 2], [0, 10, 50, 90, 100]),
          'UPWARD NORMALS', int((normals[:, 2] > .85).sum()), flush=True)
    selected = normals[:, 2] > .85
    positions, uv = positions[selected], uv[selected]
    xmin, ymin, xmax, ymax = original['local_bounds']
    side = 256
    u = (positions[:, 0]-xmin)/(xmax-xmin)
    v = 1-(positions[:, 1]-ymin)/(ymax-ymin)
    ground_cells = np.clip((v*side).astype(int), 0, side-1)*side+np.clip((u*side).astype(int), 0, side-1)
    selected = np.zeros(len(positions), dtype=bool)
    for cell in np.unique(ground_cells):
        local = ground_cells == cell
        floor = np.percentile(positions[local, 2], 15)
        selected |= local & (positions[:, 2] <= floor+2.5)
    positions, uv = positions[selected], uv[selected]
    atlas_path = full / 'zhumadian_full_atlas.jpg'
    atlas = np.asarray(Image.open(atlas_path).convert('RGB'))
    photo = atlas[np.clip((uv[:, 1]*atlas.shape[0]).astype(int), 0, atlas.shape[0]-1),
                  np.clip((uv[:, 0]*atlas.shape[1]).astype(int), 0, atlas.shape[1]-1)].astype(float)/255
    image_path = context / 'context.jpg'
    satellite = np.asarray(Image.open(image_path).convert('RGB'))
    u = (positions[:, 0]-xmin)/(xmax-xmin)
    v = 1-(positions[:, 1]-ymin)/(ymax-ymin)
    background = satellite[np.clip((v*satellite.shape[0]).astype(int), 0, satellite.shape[0]-1),
                           np.clip((u*satellite.shape[1]).astype(int), 0, satellite.shape[1]-1)].astype(float)/255
    # Avoid sky-blue roof/water, very dark shadows, and white clipping samples.
    usable = ((photo.min(axis=1) > .07) & (photo.max(axis=1) < .90) &
              (background.min(axis=1) > .07) & (background.max(axis=1) < .90) &
              (photo[:, 2] < photo[:, 1]*1.05) & (background[:, 2] < background[:, 1]*1.05))
    photo, background, u, v = photo[usable], background[usable], u[usable], v[usable]
    cells = np.clip((v*side).astype(int), 0, side-1)*side+np.clip((u*side).astype(int), 0, side-1)
    # Hold out entire geographic cells; duplicated source vertices cannot leak
    # into both fitting and validation at the same geographic location.
    training = cells % 5 != 0
    print('PAIRED GROUND', len(cells), 'cells', len(np.unique(cells)), flush=True)
    anchors, gains, counts = [], [], []
    for cell in np.unique(cells[training]):
        mask = training & (cells == cell)
        if mask.sum() < 16:
            continue
        photo_odds = photo[mask]/(1-photo[mask])
        background_odds = background[mask]/(1-background[mask])
        gain = np.exp(np.median(np.log(photo_odds/background_odds), axis=0))
        anchors.append((cell % side, cell // side))
        gains.append(np.clip(gain, .5, 2.5)); counts.append(int(mask.sum()))
    if len(anchors) < 40:
        raise RuntimeError('Insufficient geographically paired ground samples')
    anchors, gains = np.asarray(anchors), np.asarray(gains)
    yy, xx = np.indices((side, side))
    points = np.stack([xx, yy], axis=-1).reshape(-1, 2)
    nearest = np.empty(len(points), dtype=int)
    distance = np.empty(len(points))
    for start in range(0, len(points), 2048):
        squared = ((points[start:start+2048, None]-anchors[None])**2).sum(axis=2)
        indices = squared.argmin(axis=1)
        nearest[start:start+len(indices)] = indices
        distance[start:start+len(indices)] = np.sqrt(squared[np.arange(len(indices)), indices])*(xmax-xmin)/side
    smooth_distance = np.clip(distance/1500, 0, 1)
    weight = 1-smooth_distance*smooth_distance*(3-2*smooth_distance)
    field = 1+(gains[nearest]-1)*weight[:, None]
    field = field.reshape(side, side, 3)
    # Smooth the geographic gain field, not image detail. Neutral remains exactly 1.
    for channel in range(3):
        encoded = np.rint((field[:, :, channel]-1)*64+128).astype('uint8')
        field[:, :, channel] = 1+(np.asarray(Image.fromarray(encoded).filter(ImageFilter.GaussianBlur(1.5))).astype(float)-128)/64
    sampled_gain = field[np.clip((v*side).astype(int), 0, side-1), np.clip((u*side).astype(int), 0, side-1)]
    fitted = background*sampled_gain/(1+background*(sampled_gain-1))
    validation = ~training
    before = float(np.median(np.abs(photo[validation]-background[validation])))
    after = float(np.median(np.abs(photo[validation]-fitted[validation])))
    if not after < before:
        raise RuntimeError(f'Geographic held-out residual did not improve: {before} -> {after}')
    corrected = np.empty(satellite.shape, dtype='uint8')
    for channel in range(3):
        gain_image = Image.fromarray(field[:, :, channel].astype('float32')).resize(
            (satellite.shape[1], satellite.shape[0]), Image.Resampling.BILINEAR)
        gain = np.asarray(gain_image)
        value = satellite[:, :, channel].astype(float)/255
        # Odds gain maps 0 -> 0 and 1 -> 1, retaining highlights without hard clipping.
        corrected[:, :, channel] = np.rint(255*value*gain/(1+value*(gain-1))).astype('uint8')
    output = context / 'context_colormatched.jpg'
    Image.fromarray(corrected).save(output, quality=95, subsampling=0)
    field_path = context / 'context_color_gain.f32'
    field.astype('<f4').tofile(field_path)
    inputs = [binary, atlas_path, image_path, ROOT / 'data/showcase/zhumadian_context.json']
    artifacts = [output, field_path]
    manifest = {'schema': 'hongtu.zhumadian_context_color.v1',
                'policy': 'normal z > 0.85; per-156m-cell lower 15th height percentile plus 2.5 m; bounded display RGB odds gain; unsurveyed visual correction',
                'operator': 'value * gain / (1 + value * (gain - 1)); no hard clipping',
                'source_images_preserved': True, 'gain_limits': [.5, 2.5], 'falloff_m': 1500,
                'field_side': side, 'anchors': len(anchors), 'ground_samples': len(cells),
                'training_samples': int(training.sum()), 'held_out_samples': int(validation.sum()),
                'validation_policy': 'every fifth geographic cell excluded entirely from fitting',
                'median_rgb_residual_before': before, 'median_rgb_residual_after': after,
                'anchor_positions': anchors.tolist(), 'anchor_gains': gains.tolist(), 'anchor_sample_counts': counts,
                'inputs': [{'path':p.relative_to(ROOT).as_posix(), 'sha256':sha(p)} for p in inputs],
                'artifacts': [{'path':p.relative_to(ROOT).as_posix(), 'sha256':sha(p)} for p in artifacts]}
    MANIFEST.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print('CONTEXT COLOR BUILD', len(anchors), 'anchors;', len(cells), 'samples;', before, '->', after)


def check():
    doc = json.loads(MANIFEST.read_text(encoding='utf-8'))
    for item in doc['inputs']+doc['artifacts']:
        assert sha(ROOT/item['path']) == item['sha256'], item['path']
    assert doc['median_rgb_residual_after'] < doc['median_rgb_residual_before']
    field = array.array('f')
    field.frombytes((ROOT/'assets/showcase/runtime/zhumadian/context/context_color_gain.f32').read_bytes())
    if sys.byteorder != 'little':
        field.byteswap()
    assert len(field) == 256*256*3 and min(field) >= .5 and max(field) <= 2.5
    assert list(field[:3]) == list(field[-3:]) == [1.0, 1.0, 1.0]
    print('CONTEXT COLOR AUDIT PASS')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', action='store_true')
    args = parser.parse_args()
    if args.build:
        build()
    check()
