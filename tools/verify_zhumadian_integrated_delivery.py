"""Audit actual shipped/default/old-shortcut captures for the integrated profile."""
import hashlib
import json
from pathlib import Path
from verify_zhumadian_native_lod import validate

root = Path(__file__).resolve().parents[1]
directory = root/'work/releases/native_osgb_context_20261010'
path = directory/'manifest.json'
manifest = json.loads(path.read_text(encoding='utf-8'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


for item in manifest['runtime_files']:
    assert sha(directory/item['name']) == item['sha256']
for item in manifest['inputs']:
    assert sha(root/item['path']) == item['sha256'], item['path']
assert sha(root/'work/releases/native_osgb_lod_20261010/manifest.json') == manifest['prior_native_manifest_sha256']
evidence = {}
for name, view in [('shipped', 'overview'), ('entry', 'overview'), ('shortcut', 'overview'), ('close_entry', 'close')]:
    image = root/f'work/zhumadian_integrated_{name}.png'
    stats_path = root/f'work/zhumadian_integrated_{name}.json'
    assert sha(image) == manifest[f'{view}_sha256'], name
    stats = json.loads(stats_path.read_text(encoding='utf-8'))
    assert stats['zhumadian_context_active'] and stats['zhumadian_context_fused']
    assert stats['zhumadian_context_color_matched'] and stats['zhumadian_native_recursive_streaming']
    validate(stats['zhumadian_native_lod'])
    if view == 'close':
        assert any(r['tile'] == 141 and r['depth'] >= 5 for r in stats['zhumadian_native_lod']['resident'])
    evidence[str(image.relative_to(root))] = sha(image)
    evidence[str(stats_path.relative_to(root))] = sha(stats_path)
manifest['delivery_evidence'] = evidence
path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
print('INTEGRATED SHIPPED / DEFAULT ENTRY / EXISTING SHORTCUT / DETAILED CLOSE AUDIT PASS')
