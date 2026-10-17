#!/usr/bin/env python3
"""Split audited foreground geometry into independent root payloads for paging."""
import argparse
import array
import json
import math
import re
import struct
from pathlib import Path
import build_zhumadian_stage_asset as base


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',action='store_true')
    args=parser.parse_args()
    root=base.repo_root()
    source_manifest=root/'data/showcase/zhumadian_geometry_foreground.json'
    source=json.loads(source_manifest.read_text(encoding='utf-8'))
    manifest_path=root/'data/showcase/zhumadian_geometry_chunks.json'
    output=root/'assets/showcase/runtime/zhumadian/geometry_chunks'
    if args.build:
        names={}
        pattern=re.compile(r'Tile_[+-]\d+_[+-]\d+_L\d+_0[0-3u]+')
        for entry in source['replacements']:
            for child in entry['children']:
                path=root/child['path']
                if base.sha256(path)!=child['sha256']:
                    base.fail('child source changed')
                found=set(pattern.findall(path.read_bytes().decode('latin1')))
                if len(found)!=1 or next(iter(found)) in names:
                    base.fail('ambiguous child name')
                names[next(iter(found))]=entry['tile_id']
        obj=root/source['child_export']['path']
        if base.sha256(obj)!=source['child_export']['sha256']:
            base.fail('child export changed')
        spans,current,offset={},None,0
        with obj.open(encoding='utf-8') as lines:
            for line in lines:
                if line.startswith('g '):
                    match=pattern.search(line)
                    current=names.get(match.group()) if match else None
                elif line.startswith('f '):
                    if current is None or len(line.split())!=4:
                        base.fail('unattributed child triangle')
                    ranges=spans.setdefault(current,[])
                    if ranges and sum(ranges[-1])==offset:
                        ranges[-1][1]+=3
                    else:
                        ranges.append([offset,3])
                    offset+=3
        if offset!=source['triangles']*3 or set(spans)!={e['tile_id'] for e in source['replacements']}:
            base.fail('chunk face coverage differs from full foreground')
        directory=root/'assets/showcase/runtime/zhumadian/geometry_foreground'
        doc=json.loads((directory/'detail.gltf').read_text(encoding='utf-8'))
        binary=(directory/'detail.bin').read_bytes()
        primitive=doc['meshes'][0]['primitives'][0]
        def accessor_data(index,kind):
            accessor=doc['accessors'][index]
            view=doc['bufferViews'][accessor['bufferView']]
            data=array.array(kind)
            start=view.get('byteOffset',0)+accessor.get('byteOffset',0)
            data.frombytes(binary[start:start+view['byteLength']])
            return data
        positions=accessor_data(primitive['attributes']['POSITION'],'f')
        normals=accessor_data(primitive['attributes']['NORMAL'],'f')
        uvs=accessor_data(primitive['attributes']['TEXCOORD_0'],'f')
        indices=accessor_data(primitive['indices'],'I')
        output.mkdir(parents=True,exist_ok=True)
        records=[]
        for entry in source['replacements']:
            tile=entry['tile_id']
            remap,vertices,out_indices={},array.array('f'),array.array('I')
            low,high=[math.inf]*3,[-math.inf]*3
            for start,count in spans[tile]:
                for i in indices[start:start+count]:
                    if i not in remap:
                        remap[i]=len(remap)
                        p=positions[i*3:i*3+3]
                        vertices.extend(p)
                        vertices.extend(normals[i*3:i*3+3])
                        vertices.extend(uvs[i*2:i*2+2])
                        low=[min(a,b) for a,b in zip(low,p)]
                        high=[max(a,b) for a,b in zip(high,p)]
                    out_indices.append(remap[i])
            path=output/f'tile_{tile}.bin'
            with path.open('wb') as target:
                target.write(struct.pack('<4sIII',b'HGC1',len(remap),len(out_indices),tile))
                vertices.tofile(target)
                out_indices.tofile(target)
            records.append({'tile_id':tile,'path':base.manifest_path(root,path),
                'bytes':path.stat().st_size,'sha256':base.sha256(path),
                'vertices':len(remap),'indices':len(out_indices),
                'gpu_payload_bytes':len(remap)*64+len(out_indices)*4,
                'bounds_min':low,'bounds_max':high,'base_spans':entry['spans'],
                'draw_order':spans[tile][0][0]})
        manifest={'schema':'hongtu.geometry_chunks.v1','source_manifest_sha256':base.sha256(source_manifest),
            'base_root_count':2346,'base_indices':9166851,'chunks':records,
            'resident_limit':96,'gpu_budget_bytes':192*1024*1024,'upload_limit_bytes':16*1024*1024,
            'source_triangles':source['triangles'],'source_vertices':source['vertices']}
        manifest_path.write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
    if manifest['source_manifest_sha256']!=base.sha256(source_manifest):
        base.fail('chunk source manifest changed')
    if len(manifest['chunks'])!=len(source['replacements']):
        base.fail('missing root chunk')
    total=0
    for entry in manifest['chunks']:
        path=root/entry['path']
        data=path.read_bytes()
        if len(data)!=entry['bytes'] or base.sha256(path)!=entry['sha256']:
            base.fail('chunk payload changed')
        magic,vertices,count,tile=struct.unpack_from('<4sIII',data)
        if (magic,vertices,count,tile)!=(b'HGC1',entry['vertices'],entry['indices'],entry['tile_id']):
            base.fail('chunk header differs from manifest')
        if len(data)!=16+vertices*32+count*4 or count%3:
            base.fail('chunk size/triangle mismatch')
        values=array.array('I'); values.frombytes(data[16+vertices*32:])
        if values and max(values)>=vertices:
            base.fail('chunk index outside vertex bounds')
        total+=count
    if total!=source['triangles']*3:
        base.fail('chunk triangle sum differs from foreground')
    print('GEOMETRY CHUNKS PASS',len(manifest['chunks']),'roots;',total//3,'triangles;',
          'max GPU chunk',max(e['gpu_payload_bytes'] for e in manifest['chunks']))


if __name__=='__main__':
    main()
