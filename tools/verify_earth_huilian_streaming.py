#!/usr/bin/env python3
"""Validate Huilian routed-imagery Earth Demo evidence.

The verifier is intentionally offline and deterministic: it consumes a final
streaming JSON snapshot and the PNG captured by earth_demo. The PNG decoder is
shared with the existing render-regression tooling and uses only Python's
standard library.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import verify_badaling_demo as image_verify


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--stats", type=Path, required=True)
    parser.add_argument("--screenshot", type=Path, required=True)
    parser.add_argument("--expected-max-lod", type=int, choices=(8, 10), required=True)
    parser.add_argument("--min-local-requests", type=int, default=0)
    parser.add_argument("--require-source-feather", action="store_true")
    parser.add_argument("--require-mipped-tiles", action="store_true")
    args = parser.parse_args()

    failures: list[str] = []
    require(args.stats.is_file(), f"stats missing: {args.stats}", failures)
    require(args.screenshot.is_file(), f"screenshot missing: {args.screenshot}", failures)
    if failures:
        print("\n".join(f"FAIL: {failure}" for failure in failures), file=sys.stderr)
        return 1

    stats = json.loads(args.stats.read_text(encoding="utf-8"))
    streaming = stats.get("streaming", {})
    routes = stats.get("huilian_routes", {})
    probes = routes.get("probes", {})

    require(stats.get("schema") == "hongtu.earth_streaming_stats.v1", "stats schema mismatch", failures)
    require(
        stats.get("imagery_provider") == "HuilianRegionalComposite-v1",
        f"unexpected imagery provider: {stats.get('imagery_provider')}",
        failures,
    )
    require(
        stats.get("cache_namespace") == "HuilianRegionalComposite-v1",
        "provider-aware cache namespace missing",
        failures,
    )
    require(int(routes.get("route_count", 0)) == 3, "expected three Huilian imagery routes", failures)
    require(
        probes.get("z3", {}).get("route") == "tianditu-offline-z1-z4"
        and probes.get("z4", {}).get("route") == "tianditu-offline-z1-z4",
        "z3/z4 probes do not resolve to the offline global base",
        failures,
    )
    require(
        probes.get("z8", {}).get("route") == "south-china-offline-z8",
        "z8 probe does not resolve to the South China offline route",
        failures,
    )
    require(
        probes.get("z10", {}).get("route") == "huilian-offline-z10",
        "z10 probe does not resolve to the Huilian near-region offline route",
        failures,
    )

    expected_key = (
        "huilian_offline_z10_requests"
        if args.expected_max_lod == 10
        else "south_china_offline_z8_requests"
    )
    local_requests = int(routes.get(expected_key, 0))
    require(
        local_requests >= args.min_local_requests,
        f"{expected_key} too low: {local_requests} < {args.min_local_requests}",
        failures,
    )
    require(
        int(streaming.get("visible_max_level", -1)) == args.expected_max_lod,
        f"visible max LOD mismatch: {streaming.get('visible_max_level')}",
        failures,
    )
    require(
        int(streaming.get("drawing_max_level", -1)) == args.expected_max_lod,
        f"drawing max LOD mismatch: {streaming.get('drawing_max_level')}",
        failures,
    )
    require(int(streaming.get("visible_tiles", 0)) >= 32, "visible tile set is unexpectedly sparse", failures)
    require(int(streaming.get("drawing_tiles", 0)) >= 16, "drawing set is unexpectedly sparse", failures)
    require(int(streaming.get("loaded_tiles", 0)) >= 32, "GPU tile cache is unexpectedly sparse", failures)
    require(int(streaming.get("pending_tiles", -1)) == 0, "imagery tiles still pending", failures)
    require(int(streaming.get("decoded_tiles", -1)) == 0, "decoded upload backlog remains", failures)
    mipped_tiles = int(streaming.get("mipped_tiles", 0))
    mip_levels_min = int(streaming.get("tile_mip_levels_min", 0))
    mip_levels_max = int(streaming.get("tile_mip_levels_max", 0))
    if args.require_mipped_tiles:
        require(mipped_tiles == int(streaming.get("loaded_tiles", -1)), "not every loaded imagery tile has mip levels", failures)
        require(mip_levels_min >= 7, f"minimum imagery mip count is too low: {mip_levels_min}", failures)
        require(mip_levels_max >= mip_levels_min, "imagery mip-level range is invalid", failures)
    lod_transition_tiles = int(streaming.get("lod_transition_tiles", 0))
    lod_transition_edges = int(streaming.get("lod_transition_edges", 0))
    require(lod_transition_tiles > 0, "drawing set contains no cross-LOD transition tiles", failures)
    require(lod_transition_edges > 0, "drawing set contains no cross-LOD transition edges", failures)
    source_transition_tiles = int(streaming.get("source_transition_tiles", 0))
    source_transition_edges = int(streaming.get("source_transition_edges", 0))
    if args.require_source_feather:
        require(source_transition_tiles > 0, "no routed source-transition tiles reached the drawing set", failures)
        require(source_transition_edges > 0, "no routed source-transition edges were feathered", failures)
    cache_usage = int(streaming.get("cache_memory_usage_bytes", -1))
    cache_budget = int(streaming.get("cache_memory_budget_bytes", -1))
    require(0 < cache_usage <= cache_budget, "tile cache memory budget invariant failed", failures)

    width, height, pixels = image_verify.read_png_rgb(args.screenshot)
    metrics = image_verify.analyze_pixels(pixels, width)
    require((width, height) == (1280, 720), f"unexpected screenshot size: {width}x{height}", failures)
    require(metrics["std_luma"] >= 12.0, f"frame contrast too low: {metrics['std_luma']:.2f}", failures)
    require(metrics["luma_range"] >= 120.0, f"frame luma range too low: {metrics['luma_range']:.2f}", failures)
    require(metrics["unique_q"] >= 200.0, f"frame color diversity too low: {metrics['unique_q']:.0f}", failures)
    require(metrics["dark_ratio"] <= 0.02, f"black/empty coverage too high: {metrics['dark_ratio']:.4f}", failures)

    digest = hashlib.sha256(args.screenshot.read_bytes()).hexdigest()
    if failures:
        print("\n".join(f"FAIL: {failure}" for failure in failures), file=sys.stderr)
        return 1

    print(
        "PASS Huilian Earth streaming: "
        f"lod={args.expected_max_lod} local_requests={local_requests} "
        f"visible={streaming['visible_tiles']} drawing={streaming['drawing_tiles']} "
        f"loaded={streaming['loaded_tiles']} cache={cache_usage}/{cache_budget} "
        f"mipped={mipped_tiles} levels={mip_levels_min}-{mip_levels_max} "
        f"lod_feather={lod_transition_tiles}/{lod_transition_edges} "
        f"source_feather={source_transition_tiles}/{source_transition_edges} "
        f"std_luma={metrics['std_luma']:.2f} sha256={digest}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
