#!/usr/bin/env python3
"""Render and validate a Badaling demo screenshot.

This script intentionally uses only Python's standard library so it can run in
CTest environments without Pillow. It decodes the PNG written by badaling_demo,
checks that the frame is nonblank, and applies light preset-specific checks for
the weather modes.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
import subprocess
import sys
import zlib
from pathlib import Path


PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class PngError(RuntimeError):
    pass


def paeth_predictor(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def read_png_rgb(path: Path) -> tuple[int, int, list[tuple[int, int, int]]]:
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise PngError(f"{path} is not a PNG file")

    pos = len(PNG_SIGNATURE)
    width = height = bit_depth = color_type = interlace = None
    idat = bytearray()

    while pos + 8 <= len(data):
        length = struct.unpack(">I", data[pos : pos + 4])[0]
        chunk_type = data[pos + 4 : pos + 8]
        chunk_data = data[pos + 8 : pos + 8 + length]
        pos += 12 + length
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, compression, filter_method, interlace = struct.unpack(
                ">IIBBBBB", chunk_data
            )
            if compression != 0 or filter_method != 0:
                raise PngError("unsupported PNG compression/filter method")
        elif chunk_type == b"IDAT":
            idat.extend(chunk_data)
        elif chunk_type == b"IEND":
            break

    if width is None or height is None or bit_depth is None or color_type is None or interlace is None:
        raise PngError("missing IHDR")
    if bit_depth != 8 or color_type not in (2, 6) or interlace != 0:
        raise PngError(f"unsupported PNG layout: bit_depth={bit_depth}, color_type={color_type}, interlace={interlace}")
    if not idat:
        raise PngError("missing IDAT")

    channels = 4 if color_type == 6 else 3
    stride = width * channels
    raw = zlib.decompress(bytes(idat))
    expected = (stride + 1) * height
    if len(raw) != expected:
        raise PngError(f"unexpected PNG payload size: got {len(raw)}, expected {expected}")

    pixels: list[tuple[int, int, int]] = []
    prev = bytearray(stride)
    offset = 0
    bpp = channels
    for _ in range(height):
        filter_type = raw[offset]
        offset += 1
        row = bytearray(raw[offset : offset + stride])
        offset += stride

        if filter_type == 1:
            for i in range(stride):
                left = row[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + left) & 0xFF
        elif filter_type == 2:
            for i in range(stride):
                row[i] = (row[i] + prev[i]) & 0xFF
        elif filter_type == 3:
            for i in range(stride):
                left = row[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + ((left + prev[i]) // 2)) & 0xFF
        elif filter_type == 4:
            for i in range(stride):
                left = row[i - bpp] if i >= bpp else 0
                up = prev[i]
                up_left = prev[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + paeth_predictor(left, up, up_left)) & 0xFF
        elif filter_type != 0:
            raise PngError(f"unsupported PNG filter type: {filter_type}")

        for i in range(0, stride, channels):
            pixels.append((row[i], row[i + 1], row[i + 2]))
        prev = row

    return width, height, pixels


def analyze_pixels(pixels: list[tuple[int, int, int]], width: int) -> dict[str, float]:
    count = len(pixels)
    luma_values = []
    quantized = set()
    whiteish = 0
    bluish_bright = 0
    dark = 0
    chroma_sum = 0.0
    min_luma = 255.0
    max_luma = 0.0

    for r, g, b in pixels:
        luma = 0.2126 * r + 0.7152 * g + 0.0722 * b
        luma_values.append(luma)
        min_luma = min(min_luma, luma)
        max_luma = max(max_luma, luma)
        quantized.add((r >> 4, g >> 4, b >> 4))
        if r > 185 and g > 185 and b > 185 and max(r, g, b) - min(r, g, b) < 36:
            whiteish += 1
        if b > r + 20 and g > r + 8 and luma > 96:
            bluish_bright += 1
        if luma < 42:
            dark += 1
        chroma_sum += max(r, g, b) - min(r, g, b)

    # The shader draws rain as bright bluish diagonal streaks. Smooth sky can be
    # blue too, so this looks for local luma discontinuities in blue-biased pixels.
    rain_edge = 0
    rain_candidate = 0
    if width > 8 and count % width == 0:
        height = count // width
        for y in range(2, height - 2, 2):
            row = y * width
            for x in range(2, width - 2, 2):
                i = row + x
                r, g, b = pixels[i]
                luma = luma_values[i]
                if b > r + 18 and g > r + 5 and luma > 75:
                    rain_candidate += 1
                    local_delta = max(
                        abs(luma - luma_values[i - 2]),
                        abs(luma - luma_values[i + 2]),
                        abs(luma - luma_values[i - 2 * width]),
                        abs(luma - luma_values[i + 2 * width]),
                        abs(luma - luma_values[i + 2 * width - 2]),
                        abs(luma - luma_values[i - 2 * width + 2]),
                    )
                    if local_delta > 18:
                        rain_edge += 1

    mean = sum(luma_values) / count
    variance = sum((value - mean) * (value - mean) for value in luma_values) / count
    sampled_count = max(1, count // 4)
    return {
        "mean_luma": mean,
        "std_luma": math.sqrt(variance),
        "min_luma": min_luma,
        "max_luma": max_luma,
        "luma_range": max_luma - min_luma,
        "unique_q": float(len(quantized)),
        "whiteish_ratio": whiteish / count,
        "bluish_bright_ratio": bluish_bright / count,
        "dark_ratio": dark / count,
        "mean_chroma": chroma_sum / count,
        "rain_edge_ratio": rain_edge / sampled_count,
        "rain_edge_candidate_ratio": rain_edge / max(1, rain_candidate),
    }


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def validate(path: Path, preset: str, width: int, height: int, min_bytes: int) -> dict[str, float]:
    if not path.exists():
        raise RuntimeError(f"screenshot missing: {path}")
    size = path.stat().st_size
    if size < min_bytes:
        raise RuntimeError(f"screenshot too small: {size} bytes < {min_bytes} bytes")

    actual_width, actual_height, pixels = read_png_rgb(path)
    metrics = analyze_pixels(pixels, actual_width)
    failures: list[str] = []

    require(actual_width == width and actual_height == height, f"expected {width}x{height}, got {actual_width}x{actual_height}", failures)
    require(metrics["unique_q"] >= 42.0, f"too few quantized colors: {metrics['unique_q']:.0f}", failures)
    require(metrics["std_luma"] >= 10.0, f"luma variation too low: {metrics['std_luma']:.2f}", failures)
    require(metrics["luma_range"] >= 48.0, f"luma range too low: {metrics['luma_range']:.2f}", failures)

    if preset == "rain":
        require(
            metrics["rain_edge_ratio"] >= 0.045 and metrics["rain_edge_candidate_ratio"] >= 0.18,
            "rain streak signal too weak: "
            f"edge={metrics['rain_edge_ratio']:.4f}, candidate={metrics['rain_edge_candidate_ratio']:.4f}",
            failures,
        )
    elif preset == "snow":
        require(
            metrics["whiteish_ratio"] >= 0.0025,
            f"snow flake signal too weak: {metrics['whiteish_ratio']:.4f}",
            failures,
        )
    elif preset == "fog":
        require(metrics["mean_luma"] >= 88.0, f"fog preset is not bright enough: {metrics['mean_luma']:.2f}", failures)
        require(metrics["mean_chroma"] <= 52.0, f"fog preset is too saturated: {metrics['mean_chroma']:.2f}", failures)
    elif preset == "night":
        require(metrics["mean_luma"] <= 82.0, f"night preset is too bright: {metrics['mean_luma']:.2f}", failures)
        require(metrics["dark_ratio"] >= 0.16, f"night dark-pixel ratio too low: {metrics['dark_ratio']:.4f}", failures)

    if failures:
        detail = "\n  - ".join(failures)
        raise RuntimeError(f"{preset} screenshot validation failed:\n  - {detail}\nmetrics={metrics}")
    return metrics


def validate_scene_stats(
    path: Path | None,
    preset: str,
    expected_azimuth: float | None,
    expected_elevation: float | None,
    expected_rain: float | None = None,
    expected_snow: float | None = None,
    expected_fog_density: float | None = None,
    expected_cloud_amount: float | None = None,
    min_cloud_clusters: int = 4,
) -> dict:
    if path is None:
        return {}
    if not path.exists():
        raise RuntimeError(f"scene stats missing: {path}")
    stats = json.loads(path.read_text(encoding="utf-8"))
    failures: list[str] = []

    def check(condition: bool, message: str) -> None:
        if not condition:
            failures.append(message)

    tree_count = int(stats.get("broadleaf_trees", 0)) + int(stats.get("pine_trees", 0))
    vegetation_count = tree_count + int(stats.get("shrubs", 0))
    check(bool(stats.get("real_asset")), "demo did not report the real Badaling asset package")
    check(int(stats.get("wall_paths", 0)) >= 3, f"too few display wall paths in scene stats: {stats.get('wall_paths')}")
    check(
        int(stats.get("cloud_clusters", 0)) >= min_cloud_clusters,
        f"too few cloud clusters: {stats.get('cloud_clusters')}",
    )
    check(int(stats.get("watch_towers", 0)) >= 3, f"too few watch towers: {stats.get('watch_towers')}")
    check(tree_count >= 120, f"too few tree instances: {tree_count}")
    check(vegetation_count >= 350, f"too few vegetation instances: {vegetation_count}")
    check(int(stats.get("vertices", 0)) >= 250_000, f"vertex count too low: {stats.get('vertices')}")
    check(int(stats.get("indices", 0)) >= 600_000, f"index count too low: {stats.get('indices')}")
    check(stats.get("preset") == preset, f"preset mismatch in stats: {stats.get('preset')} != {preset}")

    if expected_azimuth is not None:
        actual = float(stats.get("sun_azimuth_degrees", -9999.0))
        delta = abs(((actual - expected_azimuth + 180.0) % 360.0) - 180.0)
        check(delta <= 0.75, f"sun azimuth mismatch: got {actual:.2f}, expected {expected_azimuth:.2f}")
    if expected_elevation is not None:
        actual = float(stats.get("sun_elevation_degrees", -9999.0))
        check(abs(actual - expected_elevation) <= 0.75, f"sun elevation mismatch: got {actual:.2f}, expected {expected_elevation:.2f}")

    for key, expected in (
        ("rain", expected_rain),
        ("snow", expected_snow),
        ("fog_density", expected_fog_density),
        ("cloud_amount", expected_cloud_amount),
    ):
        if expected is None:
            continue
        actual = float(stats.get(key, -9999.0))
        check(abs(actual - expected) <= 0.015, f"{key} mismatch: got {actual:.3f}, expected {expected:.3f}")

    if failures:
        detail = "\n  - ".join(failures)
        raise RuntimeError(f"{preset} scene stats validation failed:\n  - {detail}\nstats={stats}")
    return stats


def run_demo(args: argparse.Namespace) -> None:
    if args.verify_only:
        return
    if not args.demo:
        raise RuntimeError("--demo is required unless --verify-only is set")

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [
        args.demo,
        f"--preset={args.preset}",
        f"--screenshot={output}",
        f"--width={args.width}",
        f"--height={args.height}",
    ]
    if args.stats_output:
        Path(args.stats_output).parent.mkdir(parents=True, exist_ok=True)
        command.append(f"--stats={args.stats_output}")
    if args.sun_azimuth is not None:
        command.append(f"--sun-azimuth={args.sun_azimuth}")
    if args.sun_elevation is not None:
        command.append(f"--sun-elevation={args.sun_elevation}")
    if args.rain is not None:
        command.append(f"--rain={args.rain}")
    if args.snow is not None:
        command.append(f"--snow={args.snow}")
    if args.fog_density is not None:
        command.append(f"--fog-density={args.fog_density}")
    if args.clouds is not None:
        command.append(f"--clouds={args.clouds}")
    subprocess.run(command, cwd=args.cwd, check=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", help="Path to badaling_demo executable.")
    parser.add_argument("--preset", required=True, choices=("clear", "cloudy", "rain", "snow", "fog", "night"))
    parser.add_argument("--output", required=True, help="Screenshot path to write/read.")
    parser.add_argument("--stats-output", help="Optional scene stats JSON path to write/read.")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--cwd", default=os.getcwd(), help="Working directory for the demo executable.")
    parser.add_argument("--min-bytes", type=int, default=90_000)
    parser.add_argument("--sun-azimuth", type=float, help="Optional sun azimuth override passed to the demo.")
    parser.add_argument("--sun-elevation", type=float, help="Optional sun elevation override passed to the demo.")
    parser.add_argument("--rain", type=float, help="Optional rain amount override passed to the demo.")
    parser.add_argument("--snow", type=float, help="Optional snow amount override passed to the demo.")
    parser.add_argument("--fog-density", type=float, help="Optional fog density override passed to the demo.")
    parser.add_argument("--clouds", type=float, help="Optional cloud amount override passed to the demo.")
    parser.add_argument("--min-cloud-clusters", type=int, default=4, help="Minimum cloud clusters expected in stats.")
    parser.add_argument("--verify-only", action="store_true", help="Only validate an existing screenshot.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        run_demo(args)
        metrics = validate(Path(args.output), args.preset, args.width, args.height, args.min_bytes)
        stats = validate_scene_stats(
            Path(args.stats_output) if args.stats_output else None,
            args.preset,
            args.sun_azimuth,
            args.sun_elevation,
            args.rain,
            args.snow,
            args.fog_density,
            args.clouds,
            args.min_cloud_clusters,
        )
    except Exception as exc:
        print(f"[badaling_verify] {exc}", file=sys.stderr)
        return 1

    stats_suffix = ""
    if args.stats_output:
        tree_count = int(stats.get("broadleaf_trees", 0)) + int(stats.get("pine_trees", 0))
        stats_suffix = (
            f" clouds={stats.get('cloud_clusters')} towers={stats.get('watch_towers')} "
            f"trees={tree_count} shrubs={stats.get('shrubs')}"
        )
    print(
        f"[badaling_verify] {args.preset}: ok "
        f"mean={metrics['mean_luma']:.1f} std={metrics['std_luma']:.1f} "
        f"unique={metrics['unique_q']:.0f} white={metrics['whiteish_ratio']:.4f} "
        f"blue={metrics['bluish_bright_ratio']:.4f} rain_edge={metrics['rain_edge_ratio']:.4f} "
        f"dark={metrics['dark_ratio']:.4f}{stats_suffix}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
