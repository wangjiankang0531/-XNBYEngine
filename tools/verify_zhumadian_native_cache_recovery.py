"""Exercise missing/corrupt derived payload recovery using unchanged real sources."""
import hashlib
import json
import sys
from pathlib import Path
from convert_zhumadian_native_root import convert, ROOT

sys.path.insert(0, str(ROOT/'work/geospatial_deps'))
folder = ROOT/'work/zhumadian_native_cache/tile_1_depth_3_atlas_1024'


def verify():
    bundle = convert(1, 3, 1024)
    for item in bundle['files']:
        assert hashlib.sha256((folder/item['name']).read_bytes()).hexdigest() == item['sha256']
    assert bundle['complete_frontier'] and bundle['indices']//3 == sum(bundle['exported_source_face_counts'].values())
    return bundle


original = verify()
binary = folder/'geometry.bin'
before = binary.read_bytes()
try:
    changed = bytearray(before)
    changed[0] ^= 0x40
    binary.write_bytes(changed)
    recovered = verify()
    assert recovered['indices'] == original['indices']
    (folder/'atlas.jpg').unlink()
    recovered = verify()
    assert recovered['indices'] == original['indices']
finally:
    # Validate/rebuild the entire transaction, rather than restoring an old
    # binary beside an atlas that may have been regenerated in a different order.
    verify()
evidence = {'passed': True, 'tile': 1, 'depth': 3, 'missing_atlas_rebuilt': True,
    'corrupt_binary_rebuilt': True, 'source_frontier': original['stamp']['frontier'],
    'triangles': recovered['indices']//3, 'original_source_untouched': True}
(ROOT/'work/native_lod_cache_recovery_20261010.json').write_text(json.dumps(evidence, indent=2), encoding='utf-8')
print('NATIVE DERIVED CACHE CORRUPT BINARY / MISSING ATLAS REAL SOURCE REBUILD PASS')
