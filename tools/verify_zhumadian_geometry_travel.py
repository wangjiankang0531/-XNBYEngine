#!/usr/bin/env python3
"""Exercise real interactive geometry uploads/evictions across near/far/near frames."""
import argparse
import json
from pathlib import Path
from run_showcase_demo import run_showcase_process


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--context", action="store_true")
    parser.add_argument("--context-color-matched", action="store_true")
    parser.add_argument("--context-fused", action="store_true")
    args = parser.parse_args()
    if args.context_color_matched and not args.context:
        parser.error('--context-color-matched requires --context')
    if args.context_fused and not args.context:
        parser.error('--context-fused requires --context')
    root = Path(__file__).resolve().parents[1]
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.unlink(missing_ok=True)
    run_showcase_process(Path(args.demo).resolve(), [
        "--preset=zhumadian", "--zhumadian-detail=full", "--zhumadian-view=plant",
        "--zhumadian-geometry=streamed", "--zhumadian-geometry-travel-probe",
        "--width=1440", "--height=900", "--frames=1", f"--stats={output}",
        *(["--zhumadian-context"] if args.context else []),
        *(["--zhumadian-context-color=matched"] if args.context_color_matched else []),
        *(["--zhumadian-context-geometry=fused"] if args.context_fused else []),
    ], root, timeout=70, fallback_root=root)
    doc = json.loads(output.read_text(encoding="utf-8"))
    assert doc["passed"] and 0 < doc["duration_seconds"] < 30 and doc["frames"] > 96
    near, far, restored = doc["stations"]
    assert near["resident_count"] > 0 and near["resident_roots"] == restored["resident_roots"]
    assert restored["uploads"] >= near["uploads"] * 2 and restored["evictions"] >= near["resident_count"]
    assert far["resident_count"] == far["buffer_payload_bytes"] == far["drawn_child_indices"] == 0
    context_indices=393216 if args.context else 0
    if args.context_fused:
        context_indices=json.loads((root/'data/showcase/zhumadian_context_fusion.json').read_text(encoding='utf-8'))['indices']
    base_indices = 9830403 + context_indices
    assert far["drawn_base_indices"] == base_indices
    for station in doc["stations"]:
        assert station["base_root_coverage"] == 2346 and not station["pending"] and station["failures"] == 0
        assert station['prefix_indices']==663552+context_indices
        assert station["peak_payload_bytes"] <= station["budget_bytes"] == 192*1024*1024
        assert station["peak_resident_count"] <= station["resident_limit"] == 96
        assert station["max_upload_per_service_bytes"] <= station["upload_limit_bytes"]
        assert station["drawn_base_indices"] == base_indices - station["removed_parent_indices"]
        assert station["drawn_child_indices"] == station["child_indices"]
        assert (station["drawn_base_indices"] - station["prefix_indices"] + station["drawn_child_indices"]) // 3 == station["source_drawn_triangles"]
    print("INTERACTIVE GEOMETRY TRAVEL PASS", doc["frames"], "frames;", doc["duration_seconds"], "seconds")


if __name__ == "__main__":
    main()
