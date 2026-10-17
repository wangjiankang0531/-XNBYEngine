#!/usr/bin/env python3
"""Verify full source-root coverage, runtime geometry and a Vulkan capture."""
import argparse
import json
from pathlib import Path

import build_zhumadian_full_stage as full
import build_zhumadian_stage_asset as base
from run_showcase_demo import run_showcase_process
from verify_zhumadian_stage import validate_image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", required=True)
    parser.add_argument("--view", choices=["overview", "oblique", "close", "plant"], default="overview")
    parser.add_argument("--output", required=True)
    parser.add_argument("--stats-output", required=True)
    parser.add_argument("--width", type=int, default=1440)
    parser.add_argument("--height", type=int, default=900)
    args = parser.parse_args()
    root = base.repo_root()
    manifest = json.loads((root / "data/showcase/zhumadian_full_stage_manifest.json").read_text(encoding="utf-8"))
    roots = full.roots_from(root / "assets/驻马店园区")
    records = full.source_records(root, roots)
    assert records == manifest["source_roots"], "source roots/hash changed"
    assert len(roots) == manifest["root_count"] == 2346, "full root count differs"
    objects = [root / "work/zmd_full_stage_convert" / f"batch_{start:04d}" / "roots.obj"
               for start in range(0, len(roots), full.BATCH)]
    assert full.audit_root_geometry(roots, objects) == len(roots)
    image, stats_path = Path(args.output).resolve(), Path(args.stats_output).resolve()
    for path in (image, stats_path):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.unlink(missing_ok=True)
    demo = Path(args.demo).resolve()
    run_showcase_process(demo, ["--scene=industrial_park", "--preset=zhumadian", "--zhumadian-detail=full",
                               f"--zhumadian-view={args.view}", "--frames=3", f"--width={args.width}",
                               f"--height={args.height}", f"--screenshot={image}", f"--stats={stats_path}"],
                         root, timeout=90, fallback_root=root)
    stats = json.loads(stats_path.read_text(encoding="utf-8"))
    expected = {"zhumadian_stage_detail": "full", "zhumadian_stage_partial_roi": False,
                "zhumadian_stage_full_dataset_streaming": False, "zhumadian_stage_model_loaded": True,
                "zhumadian_stage_texture_loaded": True, "zhumadian_stage_texture_fallback": False,
                "zhumadian_stage_rendered_root_count": len(roots),
                "zhumadian_stage_root_ids": [r.tile_id for r in roots],
                "zhumadian_stage_triangles": manifest["triangles"],
                "zhumadian_stage_vertices": manifest["vertices"],
                "zhumadian_stage_atlas_size_px": [8192, 8192], "zhumadian_stage_atlas_mip_levels": 14,
                "zhumadian_stage_view": args.view, "zhumadian_stage_untextured_primitives": 0}
    for key, value in expected.items():
        assert stats.get(key) == value, f"{key}: {stats.get(key)} != {value}"
    for side in ("min", "max"):
        assert all(abs(a-b) < .02 for a, b in zip(stats[f"zhumadian_stage_bounds_{side}_m"], manifest[f"bounds_{side}"]))
    assert stats["camera_far_clip_m"] > stats["camera_maximum_scene_distance_m"]
    assert stats["camera_far_clip_margin_m"] >= 800
    metrics = validate_image(image, args.width, args.height, 100000)
    print(f"FULL SCENE {args.view} PASS: {len(roots)} roots have exported faces; "
          f"{manifest['triangles']} runtime triangles, luma std={metrics['std_luma']:.2f}; "
          "complete root footprint, not recursive detail streaming")


if __name__ == "__main__":
    main()
