"""Verify visible-LOD picking against independent, complete glTF triangle data.

Run with the workspace QA Python containing NumPy. NumPy is not an application
runtime dependency. No source triangles are decimated for this reference check.
"""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def mesh(path):
    gltf = read(path)
    assert all(set(n) <= {'mesh', 'name'} for n in gltf['nodes'])
    assert len(gltf['meshes']) == 1 and len(gltf['meshes'][0]['primitives']) == 1
    primitive = gltf['meshes'][0]['primitives'][0]
    accessors = gltf['accessors']
    position = accessors[primitive['attributes']['POSITION']]
    index = accessors[primitive['indices']]
    assert position['componentType'] == 5126 and position['type'] == 'VEC3'
    assert index['componentType'] == 5125 and index['type'] == 'SCALAR'
    views = gltf['bufferViews']
    data = path.parent / gltf['buffers'][0]['uri']
    assert data.stat().st_size == gltf['buffers'][0]['byteLength']
    def offset(accessor):
        view = views[accessor['bufferView']]
        assert view.get('buffer', 0) == 0 and 'byteStride' not in view
        return view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
    positions = np.memmap(data, mode='r', dtype='<f4', offset=offset(position), shape=(position['count'], 3))
    indices = np.memmap(data, mode='r', dtype='<u4', offset=offset(index), shape=(index['count'],))
    assert len(indices) % 3 == 0 and indices.max() < len(positions)
    return positions, indices


def reference_part(positions, indices, tile, depth):
    triangles = positions[np.asarray(indices).reshape(-1, 3)].astype(np.float64)
    assert np.isfinite(triangles).all()
    return {'v0': triangles[:, 0].copy(), 'e1': triangles[:, 1] - triangles[:, 0],
            'e2': triangles[:, 2] - triangles[:, 0], 'low': triangles.min(axis=(0, 1)),
            'high': triangles.max(axis=(0, 1)), 'tile': tile, 'depth': depth, 'count': len(triangles)}


def possible(origin, direction, low, high, maximum):
    first, last = 0., maximum
    for axis in range(3):
        if direction[axis] == 0:
            if not low[axis] <= origin[axis] <= high[axis]:
                return False
        else:
            a, b = (low[axis] - origin[axis]) / direction[axis], (high[axis] - origin[axis]) / direction[axis]
            first, last = max(first, min(a, b)), min(last, max(a, b))
            if last < first:
                return False
    return True


def closest(parts, origin, direction):
    # Vectorized direct Moller-Trumbore, independent of the C++ BVH partitions.
    nearest, chosen = float('inf'), None
    for part in parts:
        if not possible(origin, direction, part['low'], part['high'], nearest):
            continue
        pv = np.cross(direction, part['e2'])
        determinant = np.einsum('ij,ij->i', part['e1'], pv)
        valid = np.abs(determinant) >= 1e-12
        inverse = np.divide(1., determinant, out=np.zeros_like(determinant), where=valid)
        tv = origin - part['v0']
        u = np.einsum('ij,ij->i', tv, pv) * inverse
        qv = np.cross(tv, part['e1'])
        v = qv @ direction * inverse
        distance = np.einsum('ij,ij->i', part['e2'], qv) * inverse
        valid &= (u >= 0) & (v >= 0) & (u + v <= 1) & (distance > 1e-9) & (distance <= nearest)
        if valid.any():
            candidate = distance[valid].min()
            if candidate < nearest:
                nearest, chosen = candidate, part
    return None if chosen is None else (nearest, chosen)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', default='work/releases/landscape_apps_20261011')
    parser.add_argument('--view', choices=['near', 'base', 'far'], default='near')
    parser.add_argument('--exe', default='ht_landscape_editor_0_8.exe')
    parser.add_argument('--output', default='work/landscape_surface_checks')
    args = parser.parse_args()
    package = (ROOT / args.package).resolve()
    out = ROOT / args.output
    report = out / f'{args.view}.json'
    data = read(report)
    lod, probe = data['native_lod'], data['surface_pick']
    assert lod['picking_enabled'] and lod['base_picking_triangles'] == 3668177
    assert not lod['partial_root_drawn']
    if args.view == 'base':
        assert lod['pending'] and not lod['resident']  # Pick the visible parent during real loading.
    else:
        assert lod['settled'] and not lod['pending']
    assert all(r['picking_triangles'] == r['triangles'] for r in lod['resident'])
    catalog = read(package / 'data/showcase/zhumadian_native_hierarchy.json')
    positions, indices = mesh(package / 'assets/showcase/runtime/zhumadian/full/zhumadian_full.gltf')
    context_positions, context_indices = mesh(package / 'assets/showcase/runtime/zhumadian/context/context_fused.gltf')
    context = reference_part(context_positions, context_indices, 0, 0)
    resident = {r['tile']: r for r in lod['resident']}
    fine, source_hashes = [], []
    for tile, ready in resident.items():
        folder = package / f'work/zhumadian_native_chunk_cache/tile_{tile}_depth_{ready["depth"]}'
        if not (folder / 'complete.json').exists():
            subprocess.run([str(package / 'runtime/python/python.exe'), '-X', 'utf8',
                            str(package / 'tools/convert_zhumadian_native_chunks.py'),
                            f'--tile={tile}', f'--depth={ready["depth"]}'], cwd=package, check=True, timeout=180)
        complete = read(folder / 'complete.json')
        assert complete['indices'] // 3 == ready['triangles'] and complete['complete_frontier']
        for item in complete['parts']:
            for file in item['files']:
                path = folder / item['directory'] / file['name']
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                assert digest == file['sha256']
                source_hashes.append({'path': str(path.relative_to(package)), 'sha256': digest})
            p, i = mesh(folder / item['directory'] / 'geometry.gltf')
            fine.append(reference_part(p, i, tile, ready['depth']))
    rows, differences = [], []
    # Base candidates are selected using root bounds, then tested without a BVH.
    # Only ready parents are omitted from the visible reference, exactly as draw().
    for sample in probe['samples']:
        origin, direction = np.array(sample['ray_origin']), np.array(sample['ray_direction'])
        assert abs(np.linalg.norm(direction) - 1) < 1e-8
        bases = []
        for root in catalog['root_records']:
            if not possible(origin, direction, root['bounds_min'], root['bounds_max'], float('inf')):
                continue
            spans = np.concatenate([indices[first:first + count] for first, count in root['base_spans']])
            bases.append(reference_part(positions, spans, root['tile_id'], 0))
        displayed = [context, *fine, *(b for b in bases if b['tile'] not in resident)]
        expected = closest(displayed, origin, direction)
        actual = sample.get('hit')
        assert bool(expected) == bool(actual), sample['pixel']
        if expected:
            distance, part = expected
            assert abs(distance - actual['distance']) < 1e-5, (sample['pixel'], distance, actual)
            assert actual['tile'] == part['tile'] and actual['depth'] == part['depth']
            assert np.linalg.norm(origin + direction * distance - actual['point']) < 1e-5
        coarse = closest([context, *bases], origin, direction)
        if coarse:
            assert np.linalg.norm(origin + direction * coarse[0] - sample['base_point']) < 1e-5
            if expected:
                differences.append(abs(coarse[0] - expected[0]))
        rows.append({'pixel': sample['pixel'], 'matched': True, 'triangle_tests': sample['triangle_tests'],
                     'query_ms': sample['query_ms'], 'tile': actual['tile'] if actual else 0,
                     'depth': actual['depth'] if actual else 0})
    assert len(rows) == 25
    if args.view == 'near':
        assert probe['fine_hits'] >= 20 and probe['different_from_base'] > 0
        assert any(r['tile'] == 141 and r['depth'] >= 6 for r in lod['resident'])
    else:
        assert not lod['resident'] and not probe['fine_hits']
    result = {'passed': True, 'view': args.view, 'samples': rows, 'independent_reference': 'complete source glTF triangles',
              'max_base_difference_m': max(differences, default=0), 'picking_cpu_bytes': lod['picking_cpu_bytes'],
              'picking_base_triangles': lod['base_picking_triangles'], 'fine_hits': probe['fine_hits'],
              'exe_sha256': hashlib.sha256((package / args.exe).read_bytes()).hexdigest(),
              'source_files': source_hashes}
    (out / f'{args.view}_metrics.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print('VISIBLE SOURCE TRIANGLES / NEAREST SURFACE / HIDDEN PARENT EXCLUSION PASS', args.view)


if __name__ == '__main__':
    main()
