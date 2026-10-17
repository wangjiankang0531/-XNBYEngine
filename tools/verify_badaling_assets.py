#!/usr/bin/env python3
"""Validate the real-world Badaling asset package used by badaling_demo."""

from __future__ import annotations

import argparse
import math
import statistics
import struct
import sys
from pathlib import Path


MAGIC = b"HTBDALNG"


def fail(message: str) -> None:
    raise RuntimeError(message)


def unpack_from(fmt: str, data: bytes, offset: int):
    size = struct.calcsize(fmt)
    if offset + size > len(data):
        fail(f"unexpected EOF while reading {fmt} at offset {offset}")
    return struct.unpack_from(fmt, data, offset), offset + size


def quantized_color_count(colors: bytes) -> int:
    return len({(colors[i] >> 4, colors[i + 1] >> 4, colors[i + 2] >> 4) for i in range(0, len(colors), 3)})


def color_metrics(colors: bytes) -> dict[str, float]:
    pixel_count = len(colors) // 3
    green = 0
    bluish = 0
    lumas = []
    chroma_sum = 0.0
    for i in range(0, len(colors), 3):
        r = colors[i]
        g = colors[i + 1]
        b = colors[i + 2]
        if g > r * 1.05 and g > b * 1.02 and g > 38:
            green += 1
        if b > r + 16 and b > g + 8:
            bluish += 1
        lumas.append(0.2126 * r + 0.7152 * g + 0.0722 * b)
        chroma_sum += max(r, g, b) - min(r, g, b)
    return {
        "unique_q": float(quantized_color_count(colors)),
        "green_ratio": green / pixel_count,
        "bluish_ratio": bluish / pixel_count,
        "mean_luma": statistics.fmean(lumas),
        "std_luma": statistics.pstdev(lumas),
        "mean_chroma": chroma_sum / pixel_count,
    }


def path_length(path: list[tuple[float, float]]) -> float:
    return sum(math.hypot(path[i + 1][0] - path[i][0], path[i + 1][1] - path[i][1]) for i in range(len(path) - 1))


def parse_asset(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < 64:
        fail(f"asset too small: {len(data)} bytes")
    if data[: len(MAGIC)] != MAGIC:
        fail(f"bad magic in {path}")
    offset = len(MAGIC)

    (version, width, height, wall_count), offset = unpack_from("<IIII", data, offset)
    (min_x, max_x, min_y, max_y, base_elevation), offset = unpack_from("<fffff", data, offset)
    pixel_count = width * height
    heights_size = pixel_count * 4
    colors_size = pixel_count * 3
    if offset + heights_size + colors_size > len(data):
        fail("height/color payload exceeds file size")

    heights = struct.unpack_from(f"<{pixel_count}f", data, offset)
    offset += heights_size
    colors = data[offset : offset + colors_size]
    offset += colors_size

    paths: list[list[tuple[float, float]]] = []
    for _ in range(wall_count):
        (point_count,), offset = unpack_from("<I", data, offset)
        if point_count < 2 or point_count > 4096:
            fail(f"invalid wall point count: {point_count}")
        raw_points, offset = unpack_from(f"<{point_count * 2}f", data, offset)
        points = [(raw_points[i], raw_points[i + 1]) for i in range(0, len(raw_points), 2)]
        paths.append(points)

    if offset != len(data):
        fail(f"unexpected trailing bytes: {len(data) - offset}")

    lengths = [path_length(path_points) for path_points in paths]
    return {
        "size": len(data),
        "version": version,
        "width": width,
        "height": height,
        "wall_count": wall_count,
        "bounds": (min_x, max_x, min_y, max_y),
        "base_elevation": base_elevation,
        "height_min": min(heights),
        "height_max": max(heights),
        "height_mean": statistics.fmean(heights),
        "height_std": statistics.pstdev(heights),
        "color_metrics": color_metrics(colors),
        "paths": paths,
        "path_lengths": lengths,
        "total_path_points": sum(len(path_points) for path_points in paths),
        "total_path_length": sum(lengths),
        "longest_path_length": max(lengths) if lengths else 0.0,
    }


def validate_attribution(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    required = [
        "https://registry.opendata.aws/terrain-tiles/",
        "https://s2maps.eu/",
        "https://tiles.maps.eox.at/",
        "https://cloudless.eox.at/documentation/license",
        "https://www.openstreetmap.org/copyright",
    ]
    for needle in required:
        if needle not in text:
            fail(f"missing attribution URL: {needle}")


def validate_asset(metrics: dict) -> None:
    failures: list[str] = []

    def require(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    min_x, max_x, min_y, max_y = metrics["bounds"]
    span_x = max_x - min_x
    span_y = max_y - min_y
    height_range = metrics["height_max"] - metrics["height_min"]
    colors = metrics["color_metrics"]

    require(metrics["version"] >= 2, f"version too old: {metrics['version']}")
    require(metrics["width"] >= 257 and metrics["height"] >= 257, f"grid too small: {metrics['width']}x{metrics['height']}")
    require(metrics["width"] <= 4096 and metrics["height"] <= 4096, f"grid too large: {metrics['width']}x{metrics['height']}")
    require(metrics["size"] >= 1_000_000, f"asset package too small: {metrics['size']} bytes")
    require(span_x > 500.0 and span_y > 500.0, f"local terrain bounds too small: {span_x:.1f}x{span_y:.1f}")
    require(-500.0 <= metrics["base_elevation"] <= 6000.0, f"base elevation out of range: {metrics['base_elevation']:.1f}")
    require(height_range >= 35.0, f"DEM relief too flat: {height_range:.2f}")
    require(metrics["height_std"] >= 7.0, f"DEM height std too low: {metrics['height_std']:.2f}")
    require(colors["unique_q"] >= 48.0, f"DOM has too few quantized colors: {colors['unique_q']:.0f}")
    require(colors["green_ratio"] >= 0.22, f"DOM vegetation signal too weak: {colors['green_ratio']:.3f}")
    require(colors["std_luma"] >= 14.0, f"DOM luma variation too low: {colors['std_luma']:.2f}")
    require(colors["mean_chroma"] >= 18.0, f"DOM chroma too low: {colors['mean_chroma']:.2f}")
    require(metrics["wall_count"] >= 8, f"too few OSM wall paths: {metrics['wall_count']}")
    require(metrics["total_path_points"] >= 240, f"too few OSM wall path points: {metrics['total_path_points']}")
    require(metrics["total_path_length"] >= 900.0, f"total OSM wall length too short: {metrics['total_path_length']:.1f}")
    require(metrics["longest_path_length"] >= 180.0, f"longest OSM wall path too short: {metrics['longest_path_length']:.1f}")

    for path_index, path_points in enumerate(metrics["paths"]):
        for point_index, (x, y) in enumerate(path_points):
            if not (min_x - 80.0 <= x <= max_x + 80.0 and min_y - 80.0 <= y <= max_y + 80.0):
                failures.append(f"wall point outside terrain bounds: path={path_index}, point={point_index}, xy=({x:.1f},{y:.1f})")
                break

    if failures:
        detail = "\n  - ".join(failures)
        fail(f"Badaling asset validation failed:\n  - {detail}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--asset", default="assets/badaling/badaling_terrain.bin")
    parser.add_argument("--attribution", default="assets/badaling/SOURCE_ATTRIBUTION.txt")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        asset = Path(args.asset)
        attribution = Path(args.attribution)
        if not asset.exists():
            fail(f"missing Badaling asset package: {asset}")
        if not attribution.exists():
            fail(f"missing attribution file: {attribution}")
        metrics = parse_asset(asset)
        validate_asset(metrics)
        validate_attribution(attribution)
    except Exception as exc:
        print(f"[badaling_asset_verify] {exc}", file=sys.stderr)
        return 1

    colors = metrics["color_metrics"]
    print(
        "[badaling_asset_verify] ok "
        f"grid={metrics['width']}x{metrics['height']} "
        f"height_range={metrics['height_max'] - metrics['height_min']:.1f} "
        f"colors={colors['unique_q']:.0f} green={colors['green_ratio']:.3f} "
        f"walls={metrics['wall_count']} points={metrics['total_path_points']} "
        f"length={metrics['total_path_length']:.1f}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
