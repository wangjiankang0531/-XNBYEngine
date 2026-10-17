#!/usr/bin/env python3
"""Validate that Badaling demo sun-angle controls affect the rendered frame."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

import verify_badaling_demo as demo_verify


def render_case(args: argparse.Namespace, label: str, azimuth: float, elevation: float) -> tuple[Path, Path]:
    image = Path(args.output_dir) / f"badaling_lighting_{label}.png"
    stats = Path(args.output_dir) / f"badaling_lighting_{label}.stats.json"
    image.parent.mkdir(parents=True, exist_ok=True)
    command = [
        args.demo,
        "--preset=clear",
        f"--sun-azimuth={azimuth}",
        f"--sun-elevation={elevation}",
        f"--screenshot={image}",
        f"--stats={stats}",
        f"--width={args.width}",
        f"--height={args.height}",
    ]
    subprocess.run(command, cwd=args.cwd, check=True)
    demo_verify.validate(image, "clear", args.width, args.height, args.min_bytes)
    demo_verify.validate_scene_stats(stats, "clear", azimuth, elevation)
    return image, stats


def compare_images(a: Path, b: Path) -> tuple[float, float]:
    width_a, height_a, pixels_a = demo_verify.read_png_rgb(a)
    width_b, height_b, pixels_b = demo_verify.read_png_rgb(b)
    if width_a != width_b or height_a != height_b:
        raise RuntimeError(f"image size mismatch: {width_a}x{height_a} vs {width_b}x{height_b}")

    total_delta = 0.0
    changed = 0
    for pa, pb in zip(pixels_a, pixels_b):
        la = 0.2126 * pa[0] + 0.7152 * pa[1] + 0.0722 * pa[2]
        lb = 0.2126 * pb[0] + 0.7152 * pb[1] + 0.0722 * pb[2]
        delta = abs(la - lb)
        total_delta += delta
        if delta > 10.0:
            changed += 1

    count = len(pixels_a)
    return total_delta / count, changed / count


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", required=True, help="Path to badaling_demo executable.")
    parser.add_argument("--output-dir", required=True, help="Directory for generated comparison images and stats.")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--cwd", default=".")
    parser.add_argument("--min-bytes", type=int, default=90_000)
    parser.add_argument("--min-mean-delta", type=float, default=3.0)
    parser.add_argument("--min-changed-ratio", type=float, default=0.08)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        morning, _ = render_case(args, "morning", 115.0, 18.0)
        afternoon, _ = render_case(args, "afternoon", 305.0, 64.0)
        mean_delta, changed_ratio = compare_images(morning, afternoon)
        if mean_delta < args.min_mean_delta or changed_ratio < args.min_changed_ratio:
            raise RuntimeError(
                "sun-angle image delta too small: "
                f"mean_delta={mean_delta:.3f}, changed_ratio={changed_ratio:.4f}, "
                f"required>={args.min_mean_delta}/{args.min_changed_ratio}"
            )
    except Exception as exc:
        print(f"[badaling_lighting_verify] {exc}", file=sys.stderr)
        return 1

    print(f"[badaling_lighting_verify] ok mean_delta={mean_delta:.3f} changed_ratio={changed_ratio:.4f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
