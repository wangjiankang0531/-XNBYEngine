#!/usr/bin/env python3
"""Compare real Vulkan context frames and preserve source-root/detail draw coverage."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
from run_showcase_demo import run_showcase_process
from verify_badaling_demo import read_png_rgb


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo",required=True)
    parser.add_argument("--output-dir",required=True)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    output=Path(args.output_dir).resolve()
    output.mkdir(parents=True,exist_ok=True)
    variants={
        "old":["--zhumadian-geometry=foreground"],
        "context":["--zhumadian-geometry=foreground","--zhumadian-context"],
        "overview":["--zhumadian-geometry=foreground","--zhumadian-context","--zhumadian-view=overview"],
        "streamed":["--zhumadian-geometry=streamed","--zhumadian-context"],
        "cold":["--zhumadian-geometry=streamed","--zhumadian-context","--zhumadian-geometry-cold","--zhumadian-textures=base"],
        "base":["--zhumadian-geometry=base","--zhumadian-context","--zhumadian-textures=base"],
        "old_mask":["--zhumadian-geometry=foreground","--zhumadian-coverage-mask"],
        "context_mask":["--zhumadian-geometry=foreground","--zhumadian-context","--zhumadian-coverage-mask"],
    }
    stats,hashes,images={},{},{}
    for name,extra in variants.items():
        image,telemetry=output/f"{name}.png",output/f"{name}.json"
        image.unlink(missing_ok=True); telemetry.unlink(missing_ok=True)
        run_showcase_process(Path(args.demo).resolve(),[
            "--preset=zhumadian","--zhumadian-detail=full","--zhumadian-view=plant",
            "--scenario-time=0","--frames=3","--width=1440","--height=900",
            f"--screenshot={image}",f"--stats={telemetry}",*extra,
        ],root,timeout=100,fallback_root=root)
        doc=json.loads(telemetry.read_text(encoding="utf-8"))
        assert doc["zhumadian_stage_rendered_root_count"]==2346
        assert doc["zhumadian_context_active"]==(name not in ("old","old_mask"))
        stats[name]=doc; hashes[name]=hashlib.sha256(image.read_bytes()).hexdigest()
        if name in ("old","context","overview"):
            w,h,pixels=read_png_rgb(image); assert (w,h)==(1440,900)
            images[name]=pixels
        if name not in ("old","old_mask"):
            context=doc["zhumadian_context"]
            assert context["vertices"]==66049 and context["triangles"]==131072
            assert context["imagery"]["native_resolution_m"]==10
            assert context["model_srs"]=="EPSG:32650"
        if name in ("streamed","cold"):
            s=doc["zhumadian_geometry_stream"]
            assert s["prefix_indices"]==1056768
            assert s["drawn_base_indices"]==10223619-s["removed_parent_indices"]
            assert s["drawn_child_indices"]==s["child_indices"]
            assert s["buffer_payload_bytes"]<=s["budget_bytes"]
            assert not s["pending"] and s["failures"]==0
    assert hashes["old"]=="49ea425d20a7d943fb9f3d90b9ec8b1f2a9f86f5b00d981c71993ec0728a2ec8"
    assert hashes["cold"]==hashes["base"],"Cold stream must retain the complete context/base frame"
    assert hashes["old_mask"]==hashes["context_mask"],"Context changed actual photographic geometry coverage"
    for name in ("context","overview"):
        assert stats[name]["vertices"]-stats["old"]["vertices"]==66049
        assert stats[name]["indices"]-stats["old"]["indices"]==393216
        assert stats[name]["zhumadian_geometry_rendered_triangles"]==5266039
    before,after=images["old"],images["context"]
    background,count=collections.Counter(before).most_common(1)[0]
    filled=sum(a==background and b!=background for a,b in zip(before,after))
    assert filled>=count*0.999
    changes=[i for i,(a,b) in enumerate(zip(before,after)) if a!=b and a!=background]
    # Exclude the source/background silhouette's three-pixel edge band. It can
    # legitimately change when the formerly blue sample behind the edge is land.
    interior=[]
    for i in changes:
        y,x=divmod(i,1440)
        if not any(before[yy*1440+xx]==background
                   for yy in range(max(0,y-3),min(900,y+4))
                   for xx in range(max(0,x-3),min(1440,x+4))):
            interior.append(i)
    assert len(interior)<200,("Context occluded photographic foreground",len(interior))
    assert stats["streamed"]["zhumadian_geometry_stream"]["resident_count"]>0
    assert stats["cold"]["zhumadian_geometry_stream"]["resident_count"]==0
    result={"image_sha256":hashes,"old_background_pixels":count,"filled_background_pixels":filled,
            "changed_non_background_pixels":len(changes),"changed_interior_pixels":len(interior)}
    (output/"metrics.json").write_text(json.dumps(result,indent=2),encoding="utf-8")
    print("CONTEXT COVERAGE / FOREGROUND / STREAM FALLBACK PASS",result)


if __name__=="__main__":
    main()
