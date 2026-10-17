#!/usr/bin/env python3
"""Build/check a bounded first-child OSGB replacement for the 18-root ROI.

This is an offline, static detail tier, not a recursive OSGB pager. Every
selected child must be explicitly referenced by its parent; the two tiers are
kept in separate directories so the original stage remains reproducible.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import sys
from pathlib import Path

import build_zhumadian_stage_asset as base

ATLAS_SIZE = (2048, 2048)


def select_children(roots: list[base.RootTile]) -> list[base.RootTile]:
    children = []
    for parent in roots:
        name = f"Tile_{parent.tile_id}_L{parent.level + 1}_0uu.osgb"
        references = set(re.findall(rb"[A-Za-z0-9_]+\.osgb", parent.path.read_bytes()))
        if references != {name.encode("ascii")}:
            base.fail(f"parent child references changed; refusing incomplete replacement: {parent.path}")
        child = parent.path.parent / name
        if not child.is_file():
            base.fail(f"referenced child is missing: {child}")
        children.append(base.RootTile(parent.tile_id, parent.level + 1, child))
    return children


def provenance(root: Path, roots: list[base.RootTile], children: list[base.RootTile]) -> list[dict]:
    return [
        {"tile_id": parent.tile_id,
         "parent": base.manifest_path(root, parent.path), "parent_sha256": base.sha256(parent.path),
         "child": base.manifest_path(root, child.path), "child_sha256": base.sha256(child.path),
         "child_bytes": child.path.stat().st_size, "child_level": child.level}
        for parent, child in zip(roots, children)
    ]


def geometry_bounds(path: Path) -> tuple[list[float], list[float]]:
    # Assimp exporter accessor min/max metadata is not reliable; read actual POSITION bytes.
    import struct
    doc = json.loads(path.read_text(encoding="utf-8"))
    low, high = [float("inf")] * 3, [-float("inf")] * 3
    for mesh in doc["meshes"]:
        for primitive in mesh["primitives"]:
            accessor = doc["accessors"][primitive["attributes"]["POSITION"]]
            view = doc["bufferViews"][accessor["bufferView"]]
            if accessor["type"] != "VEC3" or accessor["componentType"] != 5126:
                base.fail("child POSITION must be float VEC3")
            data = (path.parent / doc["buffers"][view["buffer"]]["uri"]).read_bytes()
            start = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
            for i in range(accessor["count"]):
                position = struct.unpack_from("<3f", data, start + i * view.get("byteStride", 12))
                low = [min(a, b) for a, b in zip(low, position)]
                high = [max(a, b) for a, b in zip(high, position)]
    for actual, expected in zip(low + high, base.EXPECTED_LOCAL_BOUNDS_MIN + base.EXPECTED_LOCAL_BOUNDS_MAX):
        if abs(actual - expected) > 0.1:
            base.fail(f"child replacement changed ROI bounds: {low}, {high}")
    return low, high


def inspect(root: Path, output: Path, records: list[dict]) -> dict:
    geometry = base.inspect_gltf(output / "zhumadian_roi18.gltf")
    if geometry["triangles"] <= 9147 or geometry["triangles"] > 100000:
        base.fail(f"child geometry outside bounded detail budget: {geometry}")
    if base.jpeg_dimensions(output / "zhumadian_roi18_atlas.jpg") != ATLAS_SIZE:
        base.fail("child atlas must be 2048x2048")
    low, high = geometry_bounds(output / "zhumadian_roi18.gltf")
    files = base.runtime_file_records(root, output)
    if sum(f["bytes"] for f in files) > 16 * 1024 * 1024:
        base.fail("child runtime package exceeds 16 MiB budget")
    return {"schema": "hongtu.zhumadian_first_child_stage.v1", "detail": "child1",
            "srs": base.EXPECTED_SRS, "srs_origin": list(base.EXPECTED_SRS_ORIGIN),
            "source_replacements": records, "source_child_count": len(records),
            "recursive_streaming": False, "partial_roi": True,
            "classification": "osgb_oblique_photogrammetry_not_dem_dom_dlg",
            "distribution_policy": "local_non_public_demo_only",
            "geometry": geometry, "bounds_min": low, "bounds_max": high,
            "atlas_size": list(ATLAS_SIZE), "files": files}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--build", action="store_true")
    action.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = base.repo_root()
    source = root / "assets/驻马店园区"
    roots, _, _ = base.audit_source(source)
    children = select_children(roots)
    records = provenance(root, roots, children)
    output = root / "assets/showcase/runtime/zhumadian/child1"
    manifest = root / "data/showcase/zhumadian_child_stage_manifest.json"
    if args.build:
        defaults = base.parse_args([])
        osg = base.resolve_path(root, defaults.osgconv)
        plugins = base.resolve_path(root, defaults.osg_plugin_dir)
        converter = base.resolve_path(root, defaults.assimp_converter)
        runtime = base.resolve_path(root, defaults.assimp_runtime_dir)
        base.require_toolchain(osg, plugins, converter, runtime)
        alias = base.ensure_ascii_source_alias(source, root / "work/zmd_source")
        work = root / "work/zmd_child_stage_convert"
        work.mkdir(parents=True, exist_ok=True)
        base.remove_known_outputs(work)
        env = os.environ.copy()
        env["PATH"] = str(osg.parent) + os.pathsep + env.get("PATH", "")
        env["OSG_LIBRARY_PATH"] = str(plugins)
        env["OSG_OPTIMIZER"] = "DEFAULT TEXTURE_ATLAS_BUILDER"
        env["OSG_NOTIFY_LEVEL"] = "WARN"
        obj = work / "zhumadian_roi18.obj"
        mapped = [alias / child.path.relative_to(source) for child in children]
        base.run_hidden([str(osg), "-O", "OutputTextureFiles", *map(str, mapped), str(obj)], work, env,
                        "first-child export")
        atlases = sorted(work.glob("atlas_*.rgb"))
        if atlases != [work / "atlas_0.rgb"] or base.sgi_dimensions(atlases[0]) != (*ATLAS_SIZE, 3):
            base.fail("unexpected first-child atlas layout")
        jpg = work / "zhumadian_roi18_atlas.jpg"
        env["OSG_OPTIMIZER"] = "OFF"
        base.run_hidden([str(osg), str(atlases[0]), str(jpg)], work, env, "first-child JPEG")
        mtl = obj.with_suffix(".mtl")
        content = mtl.read_text(encoding="utf-8")
        if "atlas_0.rgb" not in content:
            base.fail("exported material is missing the atlas reference")
        mtl.write_text(content.replace("atlas_0.rgb", jpg.name), encoding="utf-8")
        env["PATH"] = str(runtime) + os.pathsep + str(converter.parent) + os.pathsep + env["PATH"]
        base.run_hidden([str(converter), f"--input={obj}", f"--output={obj.with_suffix('.gltf')}"],
                        work, env, "first-child glTF")
        inspect(root, work, records)  # Validate before publishing generated runtime files.
        output.mkdir(parents=True, exist_ok=True)
        for name in base.RUNTIME_FILENAMES:
            shutil.copy2(work / name, output / name)
        manifest.write_text(json.dumps(inspect(root, output, records), ensure_ascii=False, indent=2) + "\n",
                            encoding="utf-8")
    expected = inspect(root, output, records)
    if json.loads(manifest.read_text(encoding="utf-8")) != expected:
        base.fail("first-child manifest differs from source or runtime; rebuild explicitly")
    print(f"Zhumadian child1 PASS: {len(children)} replacements, {expected['geometry']['triangles']} triangles, "
          "2048x2048 atlas; static bounded ROI, no recursive streaming")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (base.StageAssetError, OSError, ValueError) as exc:
        print(f"Zhumadian child1 ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
