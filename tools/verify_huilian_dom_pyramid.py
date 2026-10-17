#!/usr/bin/env python3
"""Verify Huilian DOM pyramid coverage, registration, JPEG fidelity, and seams."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from build_huilian_dem_asset import read_world_file, repo_root, transform_dom_region


def page_by_xy(level: dict[str, Any], x: int, y: int) -> dict[str, Any]:
    for page in level["pages"]:
        if int(page["x"]) == x and int(page["y"]) == y:
            return page
    raise RuntimeError(f"missing pyramid page lod={level['lod']} x={x} y={y}")


def target_bounds(page: dict[str, Any]) -> tuple[float, float, float, float]:
    west, south, east, north = (float(value) for value in page["wgs84_bounds"])
    return west, east, south, north


def image_error(actual: Any, expected: Any) -> tuple[float, int]:
    from PIL import ImageChops, ImageStat

    difference = ImageChops.difference(actual.convert("RGB"), expected.convert("RGB"))
    means = ImageStat.Stat(difference).mean
    extrema = difference.getextrema()
    return sum(float(value) for value in means) / len(means), max(high for _, high in extrema)


def stitch_horizontal(root: Path, left: dict[str, Any], right: dict[str, Any]) -> Any:
    from PIL import Image

    result = Image.new("RGB", (1024, 512))
    with Image.open(root / left["path"]) as image:
        result.paste(image.convert("RGB"), (0, 0))
    with Image.open(root / right["path"]) as image:
        result.paste(image.convert("RGB"), (512, 0))
    return result


def stitch_vertical(root: Path, south: dict[str, Any], north: dict[str, Any]) -> Any:
    from PIL import Image

    result = Image.new("RGB", (512, 1024))
    with Image.open(root / north["path"]) as image:
        result.paste(image.convert("RGB"), (0, 0))
    with Image.open(root / south["path"]) as image:
        result.paste(image.convert("RGB"), (0, 512))
    return result


def verify(manifest_path: Path, source_path: Path) -> dict[str, Any]:
    try:
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError("Pillow is required for Huilian DOM pyramid quality verification") from exc

    root = repo_root()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    pyramid = manifest["tile_pyramid"]
    levels = pyramid["levels"]
    expected_grids = [[46, 31], [23, 16], [12, 8]]
    expected_counts = [1426, 368, 96]
    failures: list[str] = []
    if manifest.get("schema") != "hongtu.huilian_dom_gpu_textures.v7":
        failures.append("manifest schema is not v7")
    if pyramid.get("coverage") != "full_dem_bounds_with_page_aligned_east_north_padding":
        failures.append("pyramid does not declare full DEM coverage")
    if [level.get("grid") for level in levels] != expected_grids:
        failures.append("pyramid level grids changed")
    if [level.get("page_count") for level in levels] != expected_counts:
        failures.append("pyramid level page counts changed")
    if sum(expected_counts) != int(pyramid.get("total_page_count", 0)):
        failures.append("pyramid total page count changed")

    actual_bytes = 0
    for level in levels:
        uv_bounds = level.get("terrain_uv_bounds", [])
        if len(uv_bounds) != 4 or float(uv_bounds[0]) != 0.0 or float(uv_bounds[1]) != 0.0:
            failures.append(f"lod {level.get('lod')} does not start at the DEM southwest boundary")
        if len(uv_bounds) != 4 or float(uv_bounds[2]) < 1.0 or float(uv_bounds[3]) < 1.0:
            failures.append(f"lod {level.get('lod')} does not cover the DEM northeast boundary")
        for page in level["pages"]:
            path = root / page["path"]
            if not path.is_file():
                failures.append(f"missing page: {page['path']}")
                continue
            actual_bytes += path.stat().st_size
            if path.stat().st_size != int(page.get("bytes", -1)):
                failures.append(f"page byte count mismatch: {page['path']}")
    if actual_bytes != int(pyramid.get("total_page_bytes", -1)):
        failures.append("pyramid total byte count does not match page files")

    source_world = read_world_file(source_path.with_suffix(".tfw"))
    Image.MAX_IMAGE_PIXELS = None
    with Image.open(source_path) as opened:
        source = opened.convert("RGB") if opened.mode != "RGB" else opened.copy()

    representative_pages = [(0, 25, 16), (1, 11, 7), (2, 5, 3)]
    page_checks: list[dict[str, Any]] = []
    for lod, x, y in representative_pages:
        page = page_by_xy(levels[lod], x, y)
        expected = transform_dom_region(source, source_world, target_bounds(page), (512, 512))
        with Image.open(root / page["path"]) as actual:
            mean_error, max_error = image_error(actual, expected)
        page_checks.append(
            {"lod": lod, "x": x, "y": y, "mean_absolute_error": mean_error, "max_error": max_error}
        )
        if mean_error > 3.0:
            failures.append(f"lod {lod} representative JPEG mean error is too high: {mean_error:.3f}")

    seam_checks: list[dict[str, Any]] = []
    for lod, x, y in representative_pages:
        left = page_by_xy(levels[lod], x, y)
        right = page_by_xy(levels[lod], x + 1, y)
        left_bounds = target_bounds(left)
        right_bounds = target_bounds(right)
        expected = transform_dom_region(
            source,
            source_world,
            (left_bounds[0], right_bounds[1], left_bounds[2], left_bounds[3]),
            (1024, 512),
        )
        actual = stitch_horizontal(root, left, right)
        mean_error, max_error = image_error(actual, expected)
        seam_checks.append(
            {"lod": lod, "axis": "x", "x": x, "y": y, "mean_absolute_error": mean_error, "max_error": max_error}
        )
        if mean_error > 3.0:
            failures.append(f"lod {lod} horizontal seam registration error is too high: {mean_error:.3f}")

    south = page_by_xy(levels[0], 25, 16)
    north = page_by_xy(levels[0], 25, 17)
    south_bounds = target_bounds(south)
    north_bounds = target_bounds(north)
    expected = transform_dom_region(
        source,
        source_world,
        (south_bounds[0], south_bounds[1], south_bounds[2], north_bounds[3]),
        (512, 1024),
    )
    actual = stitch_vertical(root, south, north)
    mean_error, max_error = image_error(actual, expected)
    seam_checks.append(
        {"lod": 0, "axis": "y", "x": 25, "y": 16, "mean_absolute_error": mean_error, "max_error": max_error}
    )
    if mean_error > 3.0:
        failures.append(f"lod 0 vertical seam registration error is too high: {mean_error:.3f}")

    return {
        "schema": "hongtu.huilian_dom_pyramid_quality.v5",
        "manifest": manifest_path.relative_to(root).as_posix(),
        "source_dom": source_path.relative_to(root).as_posix(),
        "level_grids": expected_grids,
        "total_page_count": int(pyramid["total_page_count"]),
        "total_page_bytes": actual_bytes,
        "representative_page_checks": page_checks,
        "seam_checks": seam_checks,
        "mean_error_limit": 3.0,
        "failures": failures,
        "passed": not failures,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=Path("assets/showcase/runtime/huilian_dom_gpu_manifest.json"))
    parser.add_argument("--source", type=Path, default=Path("assets/惠炼/DEM-DOM/hero_0.5M.tif"))
    parser.add_argument("--output", type=Path, default=Path("data/showcase/huilian_dom_pyramid_quality.json"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = repo_root()
    manifest = args.manifest if args.manifest.is_absolute() else root / args.manifest
    source = args.source if args.source.is_absolute() else root / args.source
    output = args.output if args.output.is_absolute() else root / args.output
    report = verify(manifest, source)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for check in report["representative_page_checks"]:
        print(
            f"page lod={check['lod']} x={check['x']} y={check['y']} "
            f"mae={check['mean_absolute_error']:.3f} max={check['max_error']}"
        )
    for check in report["seam_checks"]:
        print(
            f"seam lod={check['lod']} axis={check['axis']} x={check['x']} y={check['y']} "
            f"mae={check['mean_absolute_error']:.3f} max={check['max_error']}"
        )
    if report["failures"]:
        for failure in report["failures"]:
            print(f"- {failure}")
        return 1
    print(
        f"Huilian DOM pyramid quality: PASS levels=3 pages={report['total_page_count']} "
        f"bytes={report['total_page_bytes']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
