"""Convert a complete real OSGB frontier into bounded upload parts; never omit siblings."""
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

import convert_zhumadian_native_root as native

ROOT = native.ROOT
CACHE = ROOT/'work/zhumadian_native_chunk_cache'
PART_LIMIT = 32*1024*1024
ROOT_LIMIT = 192*1024*1024


def groups(sources, byte_limit=4*1024*1024, node_limit=12):
    """The initial estimate is conservative; actual exported payload can split again."""
    result, current, amount = [], [], 0
    for source in sources:
        if current and (amount+source['bytes'] > byte_limit or len(current) >= node_limit):
            result.append(current)
            current, amount = [], 0
        current.append(source)
        amount += source['bytes']
    if current:
        result.append(current)
    assert [r['name'] for group in result for r in group] == [r['name'] for r in sources]
    return result


def clean(folder, remove_outputs=True):
    assert folder.resolve().is_relative_to(CACHE.resolve()) and not folder.is_symlink()
    allowed = {'source_list.txt', 'source_triangles.tsv', 'frontier.obj', 'frontier.mtl'}
    if remove_outputs:
        allowed |= {'geometry.bin', 'geometry.gltf', 'atlas.jpg'}
    for path in folder.iterdir():
        if path.is_file() and not path.is_symlink() and (
                path.name in allowed or re.fullmatch(r'texture_\d+\.rgb', path.name)):
            path.unlink()


def prune(protected):
    folders = [p for p in CACHE.iterdir() if p.is_dir() and not p.is_symlink() and
               re.fullmatch(r'tile_\d+_depth_\d+', p.name)]
    records = []
    for folder in folders:
        files = [p for p in folder.rglob('*') if p.is_file() and not p.is_symlink()]
        records.append((max((p.stat().st_mtime for p in files), default=0), folder,
                        sum(p.stat().st_size for p in files)))
    size = sum(r[2] for r in records)
    for _, folder, amount in sorted(records):
        if size <= 1024*1024*1024:
            break
        if folder == protected:
            continue
        assert folder.resolve().is_relative_to(CACHE.resolve())
        for part in folder.iterdir():
            if part.is_dir() and not part.is_symlink() and re.fullmatch(r'part_\d{3}', part.name):
                clean(part)
                if not any(part.iterdir()):
                    part.rmdir()
            elif part.is_file() and not part.is_symlink() and part.name in {
                    'complete.json', 'complete.pending.json', 'rejected.json', 'failure.json'}:
                part.unlink()
        remaining = sum(p.stat().st_size for p in folder.rglob('*') if p.is_file())
        if not any(folder.iterdir()):
            folder.rmdir()
        size -= amount-remaining


def build_part(folder, sources, source_directory, env, alias):
    folder.mkdir(parents=True, exist_ok=True)
    clean(folder)
    paths = [source_directory/record['name'] for record in sources]
    source_list = folder/'source_list.txt'
    source_list.write_text(''.join(str(alias/path.relative_to((ROOT/'assets/驻马店园区').resolve()))+'\n'
                                   for path in paths), encoding='utf-8')
    obj = folder/'frontier.obj'
    audit = folder/'source_triangles.tsv'
    try:
        native.base.run_hidden([str(ROOT/'work/osgb_exporter_build/Release/ht_osgb_exporter.exe'),
                                str(source_list), str(obj), str(audit)], folder, env, 'native frontier part')
        counts = native.audit_frontier(paths, obj, audit)
        native.full.SIZE = 1024
        mapping, pages = native.full.pack_atlas([obj], folder/'atlas.jpg')
        views, vertices, indices, low, high, _ = native.full.write_geometry([obj], mapping, folder/'geometry.bin')
        texture_bytes = sum((1024//(2**i))**2*4 for i in range(11))
        payload = vertices*64+indices*4+texture_bytes
        if payload > PART_LIMIT:
            raise ValueError(f'part upload budget: {payload} > {PART_LIMIT}')
        assert indices > 0 and indices//3 == sum(counts.values())
        gltf = {'asset': {'version': '2.0', 'generator': 'HongTu bounded complete native frontier part'},
            'buffers': [{'uri': 'geometry.bin', 'byteLength': (folder/'geometry.bin').stat().st_size}],
            'bufferViews': views, 'accessors': [
                {'bufferView': 0, 'componentType': 5126, 'count': vertices, 'type': 'VEC3', 'min': low, 'max': high},
                {'bufferView': 1, 'componentType': 5126, 'count': vertices, 'type': 'VEC3'},
                {'bufferView': 2, 'componentType': 5126, 'count': vertices, 'type': 'VEC2'},
                {'bufferView': 3, 'componentType': 5125, 'count': indices, 'type': 'SCALAR'}],
            'images': [{'uri': 'atlas.jpg'}], 'textures': [{'source': 0}],
            'materials': [{'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}, 'metallicFactor': 0,
                                                  'roughnessFactor': 1}}],
            'meshes': [{'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1, 'TEXCOORD_0': 2},
                                        'indices': 3, 'material': 0}]}],
            'nodes': [{'mesh': 0}], 'scenes': [{'nodes': [0]}], 'scene': 0}
        (folder/'geometry.gltf').write_text(json.dumps(gltf), encoding='utf-8')
        positions, triangles = array.array('f'), array.array('I')
        with (folder/'geometry.bin').open('rb') as source:
            positions.fromfile(source, vertices*3)
            source.seek(views[3]['byteOffset'])
            triangles.fromfile(source, indices)
        edges = []
        for first in range(0, indices, 3):
            points = [positions[int(i)*3:int(i)*3+3] for i in triangles[first:first+3]]
            edges.append(max(math.dist(points[a], points[b]) for a, b in ((0, 1), (1, 2), (2, 0))))
        record = {'directory': folder.name, 'vertices': vertices, 'indices': indices,
            'gpu_payload_bytes': payload, 'atlas_size': 1024, 'source_atlas_pages': pages,
            'source_names': [r['name'] for r in sources], 'source_face_counts': counts,
            'files': [{'name': name, 'bytes': (folder/name).stat().st_size, 'sha256': native.sha(folder/name)}
                      for name in ('geometry.gltf', 'geometry.bin', 'atlas.jpg')]}
        return record, edges
    finally:
        clean(folder, remove_outputs=False)


def convert_locked(tile, depth):
    master = json.loads((ROOT/'data/showcase/zhumadian_native_hierarchy.json').read_text(encoding='utf-8'))
    root_record = next(r for r in master['root_records'] if r['tile_id'] == tile)
    catalog_path = ROOT/root_record['catalog']
    assert native.sha(catalog_path) == root_record['sha256']
    catalog = json.loads(catalog_path.read_text(encoding='utf-8'))
    assert depth <= catalog['max_descendant_depth']
    sources = native.frontier(catalog, depth)
    source_directory = (ROOT/catalog['source_directory']).resolve()
    assert source_directory.is_relative_to((ROOT/'assets/驻马店园区').resolve())
    assert sources and sum(r['bytes'] for r in sources) <= 64*1024*1024
    for record in sources:
        path = source_directory/record['name']
        assert path.parent == source_directory and native.sha(path) == record['sha256'], 'source changed'
    folder = CACHE/f'tile_{tile}_depth_{depth}'
    folder.mkdir(parents=True, exist_ok=True)
    stamp = {'catalog_sha256': root_record['sha256'], 'depth': depth, 'frontier': sources,
        'converter_sha256': native.sha(Path(__file__)), 'native_helper_sha256': native.sha(Path(native.__file__)),
        'packer_sha256': native.sha(Path(native.full.__file__)),
        'exporter_sha256': native.sha(ROOT/'work/osgb_exporter_build/Release/ht_osgb_exporter.exe')}
    marker = folder/'complete.json'
    rejected = folder/'rejected.json'
    if rejected.exists():
        previous = json.loads(rejected.read_text(encoding='utf-8'))
        if previous['stamp'] == stamp:
            raise ValueError(previous['reason'])
    if marker.exists():
        complete = json.loads(marker.read_text(encoding='utf-8'))
        if complete['stamp'] == stamp and all(
                re.fullmatch(r'part_\d{3}', p['directory']) and
                {f['name'] for f in p['files']} == {'geometry.gltf', 'geometry.bin', 'atlas.jpg'} and
                all((folder/p['directory']/f['name']).is_file() and
                    native.sha(folder/p['directory']/f['name']) == f['sha256'] for f in p['files'])
                for p in complete['parts']):
            marker.touch()
            print(json.dumps({'cached': True, 'bundle': str(marker)}), flush=True)
            return complete
    marker.unlink(missing_ok=True)
    defaults = native.base.parse_args([])
    osg = native.base.resolve_path(ROOT, defaults.osgconv)
    env = os.environ.copy()
    env['PATH'] = str(osg.parent)+os.pathsep+env.get('PATH', '')
    env['OSG_LIBRARY_PATH'] = str(native.base.resolve_path(ROOT, defaults.osg_plugin_dir))
    env['OSG_NOTIFY_LEVEL'] = 'WARN'
    alias = native.base.ensure_ascii_source_alias(ROOT/'assets/驻马店园区', ROOT/'work/zmd_source')
    pending = groups(sources)
    parts, edges, payload = [], [], 0
    try:
        while pending:
            group = pending.pop(0)
            part_folder = folder/f'part_{len(parts):03d}'
            try:
                part, sample = build_part(part_folder, group, source_directory, env, alias)
            except (ValueError, native.base.StageAssetError) as error:
                if len(group) <= 1 or 'budget' not in str(error):
                    raise
                clean(part_folder)
                middle = len(group)//2
                pending[0:0] = [group[:middle], group[middle:]]
                continue
            payload += part['gpu_payload_bytes']
            if payload > ROOT_LIMIT or len(parts) >= 64:
                raise ValueError('complete root frontier exceeds native residency budget')
            parts.append(part)
            edges.extend(sample)
            print(f'part {len(parts)}: {part["indices"]//3} triangles, {part["gpu_payload_bytes"]} bytes', flush=True)
        assert [name for part in parts for name in part['source_names']] == [r['name'] for r in sources]
        assert len(set(name for part in parts for name in part['source_names'])) == len(sources)
        edges.sort()
        result = {'schema': 'hongtu.osgb_native_chunk_bundle.v1', 'tile_id': tile, 'depth': depth,
            'stamp': stamp, 'parts': parts, 'complete_frontier': True, 'frontier_count': len(sources),
            'gpu_payload_bytes': payload, 'indices': sum(p['indices'] for p in parts),
            'longest_edge_p90_m': edges[min(len(edges)-1, int(len(edges)*.9))]}
        temporary = folder/'complete.pending.json'
        temporary.write_text(json.dumps(result, indent=2), encoding='utf-8')
        temporary.replace(marker)
        rejected.unlink(missing_ok=True)
        print(json.dumps({'bundle': str(marker), 'parts': len(parts), 'payload_bytes': payload}), flush=True)
        return result
    except Exception as error:
        (folder/'failure.json').write_text(json.dumps({'reason': str(error)}), encoding='utf-8')
        if 'budget' in str(error):
            rejected.write_text(json.dumps({'stamp': stamp, 'reason': str(error)}), encoding='utf-8')
        raise
    finally:
        prune(folder)


def convert(tile, depth):
    import msvcrt
    CACHE.mkdir(parents=True, exist_ok=True)
    with (CACHE/'decoder.lock').open('a+b') as lock:
        # Windows byte locks also reject reads by another process. Inspect the
        # file length without reading the locked byte, then acquire ownership.
        lock.seek(0, os.SEEK_END)
        if lock.tell() == 0:
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
                    raise RuntimeError('native chunk cache lock deadline')
                time.sleep(.05)
        try:
            return convert_locked(tile, depth)
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tile', type=int, required=True)
    parser.add_argument('--depth', type=int, required=True)
    args = parser.parse_args()
    assert 1 <= args.tile <= 2346 and 0 <= args.depth <= 32
    sys.path.insert(0, str(ROOT/'work/geospatial_deps'))
    convert(args.tile, args.depth)
