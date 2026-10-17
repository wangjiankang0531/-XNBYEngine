#!/usr/bin/env python3
"""Decode a complete real OSGB descendant frontier into a bounded root bundle.

Intended for a single asynchronous worker. All siblings are included, terminal
leaves persist, and original sources/verified releases are never modified.
"""
import argparse
import array
import hashlib
import json
import math
import os
import re
import sys
import time
from pathlib import Path
import build_zhumadian_stage_asset as base
import build_zhumadian_full_stage as full

ROOT = base.repo_root()
CACHE = ROOT/'work/zhumadian_native_cache'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prune_cache(protected=None):
    """Bound only this converter's derived cache; never follow directory links."""
    if not CACHE.exists():
        return
    folders = [p for p in CACHE.iterdir() if p.is_dir() and not p.is_symlink() and
               re.fullmatch(r'tile_\d+_depth_\d+_atlas_(1024|2048)', p.name)]
    records = sorted((max((f.stat().st_mtime for f in p.iterdir() if f.is_file()), default=0), p,
                      sum(f.stat().st_size for f in p.iterdir() if f.is_file())) for p in folders)
    size = sum(record[2] for record in records)
    for _, folder, amount in records:
        if size <= 1024*1024*1024:
            break
        if folder == protected:
            continue
        assert folder.resolve().is_relative_to(CACHE.resolve())
        for path in folder.iterdir():
            if path.is_file() and not path.is_symlink() and (path.name in {
                    'geometry.gltf', 'geometry.bin', 'atlas.jpg', 'complete.json', 'complete.pending.json',
                    'frontier.obj', 'frontier.mtl', 'source_list.txt', 'source_triangles.tsv', 'rejected.json', 'failure.json'} or
                    re.fullmatch(r'texture_\d+\.rgb', path.name)):
                path.unlink()
        remaining = sum(path.stat().st_size for path in folder.iterdir() if path.is_file())
        if not any(folder.iterdir()):
            folder.rmdir()
        size -= amount-remaining


def frontier(catalog, depth):
    nodes = {record['name']: record for record in catalog['nodes']}
    names = {catalog['root']}
    for _ in range(depth):
        next_names = set()
        for name in names:
            next_names.update(nodes[name]['children'] or [name])
        if next_names == names:
            break
        names = next_names
    return [nodes[name] for name in sorted(names)]


def audit_frontier(paths, obj, source_audit):
    # Spatial subdivisions append digits, not just the initial chain of 'u'.
    pattern = re.compile(r'Tile_[+-]\d+_[+-]\d+_L\d+_0[0-9u]+')
    native_counts = {}
    for line in source_audit.read_text(encoding='utf-8').splitlines():
        path, count = line.rsplit('\t', 1)
        assert Path(path).name not in native_counts, 'duplicate audited source'
        native_counts[Path(path).name] = int(count)
    assert set(native_counts) == {path.name for path in paths}, 'incomplete native reader audit'
    expected = {}
    for path in paths:
        names = set(pattern.findall(path.read_bytes().decode('latin1')))
        assert len(names) == 1, f'ambiguous source geometry in {path.name}'
        name = names.pop()
        assert name not in expected, 'duplicate frontier source geometry'
        expected[name] = native_counts[path.name]
    counts, current = {}, None
    for line in full.text_lines(obj):
        if line.startswith(('g ', 'o ')):
            match = pattern.search(line)
            current = match.group() if match else None
        elif line.startswith('f '):
            assert current is not None, 'unattributed exported triangle'
            counts[current] = counts.get(current, 0)+1
    assert not set(counts)-set(expected), 'unexpected exported geometry'
    assert all(counts.get(name, 0) == count for name, count in expected.items()), (
        'native reader / OBJ triangle mismatch',
        {name: [count, counts.get(name, 0)] for name, count in expected.items()
         if counts.get(name, 0) != count})
    # Empty native geometry is retained explicitly; a positive source count can
    # never be excused as a missing output group.
    return expected


def convert_payload(tile, depth, atlas_size=1024):
    manifest_path = ROOT/'data/showcase/zhumadian_native_hierarchy.json'
    if manifest_path.exists():
        master = json.loads(manifest_path.read_text(encoding='utf-8'))
        root_record = next(record for record in master['root_records'] if record['tile_id'] == tile)
        catalog_path = ROOT/root_record['catalog']
    else:
        # A completed individual catalog can be tested while the full forest
        # indexer runs. This does not advertise a partial forest as complete.
        catalog_path = ROOT/f'assets/showcase/runtime/zhumadian/native_hierarchy/tile_{tile}.json'
        root_record = {'sha256': sha(catalog_path)}
    assert sha(catalog_path) == root_record['sha256']
    catalog = json.loads(catalog_path.read_text(encoding='utf-8'))
    expected = next(record for record in json.loads((ROOT/'data/showcase/zhumadian_full_stage_manifest.json').read_text(encoding='utf-8'))['source_roots']
                    if record['tile_id'] == tile)
    assert next(record for record in catalog['nodes'] if record['name'] == catalog['root'])['sha256'] == expected['sha256']
    depth = min(depth, catalog['max_descendant_depth'])
    sources = frontier(catalog, depth)
    source_directory = (ROOT/catalog['source_directory']).resolve()
    assert source_directory.is_relative_to((ROOT/'assets/驻马店园区').resolve())
    assert len(sources) > 0 and sum(record['bytes'] for record in sources) <= 64*1024*1024, 'frontier source decode budget exceeded'
    for record in sources:
        path = source_directory/record['name']
        assert path.parent == source_directory and sha(path) == record['sha256'], 'source changed'
    folder = CACHE/f'tile_{tile}_depth_{depth}_atlas_{atlas_size}'
    assert folder.resolve().is_relative_to(CACHE.resolve())
    folder.mkdir(parents=True, exist_ok=True)
    prune_cache(folder)
    stamp = {'catalog_sha256': root_record['sha256'], 'depth': depth, 'atlas_size': atlas_size,
        'exporter_sha256': sha(ROOT/'work/osgb_exporter_build/Release/ht_osgb_exporter.exe'),
        'packer_sha256': sha(Path(full.__file__)),
        'converter_sha256': sha(Path(__file__)), 'frontier': sources}
    complete = folder/'complete.json'
    rejected = folder/'rejected.json'
    if rejected.exists():
        previous = json.loads(rejected.read_text(encoding='utf-8'))
        if previous.get('stamp') == stamp:
            raise AssertionError(previous['reason'])
    if complete.exists():
        cached = json.loads(complete.read_text(encoding='utf-8'))
        if (cached.get('stamp') == stamp and {f['name'] for f in cached.get('files', [])} ==
                {'geometry.gltf', 'geometry.bin', 'atlas.jpg'} and
                all((folder/f['name']).is_file() and sha(folder/f['name']) == f['sha256'] for f in cached['files'])):
            complete.touch()
            print(json.dumps({'cached': True, 'bundle': str(complete)}), flush=True)
            return cached
    defaults = base.parse_args([])
    osg = base.resolve_path(ROOT, defaults.osgconv)
    env = os.environ.copy()
    env['PATH'] = str(osg.parent)+os.pathsep+env.get('PATH', '')
    env['OSG_LIBRARY_PATH'] = str(base.resolve_path(ROOT, defaults.osg_plugin_dir))
    env['OSG_OPTIMIZER'] = 'DEFAULT TEXTURE_ATLAS_BUILDER'
    env['OSG_NOTIFY_LEVEL'] = 'WARN'
    alias = base.ensure_ascii_source_alias(ROOT/'assets/驻马店园区', ROOT/'work/zmd_source')
    paths = [source_directory/record['name'] for record in sources]
    source_list = folder/'source_list.txt'
    source_list.write_text(''.join(str(alias/path.relative_to((ROOT/'assets/驻马店园区').resolve()))+'\n' for path in paths), encoding='utf-8')
    obj = folder/'frontier.obj'
    source_audit = folder/'source_triangles.tsv'
    base.run_hidden([str(ROOT/'work/osgb_exporter_build/Release/ht_osgb_exporter.exe'), str(source_list), str(obj), str(source_audit)],
                    folder, env, f'native root {tile} depth {depth}')
    face_counts = audit_frontier(paths, obj, source_audit)
    material = obj.with_suffix('.mtl').read_text(encoding='utf-8')
    texture_names = re.findall(r'map_Kd\s+([^\r\n]+)', material)
    assert texture_names and all(not Path(name).is_absolute() and ':' not in name and '..' not in Path(name).parts
                                 for name in texture_names), 'exported image path escapes local output'
    full.SIZE = atlas_size
    atlas = folder/'atlas.jpg'
    try:
        mapping, pages = full.pack_atlas([obj], atlas)
    except base.StageAssetError as error:
        if 'budget' in str(error):
            rejected.write_text(json.dumps({'stamp': stamp, 'reason': str(error)}), encoding='utf-8')
        raise
    binary = folder/'geometry.bin'
    views, vertices, indices, low, high, _ = full.write_geometry([obj], mapping, binary)
    # Geometry and mip atlas payload are bounded before the render thread receives it.
    texture_bytes = sum((atlas_size//(2**i))**2*4 for i in range(int(math.log2(atlas_size))+1))
    payload = vertices*64 + indices*4 + texture_bytes
    if payload > 32*1024*1024:
        reason = f'converted GPU root bundle exceeds upload budget: {payload} > {32*1024*1024}'
        rejected.write_text(json.dumps({'stamp': stamp, 'reason': reason}), encoding='utf-8')
        raise AssertionError(reason)
    geometry = {'asset': {'version': '2.0', 'generator': 'HongTu complete native OSGB frontier'},
        'buffers': [{'uri': 'geometry.bin', 'byteLength': binary.stat().st_size}], 'bufferViews': views,
        'accessors': [
            {'bufferView': 0, 'componentType': 5126, 'count': vertices, 'type': 'VEC3', 'min': low, 'max': high},
            {'bufferView': 1, 'componentType': 5126, 'count': vertices, 'type': 'VEC3'},
            {'bufferView': 2, 'componentType': 5126, 'count': vertices, 'type': 'VEC2'},
            {'bufferView': 3, 'componentType': 5125, 'count': indices, 'type': 'SCALAR'}],
        'images': [{'uri': 'atlas.jpg'}], 'textures': [{'source': 0}],
        'materials': [{'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}, 'metallicFactor': 0, 'roughnessFactor': 1}}],
        'meshes': [{'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1, 'TEXCOORD_0': 2}, 'indices': 3, 'material': 0}]}],
        'nodes': [{'mesh': 0}], 'scenes': [{'nodes': [0]}], 'scene': 0}
    (folder/'geometry.gltf').write_text(json.dumps(geometry, indent=2), encoding='utf-8')
    # A source-derived edge scale will drive successive refinement. It is a
    # tessellation metric, not a fabricated geometric-deviation error bound.
    positions, triangle_indices = array.array('f'), array.array('I')
    with binary.open('rb') as source:
        positions.fromfile(source, vertices*3)
        source.seek(views[3]['byteOffset'])
        triangle_indices.fromfile(source, indices)
    edges = []
    for first in range(0, indices, 3):
        points = [positions[int(i)*3:int(i)*3+3] for i in triangle_indices[first:first+3]]
        edges.append(max(math.dist(points[a], points[b]) for a, b in ((0,1),(1,2),(2,0))))
    edges.sort()
    result = {'schema': 'hongtu.osgb_native_root_bundle.v1', 'tile_id': tile, 'depth': depth,
        'stamp': stamp, 'vertices': vertices, 'indices': indices, 'bounds_min': low, 'bounds_max': high,
        'gpu_payload_bytes': payload, 'atlas_size': atlas_size, 'source_atlas_pages': pages,
        'frontier_count': len(sources), 'complete_frontier': True,
        'exported_source_face_counts': face_counts,
        'native_empty_geometry_sources': sum(count == 0 for count in face_counts.values()),
        'longest_edge_p90_m': edges[min(len(edges)-1, int(len(edges)*0.9))],
        'files': [{'name': name, 'sha256': sha(folder/name), 'bytes': (folder/name).stat().st_size}
                  for name in ('geometry.gltf', 'geometry.bin', 'atlas.jpg')]}
    temporary = folder/'complete.pending.json'
    temporary.write_text(json.dumps(result, indent=2), encoding='utf-8')
    temporary.replace(complete)
    # Only known intermediates emitted into this dedicated folder are removed.
    for path in [obj, obj.with_suffix('.mtl'), source_list, *[folder/name for name in texture_names]]:
        assert path.resolve().is_relative_to(folder.resolve())
        path.unlink(missing_ok=True)
    print(json.dumps({'cached': False, 'bundle': str(complete), 'depth': depth, 'frontier': len(sources),
                      'triangles': indices//3, 'gpu_payload_bytes': payload}), flush=True)
    return result


def convert_locked(tile, depth, atlas_size=1024):
    folder = CACHE/f'tile_{tile}_depth_{depth}_atlas_{atlas_size}'
    try:
        return convert_payload(tile, depth, atlas_size)
    except Exception as error:
        folder.mkdir(parents=True, exist_ok=True)
        (folder/'failure.json').write_text(json.dumps({'reason': str(error)}), encoding='utf-8')
        # Invalidate only the derived output of this failed transaction. The
        # renderer keeps its already uploaded parent/previous tier intact.
        for name in ('geometry.gltf', 'geometry.bin', 'atlas.jpg', 'complete.json', 'complete.pending.json'):
            (folder/name).unlink(missing_ok=True)
        raise
    finally:
        # Even a rejected oversized frontier must not accumulate multi-hundred
        # megabyte OBJ exports. Remove only this worker's known intermediates.
        if folder.exists():
            assert folder.resolve().is_relative_to(CACHE.resolve())
            for path in folder.iterdir():
                if path.name in {'frontier.obj', 'frontier.mtl', 'source_list.txt'} or re.fullmatch(r'texture_\d+\.rgb', path.name):
                    path.unlink(missing_ok=True)
        prune_cache(folder)


def convert(tile, depth, atlas_size=1024):
    # Multiple demo windows still share one cache. OS-owned locking is released
    # automatically if a decoder exits, and prevents concurrent writes/pruning.
    import msvcrt
    CACHE.mkdir(parents=True, exist_ok=True)
    with (CACHE/'decoder.lock').open('a+b') as lock:
        lock.seek(0)
        if not lock.read(1):
            lock.write(b'0')
            lock.flush()
        deadline = time.monotonic()+100
        while True:
            try:
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
                break
            except OSError:
                if time.monotonic() > deadline:
                    raise RuntimeError('native decoder cache lock deadline')
                time.sleep(.05)
        try:
            return convert_locked(tile, depth, atlas_size)
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tile', type=int, required=True)
    parser.add_argument('--depth', type=int, required=True)
    parser.add_argument('--atlas-size', type=int, choices=(1024, 2048), default=1024)
    args = parser.parse_args()
    assert 1 <= args.tile <= 2346 and 0 <= args.depth <= 32
    sys.path.insert(0, str(ROOT/'work/geospatial_deps'))
    convert(args.tile, args.depth, args.atlas_size)
