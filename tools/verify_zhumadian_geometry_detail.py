#!/usr/bin/env python3
"""Validate actual Vulkan replacement draw counts and before/after captures."""
import argparse
import json
from pathlib import Path
from run_showcase_demo import run_showcase_process
from verify_badaling_demo import read_png_rgb


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--demo',required=True)
    parser.add_argument('--output-dir',required=True)
    parser.add_argument('--profile',choices=('center','foreground'),default='center')
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    output=Path(args.output_dir).resolve()
    output.mkdir(parents=True,exist_ok=True)
    foreground=args.profile=='foreground'
    mode_detail='foreground' if foreground else 'refined'
    tier='geometry_foreground' if foreground else 'geometry_detail'
    manifest=json.loads((root/'data/showcase'/f'zhumadian_{tier}.json').read_text(encoding='utf-8'))
    captures,telemetry={},{}
    for view in ('plant','overview'):
        for mode in ('base',mode_detail):
            name=f'{view}_{mode}'
            image,stats=output/f'{name}.png',output/f'{name}.json'
            command=['--preset=zhumadian','--zhumadian-detail=full',f'--zhumadian-view={view}',
                     f'--zhumadian-geometry={mode}','--frames=3','--width=1440','--height=900',
                     f'--screenshot={image}',f'--stats={stats}','--scenario-time=0']
            run_showcase_process(Path(args.demo).resolve(),command,root,timeout=90,fallback_root=root)
            w,h,pixels=read_png_rgb(image)
            assert (w,h)==(1440,900)
            doc=json.loads(stats.read_text(encoding='utf-8'))
            assert doc['zhumadian_stage_rendered_root_count']==2346
            assert doc['zhumadian_geometry_refined']==(mode!='base')
            assert doc['zhumadian_geometry_rendered_triangles']==(
                manifest['rendered_triangles'] if mode!='base' else 3055617)
            if mode!='base':
                assert doc['zhumadian_geometry_child_triangles']==manifest['triangles']
                assert doc['zhumadian_geometry_replaced_roots']==len(manifest['replacements'])
                assert doc['zhumadian_texture_page_owner']==('foreground_children' if foreground else 'base_roots')
                assert doc['zhumadian_texture_resident_pages']<=4
            captures[name]=pixels
            telemetry[name]=doc
        before,after=telemetry[f'{view}_base'],telemetry[f'{view}_{mode_detail}']
        assert after['indices']-before['indices']==3*(manifest['triangles']-manifest['removed_base_triangles'])
        assert after['vertices']-before['vertices']==(
            manifest['vertices']-after.get('zhumadian_geometry_removed_unreferenced_vertices',0))
    a,b=captures['plant_base'],captures[f'plant_{mode_detail}']
    changed=sum(max(abs(x-y) for x,y in zip(p,q))>3 for p,q in zip(a,b))
    assert changed>10000,changed
    # This far northern strip contains no selected parent roots; it must be unchanged.
    far_changed=sum(p!=q for p,q in zip(a[:1440*140],b[:1440*140]))
    assert far_changed==0,far_changed
    native_changed=0
    fade_mid_fraction=0
    if foreground:
        image,stats=output/'plant_foreground_packed.png',output/'plant_foreground_packed.json'
        run_showcase_process(Path(args.demo).resolve(),['--preset=zhumadian','--zhumadian-detail=full',
            '--zhumadian-view=plant','--zhumadian-geometry=foreground','--zhumadian-textures=base',
            '--scenario-time=0','--frames=3','--width=1440','--height=900',
            f'--screenshot={image}',f'--stats={stats}'],root,timeout=90,fallback_root=root)
        _,_,packed=read_png_rgb(image)
        native_changed=sum(max(abs(x-y) for x,y in zip(p,q))>3 for p,q in zip(packed[1440*400:],b[1440*400:]))
        assert native_changed>1000,native_changed
        fades={}
        for label,time in (('start','0'),('mid','0.175'),('end','0.35')):
            image=output/f'foreground_fade_{label}.png'
            run_showcase_process(Path(args.demo).resolve(),['--preset=zhumadian','--zhumadian-detail=full',
                '--zhumadian-view=plant','--zhumadian-geometry=foreground','--zhumadian-texture-fade-probe',
                f'--scenario-time={time}','--frames=3','--width=1440','--height=900',f'--screenshot={image}'],
                root,timeout=90,fallback_root=root)
            _,_,fades[label]=read_png_rgb(image)
        def difference(left,right):
            return sum(abs(x-y) for p,q in zip(left,right) for x,y in zip(p,q))
        assert difference(packed,fades['start'])/(len(packed)*3)<0.02
        assert difference(b,fades['end'])/(len(b)*3)<0.02
        fade_mid_fraction=difference(packed,fades['mid'])/difference(packed,b)
        assert .35<fade_mid_fraction<.75,fade_mid_fraction
    metrics={'plant_changed_pixels':changed,'unaffected_northern_strip_changed_pixels':far_changed,
             'native_child_texture_changed_foreground_pixels':native_changed,
             'native_child_fade_mid_fraction':fade_mid_fraction,
             'base_root_count':2346,'refined_root_count':len(manifest['replacements']),
             'rendered_triangles':manifest['rendered_triangles'],
             'child_to_parent_triangle_ratio':manifest['triangles']/manifest['removed_base_triangles']}
    (output/'metrics.json').write_text(json.dumps(metrics,indent=2),encoding='utf-8')
    print('GEOMETRY REPLACEMENT PASS',metrics)


if __name__=='__main__':
    main()
