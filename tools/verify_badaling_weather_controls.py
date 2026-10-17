#!/usr/bin/env python3
"""Validate direct Badaling weather control CLI parameters."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

import verify_badaling_demo as demo_verify


def render_case(
    args: argparse.Namespace,
    label: str,
    preset: str,
    rain: float,
    snow: float,
    fog_density: float,
    clouds: float,
    min_cloud_clusters: int,
) -> dict:
    image = Path(args.output_dir) / f"badaling_weather_{label}.png"
    stats = Path(args.output_dir) / f"badaling_weather_{label}.stats.json"
    image.parent.mkdir(parents=True, exist_ok=True)
    command = [
        args.demo,
        f"--preset={preset}",
        f"--rain={rain}",
        f"--snow={snow}",
        f"--fog-density={fog_density}",
        f"--clouds={clouds}",
        f"--screenshot={image}",
        f"--stats={stats}",
        f"--width={args.width}",
        f"--height={args.height}",
    ]
    subprocess.run(command, cwd=args.cwd, check=True)
    demo_verify.validate(image, preset, args.width, args.height, args.min_bytes)
    return demo_verify.validate_scene_stats(
        stats,
        preset,
        None,
        None,
        rain,
        snow,
        fog_density,
        clouds,
        min_cloud_clusters,
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", required=True, help="Path to badaling_demo executable.")
    parser.add_argument("--output-dir", required=True, help="Directory for generated images and stats.")
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--cwd", default=".")
    parser.add_argument("--min-bytes", type=int, default=90_000)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        sparse = render_case(args, "sparse_cloud", "clear", 0.0, 0.0, 0.12, 0.25, 1)
        heavy = render_case(args, "heavy_weather", "rain", 0.70, 0.25, 0.84, 1.75, 7)
        sparse_clouds = int(sparse.get("cloud_clusters", 0))
        heavy_clouds = int(heavy.get("cloud_clusters", 0))
        if heavy_clouds - sparse_clouds < 5:
            raise RuntimeError(f"cloud amount did not change enough: sparse={sparse_clouds}, heavy={heavy_clouds}")
    except Exception as exc:
        print(f"[badaling_weather_controls_verify] {exc}", file=sys.stderr)
        return 1

    print(
        "[badaling_weather_controls_verify] ok "
        f"sparse_clouds={sparse_clouds} heavy_clouds={heavy_clouds} "
        f"rain={heavy.get('rain')} snow={heavy.get('snow')} fog={heavy.get('fog_density')}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
