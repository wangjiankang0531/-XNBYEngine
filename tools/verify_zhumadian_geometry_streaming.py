#!/usr/bin/env python3
"""Render cold/warm/budget/probe frames and audit actual root draw residency."""
import argparse
import hashlib
import json
from pathlib import Path

from run_showcase_demo import run_showcase_process
from verify_badaling_demo import read_png_rgb


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", required=True)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = Path(args.output_dir).resolve()
    output.mkdir(parents=True, exist_ok=True)
    catalog = json.loads((root / "data/showcase/zhumadian_geometry_chunks.json").read_text(encoding="utf-8"))
    chunks = {c["tile_id"]: c for c in catalog["chunks"]}
    variants = {
        "base": ["--zhumadian-geometry=base", "--zhumadian-textures=base"],
        "cold": ["--zhumadian-geometry-cold", "--zhumadian-textures=base"],
        "warm": ["--zhumadian-textures=base"],
        "small": ["--zhumadian-geometry-budget-mib=32", "--zhumadian-textures=base"],
        "paged": ["--zhumadian-textures=paged"],
        "probe": ["--zhumadian-geometry-stream-probe", "--zhumadian-textures=paged"],
        "overview": ["--zhumadian-view=overview", "--zhumadian-textures=base"],
    }
    telemetry, hashes, captures = {}, {}, {}
    for name, extra in variants.items():
        image, stats = output / f"{name}.png", output / f"{name}.json"
        image.unlink(missing_ok=True)
        stats.unlink(missing_ok=True)
        run_showcase_process(Path(args.demo).resolve(), [
            "--preset=zhumadian", "--zhumadian-detail=full", "--zhumadian-view=plant",
            "--zhumadian-geometry=streamed", "--scenario-time=0", "--frames=3",
            "--width=1440", "--height=900", f"--screenshot={image}", f"--stats={stats}", *extra,
        ], root, timeout=100, fallback_root=root)
        doc = json.loads(stats.read_text(encoding="utf-8"))
        assert doc["zhumadian_stage_rendered_root_count"] == 2346
        assert doc["vertices"] == 9277204 and doc["indices"] == 9830403
        hashes[name] = hashlib.sha256(image.read_bytes()).hexdigest()
        telemetry[name] = doc
        if name != "base":
            s = doc["zhumadian_geometry_stream"]
            ids = s["resident_roots"]
            assert len(ids) == len(set(ids)) == s["resident_count"]
            assert s["base_root_coverage"] == 2346 and not s["pending"]
            assert s["buffer_payload_bytes"] == sum(chunks[i]["gpu_payload_bytes"] for i in ids)
            assert s["child_indices"] == sum(chunks[i]["indices"] for i in ids)
            assert s["removed_parent_indices"] == sum(count for i in ids for _, count in chunks[i]["base_spans"])
            assert s["peak_payload_bytes"] <= s["budget_bytes"]
            assert s["peak_resident_count"] <= s["resident_limit"] == 96
            assert s["max_upload_per_service_bytes"] <= s["upload_limit_bytes"]
            assert s["drawn_base_indices"] == doc["indices"] - s["removed_parent_indices"]
            assert s["drawn_child_indices"] == s["child_indices"]
            drawn = (s["drawn_base_indices"] - s["prefix_indices"] + s["drawn_child_indices"]) // 3
            assert drawn == s["source_drawn_triangles"] == doc["zhumadian_geometry_rendered_triangles"]
            assert doc["zhumadian_geometry_replaced_roots"] == len(ids)
            assert s["failures"] == (2 if name == "probe" else 0)
        if name in ("cold", "warm", "small"):
            w, h, pixels = read_png_rgb(image)
            assert (w, h) == (1440, 900)
            captures[name] = pixels
    assert hashes["base"] == hashes["cold"], "Cold stream must draw the identical complete base"
    assert telemetry["cold"]["zhumadian_geometry_stream"]["resident_count"] == 0
    warm = telemetry["warm"]["zhumadian_geometry_stream"]
    small = telemetry["small"]["zhumadian_geometry_stream"]
    assert 0 < small["resident_count"] < warm["resident_count"]
    assert small["buffer_payload_bytes"] < warm["buffer_payload_bytes"]
    changed = sum(max(abs(x-y) for x, y in zip(a, b)) > 3
                  for a, b in zip(captures["cold"], captures["warm"]))
    assert changed > 10000, changed
    assert captures["cold"][:1440*140] == captures["warm"][:1440*140], "Unrefined northern scene changed"
    assert hashes["paged"] == hashes["probe"], "Request/failure recovery must restore the same image"
    probe = telemetry["probe"]["zhumadian_geometry_stream"]
    assert probe["probe_passed"] and probe["discarded_stale"] >= 1 and probe["evictions"] > 0
    assert probe["resident_roots"] == telemetry["paged"]["zhumadian_geometry_stream"]["resident_roots"]
    overview = telemetry["overview"]["zhumadian_geometry_stream"]
    assert overview["resident_count"] == 0 and overview["buffer_payload_bytes"] == 0
    result = {"image_sha256": hashes, "changed_pixels": changed,
              "stream_stats": {k: v["zhumadian_geometry_stream"] for k, v in telemetry.items() if k != "base"}}
    (output / "metrics.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    print("GEOMETRY STREAM / FALLBACK / BUDGET / ACTUAL DRAW PASS",
          warm["resident_count"], "roots;", warm["buffer_payload_bytes"], "bytes;", changed, "changed pixels")


if __name__ == "__main__":
    main()
