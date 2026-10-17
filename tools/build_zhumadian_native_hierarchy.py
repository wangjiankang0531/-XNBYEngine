#!/usr/bin/env python3
"""Index the actual OSGB forest for camera-driven recursive decoding.

Per-root catalogs are lazy-loadable. No geometry bounds/error are invented for
children: decoded geometry must provide them. The complete audited base stays
the fallback for missing or incomplete descendant bundles.
"""
import argparse
import concurrent.futures
import hashlib
import json
import re
from pathlib import Path
import build_zhumadian_full_stage as full
import build_zhumadian_geometry_detail as detail
import build_zhumadian_stage_asset as base

ROOT = base.repo_root()
SOURCE = ROOT/'assets/驻马店园区'
OUTPUT = ROOT/'assets/showcase/runtime/zhumadian/native_hierarchy'
MANIFEST = ROOT/'data/showcase/zhumadian_native_hierarchy.json'
NAME = re.compile(r'Tile_(\d+)_L(\d+)_([0-9u]+)\.osgb$')
WRAPPER = re.compile(r'Tile_(\d+)\.osgb$')
REFERENCES = re.compile(rb'[A-Za-z0-9_]+\.osgb')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def index_root(root):
    nodes = {}
    total_bytes = 0
    for path in sorted(root.path.parent.glob('*.osgb')):
        match = NAME.fullmatch(path.name)
        wrapper = WRAPPER.fullmatch(path.name)
        if not (match or wrapper) or int((match or wrapper)[1]) != root.tile_id:
            raise RuntimeError(f'Unexpected hierarchy filename: {path}')
        data = path.read_bytes()
        children = sorted({name.decode('ascii') for name in REFERENCES.findall(data)})
        for name in children:
            if not (NAME.fullmatch(name) or WRAPPER.fullmatch(name)) or not (path.parent/name).is_file():
                raise RuntimeError(f'Missing/invalid referenced source: {path.name} -> {name}')
        nodes[path.name] = {'name': path.name, 'level': int(match[2]) if match else None,
            'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(), 'children': children}
        total_bytes += len(data)
    reached, active, levels = set(), set(), {}

    def visit(name):
        if name in active:
            raise RuntimeError(f'OSGB cycle in root {root.tile_id}: {name}')
        if name in reached:
            return levels[name]
        active.add(name)
        maximum = 0
        for child in nodes[name]['children']:
            maximum = max(maximum, visit(child)+1)
        active.remove(name)
        reached.add(name)
        levels[name] = maximum
        return maximum

    depth = visit(root.path.name)
    unreachable = sorted(set(nodes)-reached)
    # Record unrelated source files explicitly; they are not fabricated children.
    catalog = {'schema': 'hongtu.osgb_root_hierarchy.v1', 'tile_id': root.tile_id,
        'source_directory': base.manifest_path(ROOT, root.path.parent), 'root': root.path.name,
        'max_descendant_depth': depth, 'reachable_nodes': len(reached),
        'unreachable_files': unreachable, 'nodes': [nodes[name] for name in sorted(nodes)],
        'child_bounds_policy': 'unknown_until_native_geometry_decode'}
    path = OUTPUT/f'tile_{root.tile_id}.json'
    path.write_text(json.dumps(catalog, ensure_ascii=False, separators=(',', ':')), encoding='utf-8')
    return {'tile_id': root.tile_id, 'catalog': base.manifest_path(ROOT, path),
        'sha256': sha(path), 'node_count': len(nodes), 'reachable_nodes': len(reached),
        'source_bytes': total_bytes, 'max_depth': depth, 'unreachable_count': len(unreachable)}


def build():
    roots = full.roots_from(SOURCE)
    assert len(roots) == 2346
    # Exact fallback spans/bounds come from the already audited base OBJ order.
    proof = json.loads((ROOT/'data/showcase/zhumadian_geometry_foreground.json').read_text(encoding='utf-8'))
    objects = []
    for record in proof['base_exports']:
        path = ROOT/record['path']
        assert sha(path) == record['sha256'], path
        objects.append(path)
    cache_path = ROOT/'work/zhumadian_native_base_spans.json'
    proof_hash = sha(ROOT/'data/showcase/zhumadian_geometry_foreground.json')
    cache = json.loads(cache_path.read_text(encoding='utf-8')) if cache_path.exists() else {}
    if cache.get('proof_sha256') == proof_hash:
        spans, count = cache['spans'], cache['indices']
    else:
        spans, count = detail.partition(roots, objects)
        cache_path.write_text(json.dumps({'proof_sha256': proof_hash, 'spans': spans, 'indices': count}), encoding='utf-8')
    assert count == 9166851 and len(spans) == 2346
    by_tile = {record['tile_id']: record for record in spans}
    OUTPUT.mkdir(parents=True, exist_ok=True)
    records = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        for number, entry in enumerate(pool.map(index_root, roots), 1):
            entry.update(by_tile[entry['tile_id']])
            entry['base_spans'] = entry.pop('spans')
            records.append(entry)
            if number % 100 == 0 or number == len(roots):
                print('Indexed', number, '/', len(roots), 'actual root hierarchies', flush=True)
    manifest = {'schema': 'hongtu.osgb_native_hierarchy.v1', 'base_root_count': len(roots),
        'base_indices': count, 'root_records': records,
        'total_source_nodes': sum(r['node_count'] for r in records),
        'reachable_source_nodes': sum(r['reachable_nodes'] for r in records),
        'total_source_bytes': sum(r['source_bytes'] for r in records),
        'source_base_manifest_sha256': sha(ROOT/'data/showcase/zhumadian_full_stage_manifest.json'),
        'base_export_proof': proof['base_exports'],
        'policy': 'Original source untouched; complete sibling frontier replaces each base root atomically.',
        'runtime_recursive_streaming': False}
    assert manifest['total_source_nodes'] == 237453
    MANIFEST.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    return manifest


def audit():
    manifest = json.loads(MANIFEST.read_text(encoding='utf-8'))
    base_manifest_path = ROOT/'data/showcase/zhumadian_full_stage_manifest.json'
    assert sha(base_manifest_path) == manifest['source_base_manifest_sha256']
    expected_roots = {r['tile_id']: r for r in json.loads(base_manifest_path.read_text(encoding='utf-8'))['source_roots']}
    assert manifest['base_root_count'] == 2346 and manifest['base_indices'] == 9166851
    assert manifest['total_source_nodes'] == 237453
    spans = []
    for record in manifest['root_records']:
        path = ROOT/record['catalog']
        assert sha(path) == record['sha256'], path
        catalog = json.loads(path.read_text(encoding='utf-8'))
        assert catalog['tile_id'] == record['tile_id'] and len(catalog['nodes']) == record['node_count']
        names = {n['name'] for n in catalog['nodes']}
        native_root = next(n for n in catalog['nodes'] if n['name'] == catalog['root'])
        assert native_root['sha256'] == expected_roots[record['tile_id']]['sha256']
        assert catalog['root'] in names
        assert all(set(n['children']) <= names for n in catalog['nodes'])
        nodes = {n['name']: n for n in catalog['nodes']}
        parent_counts = {}
        for node in nodes.values():
            for child in node['children']:
                parent_counts[child] = parent_counts.get(child, 0)+1
                if node['level'] is not None:
                    assert nodes[child]['level'] > node['level'], 'cyclic/backward native reference'
        assert max(parent_counts.values(), default=0) <= 1, 'overlapping shared-source frontier'
        spans.extend(record['base_spans'])
    end = 0
    for start, count in sorted(spans):
        assert start == end and count > 0 and count % 3 == 0
        end += count
    assert end == manifest['base_indices']
    print('NATIVE HIERARCHY STRUCTURE / ALL ROOT FALLBACK SPANS PASS:',
          manifest['base_root_count'], 'roots,', manifest['total_source_nodes'], 'source nodes,',
          manifest['reachable_source_nodes'], 'reachable; runtime coverage is verified by separate renderer gates')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', action='store_true')
    args = parser.parse_args()
    if args.build:
        build()
    audit()
