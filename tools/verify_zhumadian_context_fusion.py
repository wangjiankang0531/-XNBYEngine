#!/usr/bin/env python3
"""Compare dense/coarse terrain and actual source coverage across five camera views."""
import argparse
import hashlib
import json
from pathlib import Path
from run_showcase_demo import run_showcase_process
from verify_badaling_demo import read_png_rgb


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--demo',required=True)
    parser.add_argument('--output-dir',required=True)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    output=Path(args.output_dir).resolve(); output.mkdir(parents=True,exist_ok=True)
    fusion=json.loads((root/'data/showcase/zhumadian_context_fusion.json').read_text(encoding='utf-8'))
    hashes,metrics={},{}
    for view in ('plant','overview','oblique','boundary','edge'):
        frames={}
        for mode in ('coarse','fused'):
            for mask in (False,True):
                name=f'{view}_{mode}'+('_mask' if mask else '')
                image,telemetry=output/f'{name}.png',output/f'{name}.json'
                image.unlink(missing_ok=True); telemetry.unlink(missing_ok=True)
                run_showcase_process(Path(args.demo).resolve(),[
                    '--preset=zhumadian','--zhumadian-detail=full',f'--zhumadian-view={view}',
                    '--zhumadian-geometry=foreground','--zhumadian-context','--zhumadian-context-color=matched',
                    f'--zhumadian-context-geometry={mode}','--scenario-time=0','--frames=3','--width=1440','--height=900',
                    f'--screenshot={image}',f'--stats={telemetry}',
                    *(['--zhumadian-coverage-mask'] if mask else []),
                ],root,timeout=100,fallback_root=root)
                w,h,frames[(mode,mask)]=read_png_rgb(image); assert (w,h)==(1440,900)
                hashes[name]=hashlib.sha256(image.read_bytes()).hexdigest()
                stats=json.loads(telemetry.read_text(encoding='utf-8'))
                assert stats['zhumadian_stage_rendered_root_count']==2346
                assert stats['zhumadian_context_fused']==(mode=='fused')
                assert stats['zhumadian_geometry_rendered_triangles']==5266039
                vertices= fusion['vertices'] if mode=='fused' else 66049
                indices= fusion['indices'] if mode=='fused' else 393216
                assert stats['vertices']==15908464+vertices and stats['indices']==16461669+indices
        mask=frames[('coarse',True)]
        fused_mask=frames[('fused',True)]
        # Lowering an old underlay depth conflict may reveal additional source
        # samples. Require monotonic source coverage, rather than preserving an
        # existing occlusion. Count every decrease, including partial MSAA edges.
        lost=sum(b[0]<a[0] for a,b in zip(mask,fused_mask))
        gained=sum(b[0]>a[0] for a,b in zip(mask,fused_mask))
        assert lost==0,('New terrain reduced source coverage',view,lost)
        before,after=frames[('coarse',False)],frames[('fused',False)]
        count=sum(m==(255,255,255) for m in mask)
        preserved=sum(a==b for a,b,m in zip(before,after,mask) if m==(255,255,255))
        changed=sum(a!=b for a,b in zip(before,after))
        assert count>1000 and preserved==count,('New terrain changed opaque source pixels',view,count-preserved)
        assert changed>1000
        metrics[view]={'source_opaque_pixels':count,'unchanged_source_opaque_pixels':preserved,
                      'lost_source_coverage_pixels':lost,'gained_source_coverage_pixels':gained,
                      'changed_context_pixels':changed}
        del frames
    assert hashes['plant_coarse']=='362793d4c89b4fbc95b577b6d007051a271b539790e6408b5dc742e4f3603cd9'
    assert hashes['overview_coarse']=='f6e483ba36ccc383d026de56a63cbfb635e9c9922d07561b933e5affe44feb54'
    result={'image_sha256':hashes,'views':metrics,'ground_gap_before_m':fusion['median_ground_gap_before_m'],
            'ground_gap_after_m':fusion['median_ground_gap_after_m']}
    (output/'metrics.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print('FUSION / FIVE VIEW COVERAGE / SOURCE COLOR PASS',result)


if __name__=='__main__':main()
