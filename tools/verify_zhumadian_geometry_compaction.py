#!/usr/bin/env python3
"""Verify byte-identical Vulkan images and smaller real draw-buffer payloads."""
import argparse
import hashlib
import json
from pathlib import Path

from run_showcase_demo import run_showcase_process


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--demo',required=True)
    parser.add_argument('--output-dir',required=True)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    output=Path(args.output_dir).resolve()
    output.mkdir(parents=True,exist_ok=True)
    results={}
    for profile in ('base','refined','foreground'):
        stats={}
        hashes={}
        for mode in ('legacy','compact'):
            name=f'{profile}_{mode}'
            image,telemetry=output/f'{name}.png',output/f'{name}.json'
            run_showcase_process(Path(args.demo).resolve(),[
                '--preset=zhumadian','--zhumadian-detail=full','--zhumadian-view=plant',
                f'--zhumadian-geometry={profile}',f'--zhumadian-geometry-buffer={mode}',
                '--scenario-time=0','--frames=3','--width=1440','--height=900',
                f'--screenshot={image}',f'--stats={telemetry}'],root,timeout=90,fallback_root=root)
            doc=json.loads(telemetry.read_text(encoding='utf-8'))
            assert doc['zhumadian_stage_rendered_root_count']==2346
            assert doc['scene_vertex_buffer_bytes']==doc['vertices']*doc['scene_vertex_stride_bytes']
            assert doc['scene_index_buffer_bytes']==doc['indices']*4
            assert doc['scene_vertex_capacity_bytes']==doc['scene_vertex_buffer_bytes']
            assert doc['zhumadian_geometry_compacted']==(mode=='compact' and profile!='base')
            stats[mode]=doc
            hashes[mode]=hashlib.sha256(image.read_bytes()).hexdigest()
        old,new=stats['legacy'],stats['compact']
        assert hashes['legacy']==hashes['compact'],(profile,hashes)
        assert old['indices']==new['indices']
        assert old['zhumadian_geometry_rendered_triangles']==new['zhumadian_geometry_rendered_triangles']
        removed=new['zhumadian_geometry_removed_unreferenced_vertices']
        saved=old['scene_vertex_buffer_bytes']-new['scene_vertex_buffer_bytes']
        assert old['vertices']-new['vertices']==removed
        assert saved==removed*new['scene_vertex_stride_bytes']
        if profile=='base':
            assert removed==0 and saved==0
        else:
            assert removed>100000 and saved>6*1024*1024
        results[profile]={'image_sha256':hashes['compact'],'removed_vertices':removed,
                         'vertex_buffer_bytes_before':old['scene_vertex_buffer_bytes'],
                         'vertex_buffer_bytes_after':new['scene_vertex_buffer_bytes'],
                         'vertex_buffer_bytes_saved':saved,'indices':new['indices']}
    (output/'metrics.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    print('GEOMETRY COMPACTION / PIXEL PARITY PASS',results)


if __name__=='__main__':
    main()
