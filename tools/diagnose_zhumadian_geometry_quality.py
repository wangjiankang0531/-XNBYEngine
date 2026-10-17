#!/usr/bin/env python3
"""Read-only source-tier quality diagnostics; thin faces are not auto-deleted."""
import hashlib
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root/'work/geospatial_deps'))
import numpy as np


def read(directory, name):
    path = root/f'assets/showcase/runtime/zhumadian/{directory}/{name}.gltf'
    doc = json.loads(path.read_text(encoding='utf-8'))
    binary = path.parent/doc['buffers'][0]['uri']
    primitive = doc['meshes'][0]['primitives'][0]

    def accessor(index, dtype, size):
        acc = doc['accessors'][index]
        view = doc['bufferViews'][acc['bufferView']]
        return np.memmap(binary, dtype=dtype, mode='r',
            offset=view.get('byteOffset', 0)+acc.get('byteOffset', 0), shape=(acc['count'], size))

    return accessor(primitive['attributes']['POSITION'], '<f4', 3), accessor(primitive['indices'], '<u4', 1).reshape(-1), binary


def inspect(positions, indices):
    assert len(indices) % 3 == 0 and int(indices.max()) < len(positions)
    edge, quality = [], []
    degenerate = 0
    for start in range(0, len(indices), 300000):
        triangles = positions[indices[start:start+300000].reshape(-1, 3)]
        assert np.isfinite(triangles).all()
        a, b, c = triangles[:, 1]-triangles[:, 0], triangles[:, 2]-triangles[:, 0], triangles[:, 2]-triangles[:, 1]
        squared = np.stack([(a*a).sum(1), (b*b).sum(1), (c*c).sum(1)], axis=1)
        doubled_area = np.linalg.norm(np.cross(a, b), axis=1)
        degenerate += int((doubled_area < 1e-6).sum())
        edge.append(np.sqrt(squared.max(1)))
        quality.append(2*np.sqrt(3)*doubled_area / np.maximum(squared.sum(1), 1e-12))
    edge, quality = np.concatenate(edge), np.concatenate(quality)
    return {'triangles': len(indices)//3, 'nonfinite_or_invalid_indices': 0,
        'longest_edge_m_percentiles_50_90_99': np.percentile(edge, [50, 90, 99]).tolist(),
        'near_degenerate_area_count': degenerate, 'shape_quality_below_0_05': int((quality < 0.05).sum()),
        'warning': 'Thin/long triangles may represent valid poles, pipes, roof edges or ground. Counts are diagnostics, not a safe deletion mask.'}


manifest = json.loads((root/'data/showcase/zhumadian_geometry_foreground.json').read_text(encoding='utf-8'))
positions, indices, binary = read('full', 'zhumadian_full')
spans = sorted(s for entry in manifest['replacements'] for s in entry['spans'])
parent = np.concatenate([indices[start:start+count] for start, count in spans])
before = inspect(positions, parent)
child_positions, child_indices, child_binary = read('geometry_foreground', 'detail')
after = inspect(child_positions, child_indices)
assert before['triangles'] == manifest['removed_base_triangles']
assert after['triangles'] == manifest['triangles']
result = {'source_roots': 2346, 'refined_roots': len(manifest['replacements']),
    'refinement_depth': manifest['depth'], 'center': manifest['selection_center'],
    'radius_m': manifest['selection_radius'], 'bounds_min': manifest['bounds_min'], 'bounds_max': manifest['bounds_max'],
    'parent': before, 'depth3_children': after,
    'input_sha256': {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in (binary, child_binary)},
    'conclusion': 'Finite/index-valid geometry does not establish clean reconstruction. Current static refinement is local; moving outside it retains coarse roots. Recursive screen-error refinement and source repair remain open.'}
output = root/'work/zhumadian_geometry_quality_20261010.json'
output.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
