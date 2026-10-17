#!/usr/bin/env python3
"""Compare actual Vulkan UNORM/sRGB renders, including shader early-return paths."""
import argparse
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
    results = {}
    for preset in ("zhumadian", "overview"):
        captures = []
        for srgb in (False, True):
            path = output / f"{preset}_{'srgb' if srgb else 'unorm'}.png"
            path.unlink(missing_ok=True)
            command = ["--scene=industrial_park", f"--preset={preset}", "--frames=3",
                       "--width=960", "--height=600", f"--screenshot={path}"]
            if preset == "zhumadian":
                command += ["--zhumadian-detail=full", "--zhumadian-view=plant"]
            if srgb:
                command += ["--screenshot-srgb-target"]
            run_showcase_process(Path(args.demo).resolve(), command, root, timeout=90, fallback_root=root)
            width, height, pixels = read_png_rgb(path)
            assert (width, height) == (960, 600)
            captures.append(pixels)
        differences = [abs(a-b) for p, q in zip(*captures) for a, b in zip(p, q)]
        # Opaque photogrammetry must agree to attachment quantization. The
        # overview has alpha particles: sRGB attachments blend in linear light,
        # so those samples legitimately differ from legacy encoded UNORM blending.
        # A second gamma encoding raises midtones by dozens of values and fails
        # the mean-error gate in both scenes.
        mean = sum(differences) / len(differences)
        fraction = sum(d > 2 for d in differences) / len(differences)
        assert mean < 0.65 and fraction < (0.001 if preset == "zhumadian" else 0.05), (preset, mean, fraction)
        results[preset] = {"mean_absolute_channel_error": mean, "fraction_error_gt_2": fraction}
    (output / "output_encoding_metrics.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
    print("Vulkan output encoding parity PASS:", results)


if __name__ == "__main__":
    main()
