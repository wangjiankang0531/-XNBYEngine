"""Read-only source tessellation comparison; never use shape counts as deletion masks."""
import hashlib
import json
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root/'work/geospatial_deps'))
import numpy as np


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def arrays(gltf):
    doc = json.loads(gltf.read_text(encoding='utf-8'))
    primitive = doc['meshes'][0]['primitives'][0]
    binary = gltf.parent/doc['buffers'][0]['uri']
    def read(index, dtype, components):
        accessor = doc['accessors'][index]
        view = doc['bufferViews'][accessor['bufferView']]
        return np.memmap(binary, dtype=dtype, mode='r',
            offset=view.get('byteOffset', 0)+accessor.get('byteOffset', 0), shape=(accessor['count'], components))
    return read(primitive['attributes']['POSITION'], '<f4', 3), read(primitive['indices'], '<u4', 1).reshape(-1), binary


def measure(positions, indices):
    assert len(indices) > 0 and len(indices) % 3 == 0 and int(indices.max()) < len(positions)
    triangles = positions[indices.reshape(-1, 3)]
    assert np.isfinite(triangles).all()
    a, b, c = triangles[:, 1]-triangles[:, 0], triangles[:, 2]-triangles[:, 0], triangles[:, 2]-triangles[:, 1]
    squared = np.stack([(a*a).sum(1), (b*b).sum(1), (c*c).sum(1)], axis=1)
    area = np.linalg.norm(np.cross(a, b), axis=1)
    quality = 2*np.sqrt(3)*area/np.maximum(squared.sum(1), 1e-12)
    return {'triangles': len(indices)//3, 'longest_edge_p50_p90_p99_m': np.percentile(np.sqrt(squared.max(1)), [50,90,99]).tolist(),
            'thin_faces_quality_below_0_05': int((quality < .05).sum()), 'near_zero_area': int((area < 1e-6).sum()),
            'invalid_or_nonfinite': 0}


master = json.loads((root/'data/showcase/zhumadian_native_hierarchy.json').read_text(encoding='utf-8'))
record = master['root_records'][140]
assert record['tile_id'] == 141
p, indices, path = arrays(root/'assets/showcase/runtime/zhumadian/full/zhumadian_full.gltf')
selected = np.concatenate([indices[start:start+count] for start, count in record['base_spans']])
result = {'tile': 141, 'base': measure(p, selected), 'input_sha256': {str(path.relative_to(root)): sha(path)}}
for depth in (3, 4, 5):
    folder = root/f'work/zhumadian_native_cache/tile_141_depth_{depth}_atlas_1024'
    stamp = json.loads((folder/'complete.json').read_text(encoding='utf-8'))
    p, indices, path = arrays(folder/'geometry.gltf')
    expected = next(f['sha256'] for f in stamp['files'] if f['name'] == path.name)
    assert sha(path) == expected
    result[f'depth{depth}'] = measure(p, indices)
    assert result[f'depth{depth}']['triangles'] == sum(stamp['exported_source_face_counts'].values())
    result['input_sha256'][str(path.relative_to(root))] = expected
result['conclusion'] = 'Deeper source improves tessellation at this fixed source site. Thin triangles include valid pipes/roof/vegetation, so this does not prove or repair all reconstruction noise.'
output = root/'work/native_lod_source_quality_20261010.json'
output.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
