#!/usr/bin/env python3
"""Build a static industrial-area child replacement, keeping all base roots.

Base index spans come from the exact audited OBJ exports used for the base
binary. Replacement is allowed only with the complete referenced child set.
"""
import argparse
import json
import math
import os
import re
from pathlib import Path

import build_zhumadian_full_stage as full
import build_zhumadian_stage_asset as base

NAME = re.compile(r"Tile_[+-]\d+_[+-]\d+_L\d+_0u")


def partition(roots, objects):
    records, index_offset = [], 0
    for start, obj in zip(range(0, len(roots), full.BATCH), objects):
        names = {}
        for root in roots[start:start + full.BATCH]:
            found = set(NAME.findall(root.path.read_bytes().decode('latin1')))
            if len(found) != 1:
                base.fail('ambiguous source root name')
            names[next(iter(found))] = root.tile_id
        positions, current = [], None
        entries = {}
        for line in full.text_lines(obj):
            fields = line.split()
            if not fields:
                continue
            if fields[0] == 'v':
                positions.append(tuple(map(float, fields[1:4])))
            elif fields[0] == 'g':
                match = NAME.search(line)
                current = names.get(match.group()) if match else None
            elif fields[0] == 'f':
                if current is None or len(fields) != 4:
                    base.fail('unattributed/non-triangle base geometry')
                entry = entries.setdefault(current, {'tile_id': current, 'spans': [],
                    'bounds_min': [math.inf]*3, 'bounds_max': [-math.inf]*3})
                spans = entry['spans']
                if spans and sum(spans[-1]) == index_offset:
                    spans[-1][1] += 3
                else:
                    spans.append([index_offset, 3])
                for token in fields[1:]:
                    p = positions[int(token.split('/')[0])-1]
                    entry['bounds_min'] = [min(a,b) for a,b in zip(entry['bounds_min'], p)]
                    entry['bounds_max'] = [max(a,b) for a,b in zip(entry['bounds_max'], p)]
                index_offset += 3
        if set(entries) != set(names.values()):
            base.fail('missing base roots in partition')
        records.extend(entries.values())
    return records, index_offset


def children_at_depth(parent, depth):
    frontier = [parent]
    for _ in range(depth):
        next_level = []
        for path in frontier:
            names = sorted(set(re.findall(rb'[A-Za-z0-9_]+\.osgb', path.read_bytes())))
            if not names:
                base.fail(f'no child references: {path}')
            for name in names:
                child = path.parent / name.decode('ascii')
                if not child.is_file():
                    base.fail(f'missing child: {child}')
                next_level.append(child)
        frontier = next_level
    return frontier


def audit_children(paths, obj):
    pattern = re.compile(r'Tile_[+-]\d+_[+-]\d+_L\d+_0u+')
    expected = set()
    for path in paths:
        names = set(pattern.findall(path.read_bytes().decode('latin1')))
        if len(names) != 1:
            base.fail(f'ambiguous child geometry: {path}')
        expected.update(names)
    counts, current = {}, None
    for line in full.text_lines(obj):
        if line.startswith('g '):
            match = pattern.search(line)
            current = match.group() if match else None
        elif line.startswith('f '):
            if current is None:
                base.fail('unattributed child faces')
            counts[current] = counts.get(current, 0)+1
    if set(counts) != expected or len(expected) != len(paths):
        base.fail('incomplete/duplicated child geometry export')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--profile', choices=('center','foreground'), default='center')
    args = parser.parse_args()
    root = base.repo_root()
    foreground = args.profile == 'foreground'
    tier = 'geometry_foreground' if foreground else 'geometry_detail'
    output = root / 'assets/showcase/runtime/zhumadian' / tier
    manifest_path = root / 'data/showcase' / f'zhumadian_{tier}.json'
    center, radius, depth = ((350,-1850),650,3) if foreground else ((500,-1600),300,2)
    base_manifest = root / 'data/showcase/zhumadian_full_stage_manifest.json'
    if args.build:
        source = root / 'assets/驻马店园区'
        roots = full.roots_from(source)
        objects = [root / 'work/zmd_full_stage_convert' / f'batch_{i:04d}/roots.obj'
                   for i in range(0, len(roots), full.BATCH)]
        full.audit_root_geometry(roots, objects)
        for start,obj in zip(range(0,len(roots),full.BATCH),objects):
            stamp = json.loads((obj.parent/'sources.json').read_text(encoding='utf-8'))
            if stamp != full.source_records(root,roots[start:start+full.BATCH]):
                base.fail('cached base export provenance mismatch')
        entries, count = partition(roots, objects)
        expected = json.loads(base_manifest.read_text(encoding='utf-8'))
        if count != expected['triangles']*3:
            base.fail('base partition index count mismatch')
        by_id = {r.tile_id:r for r in roots}
        selected = []
        for entry in entries:
            lo, hi = entry['bounds_min'], entry['bounds_max']
            center = [(a+b)*.5 for a,b in zip(lo,hi)]
            focus = (350,-1850) if foreground else (500,-1600)
            if math.hypot(center[0]-focus[0], center[1]-focus[1]) <= radius:
                selected.append(entry)
        if not selected:
            base.fail('no industrial-area roots')
        child_paths = []
        for entry in selected:
            parent = by_id[entry['tile_id']].path
            children = children_at_depth(parent, depth)
            entry['parent'] = base.manifest_path(root,parent)
            entry['parent_sha256'] = base.sha256(parent)
            entry['children'] = [{'path':base.manifest_path(root,p),'sha256':base.sha256(p)} for p in children]
            child_paths.extend(children)
        print(f'refining {len(selected)} roots with {len(child_paths)} depth-{depth} children', flush=True)
        defaults = base.parse_args([])
        osg = base.resolve_path(root, defaults.osgconv)
        env = os.environ.copy()
        env['PATH'] = str(osg.parent)+os.pathsep+env.get('PATH','')
        env['OSG_LIBRARY_PATH'] = str(base.resolve_path(root, defaults.osg_plugin_dir))
        env['OSG_OPTIMIZER'] = 'DEFAULT TEXTURE_ATLAS_BUILDER'
        env['OSG_NOTIFY_LEVEL'] = 'WARN'
        alias = base.ensure_ascii_source_alias(source, root/'work/zmd_source')
        work = root/'work'/('zmd_foreground_convert' if foreground else 'zmd_geometry_detail_convert')
        work.mkdir(parents=True, exist_ok=True)
        obj = work/'detail.obj'
        if foreground:
            exporter=root/'work/osgb_exporter_build/Release/ht_osgb_exporter.exe'
            source_list=work/'source_list.txt'
            source_list.write_text(''.join(str(alias/p.relative_to(source))+'\n' for p in child_paths),encoding='utf-8')
            base.run_hidden([str(exporter),str(source_list),str(obj)],work,env,'local-image industrial child export')
        else:
            base.run_hidden([str(osg),'-O','OutputTextureFiles',
                *[str(alias/p.relative_to(source)) for p in child_paths], str(obj)],work,env,'industrial child export')
        audit_children(child_paths,obj)
        if foreground:
            material= obj.with_suffix('.mtl').read_text(encoding='utf-8')
            texture_names=re.findall(r'map_Kd\s+([^\r\n]+)',material)
            if not texture_names or any(Path(p).is_absolute() or ':' in p or '..' in Path(p).parts for p in texture_names):
                base.fail('exported texture path escapes the local output directory')
        output.mkdir(parents=True, exist_ok=True)
        full.SIZE = 8192
        mapping, pages = full.pack_atlas([obj], output/'detail_atlas.jpg')
        views, vertices, indices, lo, hi, _ = full.write_geometry([obj],mapping,output/'detail.bin')
        doc = {'asset':{'version':'2.0'}, 'buffers':[{'uri':'detail.bin','byteLength':(output/'detail.bin').stat().st_size}],
            'bufferViews':views, 'accessors':[
                {'bufferView':0,'componentType':5126,'count':vertices,'type':'VEC3','min':lo,'max':hi},
                {'bufferView':1,'componentType':5126,'count':vertices,'type':'VEC3'},
                {'bufferView':2,'componentType':5126,'count':vertices,'type':'VEC2'},
                {'bufferView':3,'componentType':5125,'count':indices,'type':'SCALAR'}],
            'images':[{'uri':'detail_atlas.jpg'}],'textures':[{'source':0}],
            'materials':[{'pbrMetallicRoughness':{'baseColorTexture':{'index':0},'metallicFactor':0,'roughnessFactor':1}}],
            'meshes':[{'primitives':[{'attributes':{'POSITION':0,'NORMAL':1,'TEXCOORD_0':2},'indices':3,'material':0}]}],
            'nodes':[{'mesh':0}],'scenes':[{'nodes':[0]}],'scene':0}
        (output/'detail.gltf').write_text(json.dumps(doc,indent=2),encoding='utf-8')
        removed = sum(n for e in selected for _,n in e['spans'])
        manifest = {'schema':'hongtu.zhumadian_static_geometry_detail.v1',
            'base_manifest_sha256':base.sha256(base_manifest),'base_root_count':len(roots),
            'base_indices':count,'replacements':selected,'depth':depth,'static_refinement':True,
            'profile':args.profile,'selection_center':list(focus),'selection_radius':radius,
            'base_exports':[{'path':base.manifest_path(root,p),'sha256':base.sha256(p)} for p in objects],
            'child_export':{'path':base.manifest_path(root,obj),'sha256':base.sha256(obj)},
            'recursive_streaming':False,'vertices':vertices,'triangles':indices//3,
            'removed_base_triangles':removed//3,'rendered_triangles':(count-removed+indices)//3,
            'source_atlas_pages':pages,'bounds_min':lo,'bounds_max':hi,
            'files':[{'name':p.name,'bytes':p.stat().st_size,'sha256':base.sha256(p)}
                     for p in sorted(output.iterdir()) if p.is_file()]}
        manifest_path.write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
        if foreground:
            import build_zhumadian_texture_pages as texture_pages
            from PIL import Image
            bounds = texture_pages.page_bounds([obj])
            images = list(dict.fromkeys(full.material_images(obj).values()))
            page_output = output/'pages'
            page_output.mkdir(parents=True,exist_ok=True)
            page_records = []
            for index,path in enumerate(images):
                destination = page_output/f'page_{index:03d}.jpg'
                with Image.open(path) as source_image:
                    width,height = source_image.size
                    source_image.convert('RGB').save(destination,quality=96,subsampling=0)
                page_records.append({'id':index,'path':base.manifest_path(root,destination),
                    'sha256':base.sha256(destination),'bytes':destination.stat().st_size,
                    'source':base.manifest_path(root,path),'source_sha256':base.sha256(path),
                    'size':[width,height], 'atlas_uv_rect':texture_pages.atlas_rect(index,len(images),width,height),
                    'bounds_min':bounds[path][0],'bounds_max':bounds[path][1]})
            (root/'data/showcase/zhumadian_foreground_texture_pages.json').write_text(json.dumps({
                'schema':'hongtu.zhumadian_child_texture_pages.v1','geometry_manifest_sha256':base.sha256(manifest_path),
                'resident_page_limit':4,'pages':page_records},indent=2),encoding='utf-8')
    doc = json.loads(manifest_path.read_text(encoding='utf-8'))
    if doc['base_manifest_sha256'] != base.sha256(base_manifest):
        base.fail('base manifest changed')
    for entry in doc['base_exports']+[doc['child_export']]:
        if base.sha256(root/entry['path']) != entry['sha256']:
            base.fail('source export changed')
    spans = sorted(span for e in doc['replacements'] for span in e['spans'])
    last = 0
    for start,count in spans:
        if start < last or start%3 or count%3 or start+count > doc['base_indices']:
            base.fail('overlapping/invalid replacement index ranges')
        last = start+count
    for entry in doc['replacements']:
        if base.sha256(root/entry['parent']) != entry['parent_sha256']:
            base.fail('parent hash changed')
        actual = set(base.manifest_path(root,p) for p in children_at_depth(root/entry['parent'],doc['depth']))
        if actual != {c['path'] for c in entry['children']}:
            base.fail('incomplete child coverage')
        for child in entry['children']:
            if base.sha256(root/child['path']) != child['sha256']:
                base.fail('child hash changed')
    for entry in doc['files']:
        p = output/entry['name']
        if p.stat().st_size != entry['bytes'] or base.sha256(p) != entry['sha256']:
            base.fail('runtime hash changed')
    audit_children([root/c['path'] for e in doc['replacements'] for c in e['children']],
                   root/doc['child_export']['path'])
    if foreground:
        catalog=json.loads((root/'data/showcase/zhumadian_foreground_texture_pages.json').read_text(encoding='utf-8'))
        if catalog['geometry_manifest_sha256']!=base.sha256(manifest_path) or len(catalog['pages'])!=doc['source_atlas_pages']:
            base.fail('foreground texture catalog mismatch')
        for page in catalog['pages']:
            if base.sha256(root/page['path'])!=page['sha256'] or base.sha256(root/page['source'])!=page['source_sha256']:
                base.fail('foreground page hash mismatch')
            if base.jpeg_dimensions(root/page['path'])!=tuple(page['size']):
                base.fail('foreground native page dimensions mismatch')
        if [p['id'] for p in catalog['pages']]!=list(range(len(catalog['pages']))):
            base.fail('foreground native page IDs must be contiguous')
    print(f"Geometry detail PASS: {len(doc['replacements'])} roots, {doc['triangles']} child triangles; "
          f"{doc['rendered_triangles']} total triangles, complete base context retained")


if __name__ == '__main__':
    main()
