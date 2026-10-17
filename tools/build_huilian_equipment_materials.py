#!/usr/bin/env python3
"""Build traceable runtime base-color textures for the Huilian equipment slice.

The source library contains legacy DDS diffuse maps rather than a complete
metallic/roughness PBR package.  This tool decodes the two maps used by the
first valve/pump inspection increment into portable PNGs and records hashes;
metallic and roughness remain explicit runtime factors in showcase_demo.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image


TEXTURES = (
    (
        Path("assets/惠炼/HERO二期/F-电-绿青-7___未完成/8LS4LS绿.dds"),
        Path("assets/showcase/runtime/materials/huilian/8ls4ls_green_basecolor.png"),
        "legacy_green_valve_diffuse_proxy",
    ),
    (
        Path("assets/惠炼/HERO二期/F-电-绿青-7___未完成/法兰_16LS_浅蓝1.dds"),
        Path("assets/showcase/runtime/materials/huilian/flange_16ls_light_blue_basecolor.png"),
        "legacy_light_blue_flange_diffuse_proxy",
    ),
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def build(root: Path, manifest_path: Path) -> dict[str, object]:
    entries: list[dict[str, object]] = []
    for relative_source, relative_output, role in TEXTURES:
        source = root / relative_source
        output = root / relative_output
        if not source.is_file():
            raise FileNotFoundError(f"missing Huilian material source: {source}")
        output.parent.mkdir(parents=True, exist_ok=True)
        with Image.open(source) as image:
            rgba = image.convert("RGBA")
            width, height = rgba.size
            rgba.save(output, format="PNG", optimize=True)
        entries.append(
            {
                "role": role,
                "source": relative_source.as_posix(),
                "source_sha256": sha256(source),
                "runtime": relative_output.as_posix(),
                "runtime_sha256": sha256(output),
                "width": width,
                "height": height,
                "channels": 4,
                "source_semantics": "legacy_diffuse_base_color_only",
            }
        )

    manifest = {
        "schema": "hongtu.huilian_equipment_materials.v1",
        "source_policy": "user_supplied_legacy_dds_decoded_to_runtime_png",
        "pbr_policy": "base_color_texture_plus_explicit_metallic_roughness_factors",
        "complete_pbr_texture_set": False,
        "textures": entries,
    }
    destination = root / manifest_path
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return manifest


def verify(root: Path, manifest_path: Path) -> None:
    destination = root / manifest_path
    manifest = json.loads(destination.read_text(encoding="utf-8"))
    if manifest.get("schema") != "hongtu.huilian_equipment_materials.v1":
        raise ValueError("unexpected Huilian material manifest schema")
    if manifest.get("complete_pbr_texture_set") is not False:
        raise ValueError("legacy diffuse sources must not be reported as a complete PBR texture set")
    entries = manifest.get("textures", [])
    if len(entries) != len(TEXTURES):
        raise ValueError("Huilian material manifest texture count mismatch")
    for entry in entries:
        source = root / entry["source"]
        runtime = root / entry["runtime"]
        if not source.is_file() or not runtime.is_file():
            raise FileNotFoundError(f"missing material source/runtime pair: {source}, {runtime}")
        if sha256(source) != entry["source_sha256"] or sha256(runtime) != entry["runtime_sha256"]:
            raise ValueError(f"material hash mismatch: {runtime}")
        with Image.open(runtime) as image:
            if list(image.size) != [entry["width"], entry["height"]]:
                raise ValueError(f"material dimensions changed: {runtime}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument(
        "--manifest",
        type=Path,
        default=Path("data/showcase/huilian_equipment_material_manifest.json"),
    )
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    if not args.check:
        build(root, args.manifest)
    verify(root, args.manifest)
    print(f"Huilian equipment materials verified: {root / args.manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
