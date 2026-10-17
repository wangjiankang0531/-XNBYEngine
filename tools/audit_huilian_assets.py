#!/usr/bin/env python3
"""Audit the user-provided Huilian refinery assets and their showcase intake.

The asset drop mixes native 3ds Max files, one binary FBX, legacy SCN/SM3/SMO
packages, and texture sidecars.  HongTu can load the converted FBX through its
glTF path today.  Native MAX and legacy packages remain authoring evidence and
must pass through an explicit DCC export or a documented format decoder before
they become runtime meshes.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


SOURCE_DIR_NAME = "\u60e0\u70bc"
MODEL_EXTENSIONS = {".max", ".fbx", ".scn", ".sm3", ".smo"}
AUTHORING_EXTENSIONS = {".max", ".fbx"}
LEGACY_EXTENSIONS = {".scn", ".sm3", ".smo"}
TEXTURE_EXTENSIONS = {".dds", ".bmp", ".jpg", ".jpeg", ".png", ".psd", ".tga", ".tif", ".tiff"}
DEFAULT_SCENE = Path("data/showcase/industrial_park.scene.json")
DEFAULT_CONVERTED = Path("data/showcase/converted_huilian_assets.json")
DEFAULT_JSON = Path("data/showcase/huilian_asset_audit.json")
DEFAULT_MARKDOWN = Path("docs/project/huilian_asset_inventory.md")
DEM_RELATIVE = Path("assets") / SOURCE_DIR_NAME / "DEM-DOM" / "hero_DEM_30M.tif"
DOM_RELATIVE = Path("assets") / SOURCE_DIR_NAME / "DEM-DOM" / "hero_0.5M.tif"
PREVIEW_RELATIVE = Path("assets") / SOURCE_DIR_NAME / "DEM-DOM" / "范围预览图.png"
RUNTIME_TERRAIN_RELATIVE = Path("assets/showcase/runtime/huilian_dem_30m.bin")
RUNTIME_DOM_OVERVIEW_RELATIVE = Path("assets/showcase/runtime/huilian_dom_overview_2048.png")
RUNTIME_DOM_DETAIL_RELATIVE = Path("assets/showcase/runtime/huilian_dom_detail_valve_1024.png")
RUNTIME_DOM_MANIFEST_RELATIVE = Path("assets/showcase/runtime/huilian_dom_gpu_manifest.json")
RUNTIME_DOM_PYRAMID_RELATIVE = Path("assets/showcase/runtime/huilian_dom_pyramid")
RUNTIME_DOM_QUALITY_RELATIVE = Path("data/showcase/huilian_dom_pyramid_quality.json")


@dataclass(frozen=True)
class AssetFile:
    path: Path
    relative_path: str
    extension: str
    bytes: int
    category: str
    runtime_track: str


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def slash(path: Path) -> str:
    return path.as_posix()


def rel_to_root(root: Path, path: Path) -> str:
    return slash(path.relative_to(root))


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def classify(path: Path) -> str:
    text = "/".join(path.parts)
    if any(token in text for token in ("\u538b\u7f29\u673a", "\u8d77\u91cd\u673a", "\u7535\u673a")):
        return "rotating_lifting_equipment"
    if any(token in text for token in ("\u9600", "\u6cd5\u5170", "\u7ba1\u6865", "\u7ba1\u7ebf", "\u8f93\u6cb9\u6cf5", "\u6cf5\u68da", "\u6db2\u538b\u6cf5", "\u6c34\u7ad9\u6cf5")):
        return "piping_valves_pumps"
    if any(
        token in text
        for token in (
            "\u53cd\u5e94\u5668",
            "\u53cd\u5e94\u4ea7\u7269",
            "\u6362\u70ed\u5668",
            "\u52a0\u70ed\u7089",
            "\u5206\u79bb\u5668",
            "\u5206\u6db2\u7f50",
            "\u7f13\u51b2\u7f50",
            "\u6c7d\u63d0\u5854",
            "\u50a8\u7f50",
            "\u5bb9\u5668",
            "\u6c34\u7f50",
        )
    ):
        return "process_static_equipment"
    if any(token in text for token in ("\u4eea\u8868", "\u63a7\u5236", "\u7535\u6e90\u67dc", "\u673a\u67dc", "\u65b9\u67dc", "\u5e94\u6025\u67dc")):
        return "instrumentation_control"
    if any(token in text for token in ("\u6d88\u9632", "\u6ce1\u6cab", "\u5e94\u6025", "\u96e8\u6c34", "\u6c61\u6c34", "\u6c34\u6c60", "\u56f4\u5830")):
        return "fire_safety_environment"
    if any(
        token in text
        for token in (
            "\u5730\u9762",
            "\u5efa\u7b51",
            "\u5e73\u53f0",
            "\u5382\u533a",
            "\u95e8\u536b",
            "\u5927\u95e8",
            "\u95e8\u7981",
            "\u56f4\u680f",
            "\u98df\u5802",
            "\u64cd\u4f5c\u5ba4",
            "\u914d\u7535\u95f4",
            "\u68da",
        )
    ):
        return "site_civil_access"
    return "general_refinery_equipment"


def runtime_track(extension: str) -> str:
    if extension == ".fbx":
        return "fbx_to_gltf_runtime_lod"
    if extension == ".max":
        return "dcc_export_fbx_glb_lod_queue"
    if extension in LEGACY_EXTENSIONS:
        return "legacy_package_decode_or_matching_max_export"
    return "supporting_asset"


def scan_files(root: Path) -> list[AssetFile]:
    source = root / "assets" / SOURCE_DIR_NAME
    if not source.is_dir():
        raise FileNotFoundError(source)
    files: list[AssetFile] = []
    for path in sorted((item for item in source.rglob("*") if item.is_file()), key=lambda item: slash(item).lower()):
        extension = path.suffix.lower() or "<none>"
        files.append(
            AssetFile(
                path=path,
                relative_path=rel_to_root(root, path),
                extension=extension,
                bytes=path.stat().st_size,
                category=classify(path.relative_to(source)),
                runtime_track=runtime_track(extension),
            )
        )
    return files


def grouped(files: list[AssetFile], key_name: str) -> dict[str, dict[str, int]]:
    result: dict[str, dict[str, int]] = {}
    for asset in files:
        key = getattr(asset, key_name)
        entry = result.setdefault(key, {"count": 0, "bytes": 0})
        entry["count"] += 1
        entry["bytes"] += asset.bytes
    return dict(sorted(result.items()))


def contains_magic(path: Path, magic: bytes) -> bool:
    overlap = max(len(magic) - 1, 0)
    tail = b""
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            payload = tail + chunk
            if magic in payload:
                return True
            tail = payload[-overlap:] if overlap else b""
    return False


def tiff_metadata(path: Path) -> dict[str, Any]:
    type_sizes = {1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 11: 4, 12: 8}
    formats = {1: "B", 3: "H", 4: "I", 11: "f", 12: "d"}
    with path.open("rb") as stream:
        header = stream.read(8)
        if header[:2] == b"II":
            endian = "<"
        elif header[:2] == b"MM":
            endian = ">"
        else:
            raise RuntimeError(f"not a TIFF file: {path}")
        if struct.unpack(endian + "H", header[2:4])[0] != 42:
            raise RuntimeError(f"unsupported TIFF header: {path}")
        stream.seek(struct.unpack(endian + "I", header[4:8])[0])
        entry_count = struct.unpack(endian + "H", stream.read(2))[0]
        tags: dict[int, tuple[int, int, int, bytes]] = {}
        for _ in range(entry_count):
            entry = stream.read(12)
            tag, kind, count, offset = struct.unpack(endian + "HHII", entry)
            tags[tag] = (kind, count, offset, entry[8:12])

        def values(tag: int, default: tuple[int, ...] = ()) -> tuple[Any, ...]:
            if tag not in tags:
                return default
            kind, count, offset, inline = tags[tag]
            item_size = type_sizes[kind]
            size = item_size * count
            if size <= 4:
                payload = inline[:size]
            else:
                stream.seek(offset)
                payload = stream.read(size)
            return struct.unpack(endian + formats[kind] * count, payload)

        return {
            "width": int(values(256)[0]),
            "height": int(values(257)[0]),
            "bits_per_sample": [int(value) for value in values(258)],
            "compression": int(values(259)[0]),
            "samples_per_pixel": int(values(277, (1,))[0]),
            "sample_format": int(values(339, (1,))[0]),
            "predictor": int(values(317, (1,))[0]),
            "rows_per_strip": int(values(278, (0,))[0]),
        }


def world_file_metadata(path: Path, width: int, height: int) -> dict[str, Any]:
    values = [float(line.strip()) for line in path.read_text(encoding="ascii").splitlines() if line.strip()]
    if len(values) != 6 or abs(values[1]) > 1.0e-12 or abs(values[2]) > 1.0e-12:
        raise RuntimeError(f"rotated or malformed world file: {path}")
    pixel_x, pixel_y, origin_x, origin_y = values[0], values[3], values[4], values[5]
    return {
        "pixel_size_degrees": [pixel_x, pixel_y],
        "bounds_wgs84": [
            origin_x - pixel_x * 0.5,
            origin_y + pixel_y * (height - 0.5),
            origin_x + pixel_x * (width - 0.5),
            origin_y - pixel_y * 0.5,
        ],
    }


def geospatial_evidence(root: Path) -> dict[str, Any]:
    dem = root / DEM_RELATIVE
    dom = root / DOM_RELATIVE
    preview = root / PREVIEW_RELATIVE
    runtime = root / RUNTIME_TERRAIN_RELATIVE
    runtime_dom_overview = root / RUNTIME_DOM_OVERVIEW_RELATIVE
    runtime_dom_detail = root / RUNTIME_DOM_DETAIL_RELATIVE
    runtime_dom_manifest = root / RUNTIME_DOM_MANIFEST_RELATIVE
    runtime_dom_pyramid = root / RUNTIME_DOM_PYRAMID_RELATIVE
    runtime_dom_quality = root / RUNTIME_DOM_QUALITY_RELATIVE
    runtime_dom_page_files = sorted(runtime_dom_pyramid.glob("lod?/l?_x??_y??.jpg")) if runtime_dom_pyramid.is_dir() else []
    runtime_dom_level_files = [
        sorted((runtime_dom_pyramid / f"lod{lod}").glob(f"l{lod}_x??_y??.jpg")) for lod in range(3)
    ]
    runtime_dom_gpu = read_json(runtime_dom_manifest) if runtime_dom_manifest.is_file() else {}
    dem_info = tiff_metadata(dem)
    dom_info = tiff_metadata(dom)
    dem_world = world_file_metadata(dem.with_suffix(".tfw"), dem_info["width"], dem_info["height"])
    dom_world = world_file_metadata(dom.with_suffix(".tfw"), dom_info["width"], dom_info["height"])
    dem_projection = dem.with_suffix(".prj").read_text(encoding="ascii")
    dom_projection = dom.with_suffix(".prj").read_text(encoding="ascii")
    tolerance = max(abs(value) for value in dom_world["pixel_size_degrees"]) * 1.5
    bounds_match = all(
        abs(dem_value - dom_value) <= tolerance
        for dem_value, dom_value in zip(dem_world["bounds_wgs84"], dom_world["bounds_wgs84"])
    )
    west, south, east, north = dem_world["bounds_wgs84"]
    dom_west, dom_south, dom_east, dom_north = dom_world["bounds_wgs84"]
    dem_pixel_x, dem_pixel_y = dem_world["pixel_size_degrees"]
    first_lon = west + dem_pixel_x * 0.5
    last_lon = east - dem_pixel_x * 0.5
    first_lat = north + dem_pixel_y * 0.5
    last_lat = south - dem_pixel_y * 0.5
    covers_sample_centers = (
        dom_west - tolerance <= first_lon <= dom_east + tolerance
        and dom_west - tolerance <= last_lon <= dom_east + tolerance
        and dom_south - tolerance <= last_lat <= dom_north + tolerance
        and dom_south - tolerance <= first_lat <= dom_north + tolerance
    )
    shared_upper_left = abs(west - dom_west) <= tolerance and abs(north - dom_north) <= tolerance
    overlap_width = max(0.0, min(east, dom_east) - max(west, dom_west))
    overlap_height = max(0.0, min(north, dom_north) - max(south, dom_south))
    overlap_ratio = overlap_width * overlap_height / ((east - west) * (north - south))
    center_latitude = (south + north) * 0.5
    width_m = abs(east - west) * 111320.0 * math.cos(math.radians(center_latitude))
    height_m = abs(north - south) * 110574.0
    return {
        "dem": {
            "path": slash(DEM_RELATIVE),
            "world_file": slash(DEM_RELATIVE.with_suffix(".tfw")),
            "projection": slash(DEM_RELATIVE.with_suffix(".prj")),
            **dem_info,
            **dem_world,
            "wgs84": "WGS_1984" in dem_projection,
            "bytes": dem.stat().st_size,
            "elevation_range_m": [-18.662567, 419.034637],
        },
        "dom": {
            "path": slash(DOM_RELATIVE),
            "world_file": slash(DOM_RELATIVE.with_suffix(".tfw")),
            "projection": slash(DOM_RELATIVE.with_suffix(".prj")),
            **dom_info,
            **dom_world,
            "wgs84": "WGS_1984" in dom_projection,
            "bytes": dom.stat().st_size,
        },
        "exact_matching_wgs84_bounds": bounds_match,
        "shared_upper_left_origin": shared_upper_left,
        "covers_all_dem_sample_centers": covers_sample_centers,
        "footprint_overlap_ratio": overlap_ratio,
        "approximate_extent_m": [width_m, height_m],
        "runtime": slash(RUNTIME_TERRAIN_RELATIVE),
        "runtime_exists": runtime.is_file(),
        "runtime_bytes": runtime.stat().st_size if runtime.is_file() else 0,
        "runtime_policy": "dem_height_plus_world_file_affine_bicubic_dom_to_dem_grid",
        "runtime_dom_gpu": {
            "overview": slash(RUNTIME_DOM_OVERVIEW_RELATIVE),
            "overview_exists": runtime_dom_overview.is_file(),
            "overview_bytes": runtime_dom_overview.stat().st_size if runtime_dom_overview.is_file() else 0,
            "detail": slash(RUNTIME_DOM_DETAIL_RELATIVE),
            "detail_exists": runtime_dom_detail.is_file(),
            "detail_bytes": runtime_dom_detail.stat().st_size if runtime_dom_detail.is_file() else 0,
            "manifest": slash(RUNTIME_DOM_MANIFEST_RELATIVE),
            "manifest_exists": runtime_dom_manifest.is_file(),
            "manifest_data": runtime_dom_gpu,
            "quality_report": slash(RUNTIME_DOM_QUALITY_RELATIVE),
            "quality_report_exists": runtime_dom_quality.is_file(),
            "quality_report_data": read_json(runtime_dom_quality) if runtime_dom_quality.is_file() else {},
            "pyramid": slash(RUNTIME_DOM_PYRAMID_RELATIVE),
            "page_files": len(runtime_dom_page_files),
            "page_bytes": sum(path.stat().st_size for path in runtime_dom_page_files),
            "level_page_files": [len(files) for files in runtime_dom_level_files],
            "level_page_bytes": [sum(path.stat().st_size for path in files) for files in runtime_dom_level_files],
            "policy": "overview_plus_async_full_coverage_lod_pyramid_four_slot_priority_prefetch_crossfade",
        },
        "preview": slash(PREVIEW_RELATIVE),
        "preview_exists": preview.is_file(),
    }


def converted_assets(root: Path, converted_path: Path) -> list[dict[str, Any]]:
    if not converted_path.exists():
        return []
    rows: list[dict[str, Any]] = []
    for item in read_json(converted_path).get("assets", []):
        source = Path(item.get("input", ""))
        output = Path(item.get("output", ""))
        embedded_buffer = bool(item.get("embedded_buffer", False))
        binary = output if embedded_buffer else Path(item.get("bin", ""))
        if not source.is_absolute():
            source = root / source
        if not output.is_absolute():
            output = root / output
        if not binary.is_absolute():
            binary = root / binary
        row = dict(item)
        row["input"] = rel_to_root(root, source.resolve()) if source.exists() else slash(source)
        row["output"] = rel_to_root(root, output.resolve()) if output.exists() else slash(output)
        row["bin"] = rel_to_root(root, binary.resolve()) if binary.exists() else slash(binary)
        row["source_exists"] = source.is_file()
        row["output_exists"] = output.is_file()
        row["bin_exists"] = binary.is_file()
        row["embedded_buffer"] = embedded_buffer
        row["output_bytes"] = output.stat().st_size if output.is_file() else 0
        row["bin_bytes"] = binary.stat().st_size if binary.is_file() else 0
        row["actual_output_triangles"] = glb_triangle_count(output) if output.suffix.lower() == ".glb" else None
        rows.append(row)
    return rows


def glb_triangle_count(path: Path) -> int | None:
    if not path.is_file():
        return None
    payload = path.read_bytes()
    if len(payload) < 20 or payload[:4] != b"glTF":
        return None
    json_size, json_type = struct.unpack_from("<II", payload, 12)
    if json_type != 0x4E4F534A or 20 + json_size > len(payload):
        return None
    document = json.loads(payload[20 : 20 + json_size].decode("utf-8").rstrip(" \x00"))
    triangles = 0
    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            if primitive.get("mode", 4) != 4 or "indices" not in primitive:
                continue
            triangles += int(document["accessors"][primitive["indices"]]["count"]) // 3
    return triangles


def asset_row(asset: AssetFile) -> dict[str, Any]:
    return {
        "path": asset.relative_path,
        "extension": asset.extension,
        "bytes": asset.bytes,
        "category": asset.category,
        "runtime_track": asset.runtime_track,
    }


def build_audit(root: Path, scene_path: Path, converted_path: Path) -> dict[str, Any]:
    files = scan_files(root)
    model_files = [asset for asset in files if asset.extension in MODEL_EXTENSIONS]
    authoring = [asset for asset in files if asset.extension in AUTHORING_EXTENSIONS]
    legacy = [asset for asset in files if asset.extension in LEGACY_EXTENSIONS]
    textures = [asset for asset in files if asset.extension in TEXTURE_EXTENSIONS]
    max_files = [asset for asset in files if asset.extension == ".max"]
    fbx_files = [asset for asset in files if asset.extension == ".fbx"]
    converted = converted_assets(root, converted_path)
    source = root / "assets" / SOURCE_DIR_NAME

    max_dirs = {asset.path.parent for asset in max_files}
    legacy_with_max_peer = sum(1 for asset in legacy if asset.path.parent in max_dirs)
    legacy_dds_packages = sum(1 for asset in legacy if contains_magic(asset.path, b"DDS "))
    top_level: dict[str, dict[str, int]] = {}
    for asset in files:
        relative = asset.path.relative_to(source)
        section = relative.parts[0]
        entry = top_level.setdefault(section, {"count": 0, "bytes": 0})
        entry["count"] += 1
        entry["bytes"] += asset.bytes

    scene = read_json(scene_path)
    intake = scene.get("source_model_assets", {}).get("huilian_asset_intake", {})
    scene_terrain = scene.get("terrain", {})
    geospatial = geospatial_evidence(root)
    summary = {
        "total_files": len(files),
        "total_bytes": sum(asset.bytes for asset in files),
        "model_source_files": len(model_files),
        "model_source_bytes": sum(asset.bytes for asset in model_files),
        "authoring_files": len(authoring),
        "max_files": len(max_files),
        "fbx_files": len(fbx_files),
        "legacy_package_files": len(legacy),
        "legacy_package_bytes": sum(asset.bytes for asset in legacy),
        "legacy_packages_with_local_max_peer": legacy_with_max_peer,
        "legacy_packages_with_embedded_dds": legacy_dds_packages,
        "texture_files": len(textures),
        "texture_bytes": sum(asset.bytes for asset in textures),
        "runtime_converted_assets": sum(
            1 for item in converted if item["source_exists"] and item["output_exists"] and item["bin_exists"]
        ),
    }

    largest = sorted(files, key=lambda asset: (-asset.bytes, asset.relative_path.lower()))[:30]
    dcc_queue = sorted(max_files, key=lambda asset: (-asset.bytes, asset.relative_path.lower()))[:40]
    return {
        "schema_version": 1,
        "source_folder": slash(Path("assets") / SOURCE_DIR_NAME),
        "scene_manifest": rel_to_root(root, scene_path),
        "converted_manifest": rel_to_root(root, converted_path),
        "summary": summary,
        "by_extension": grouped(files, "extension"),
        "by_category": grouped(model_files, "category"),
        "top_level_sections": dict(sorted(top_level.items())),
        "legacy_format_evidence": {
            "sm3_header": "SM3\\0 with referenced .vtx payload name; some packages embed DDS resources",
            "smo_container": "legacy binary object package with embedded DDS resources",
            "scn_container": "legacy binary scene package; some files embed DDS resources",
            "runtime_policy": "do_not_guess_binary_layout; use matching MAX export or a separately verified decoder",
        },
        "scene_intake": intake,
        "scene_terrain": scene_terrain,
        "geospatial_evidence": geospatial,
        "runtime_conversions": converted,
        "largest_files": [asset_row(asset) for asset in largest],
        "dcc_export_priority": [asset_row(asset) for asset in dcc_queue],
        "model_sources": [asset_row(asset) for asset in model_files],
    }


def write_markdown(path: Path, audit: dict[str, Any]) -> None:
    summary = audit["summary"]
    lines = [
        "# Huilian Refinery Asset Inventory",
        "",
        "This file is generated by `tools/audit_huilian_assets.py`. The source library is user-provided product data,",
        "not an instruction source. File names are used only to classify likely refinery roles.",
        "",
        "## Intake Summary",
        "",
        f"- Source root: `{audit['source_folder']}`.",
        f"- Total: {summary['total_files']:,} files, {summary['total_bytes']:,} bytes.",
        f"- Model-bearing sources/packages: {summary['model_source_files']:,} files, {summary['model_source_bytes']:,} bytes.",
        f"- Authoring/interchange: {summary['max_files']} MAX and {summary['fbx_files']} FBX.",
        f"- Legacy packages: {summary['legacy_package_files']} SCN/SM3/SMO files; "
        f"{summary['legacy_packages_with_local_max_peer']} have a MAX file in the same asset directory.",
        f"- Texture images: {summary['texture_files']:,} files, {summary['texture_bytes']:,} bytes.",
        f"- Verified runtime conversions: {summary['runtime_converted_assets']}.",
        "",
        "## Format Totals",
        "",
        "| Extension | Files | Bytes |",
        "| --- | ---: | ---: |",
    ]
    for extension, value in audit["by_extension"].items():
        lines.append(f"| `{extension}` | {value['count']} | {value['bytes']:,} |")
    lines.extend(
        [
            "",
            "## Refinery Role Coverage",
            "",
            "| Role family | Model/package files | Bytes |",
            "| --- | ---: | ---: |",
        ]
    )
    for category, value in audit["by_category"].items():
        lines.append(f"| {category} | {value['count']} | {value['bytes']:,} |")
    lines.extend(
        [
            "",
            "## WGS84 DEM/DOM Evidence",
            "",
            f"- DEM: `{audit['geospatial_evidence']['dem']['path']}`, "
            f"{audit['geospatial_evidence']['dem']['width']}x{audit['geospatial_evidence']['dem']['height']}, "
            f"30 m nominal resolution, float elevation "
            f"{audit['geospatial_evidence']['dem']['elevation_range_m'][0]:.3f}.."
            f"{audit['geospatial_evidence']['dem']['elevation_range_m'][1]:.3f} m.",
            f"- DOM: `{audit['geospatial_evidence']['dom']['path']}`, "
            f"{audit['geospatial_evidence']['dom']['width']}x{audit['geospatial_evidence']['dom']['height']}, "
            "0.5 m nominal resolution, LZW-compressed RGB with horizontal predictor.",
            f"- DEM/DOM share the upper-left WGS84 origin and the DOM covers every DEM sample center; footprint overlap "
            f"is {audit['geospatial_evidence']['footprint_overlap_ratio'] * 100.0:.4f}%. The DOM is approximately "
            "4.8 m shorter at the east edge and 14.7 m shorter at the south edge, so runtime conversion uses the two "
            "world files for affine registration rather than width-only scaling.",
            f"- Terrain extent is approximately {audit['geospatial_evidence']['approximate_extent_m'][0]:,.1f} x "
            f"{audit['geospatial_evidence']['approximate_extent_m'][1]:,.1f} m.",
            f"- Runtime terrain: `{audit['geospatial_evidence']['runtime']}` "
            f"({audit['geospatial_evidence']['runtime_bytes']:,} bytes), with real DOM colors resampled onto the DEM grid.",
            f"- GPU DOM overview: `{audit['geospatial_evidence']['runtime_dom_gpu']['overview']}` "
            f"({audit['geospatial_evidence']['runtime_dom_gpu']['overview_bytes']:,} bytes), 2048x1386 over the full DEM "
            "footprint at approximately 5.695 m/texel.",
            f"- GPU DOM inspection detail: `{audit['geospatial_evidence']['runtime_dom_gpu']['detail']}` "
            f"({audit['geospatial_evidence']['runtime_dom_gpu']['detail_bytes']:,} bytes), 1024x1024 over a 512 m square "
            "at the source 0.5 m/texel resolution. The shader blends it into the overview with a feathered WGS84 UV window.",
            f"- GPU registration manifest: `{audit['geospatial_evidence']['runtime_dom_gpu']['manifest']}`; policy "
            "`overview_plus_async_full_coverage_lod_pyramid_four_slot_priority_prefetch_crossfade`.",
            f"- Full-coverage GPU DOM tile pyramid: `{audit['geospatial_evidence']['runtime_dom_gpu']['pyramid']}` "
            "contains three levels / "
            f"{audit['geospatial_evidence']['runtime_dom_gpu']['page_files']} pages / "
            f"{audit['geospatial_evidence']['runtime_dom_gpu']['page_bytes']:,} bytes. LOD0/1/2 use 0.5/1/2 m per "
            "texel and 46x31 / 23x16 / 12x8 grids. The camera asynchronously decodes a 2x2 working set, builds an "
            "11-level mip chain, and commits into a four-slot / 22,369,616-byte priority-aware GPU LRU cache after "
            "the prior frame fence completes. A hash page table promotes any resident page group without JPEG decode; "
            "directional camera-velocity prefetch is evicted before foreground content, and foreground requests preempt "
            "prefetch work. The current and active-transition slots are protected during the 0.35 s shader cross-fade. "
            "Epoch checkpoints cooperatively cancel obsolete page/mip work; the 12-station probe and 60-second fixed "
            "flight record resident, upload, cache, frame-time and VK_EXT_memory_budget telemetry. Vulkan textures own "
            "and release their VMA image and image view, and the second half of the endurance run enforces a memory plateau.",
            f"- Pixel/seam quality report: `{audit['geospatial_evidence']['runtime_dom_gpu']['quality_report']}`; "
            f"passed={audit['geospatial_evidence']['runtime_dom_gpu']['quality_report_data'].get('passed', False)}. "
            "Representative pages and horizontal/vertical seams are compared against fresh affine resampling from "
            "the source TIFF with a mean absolute error limit of 3.0.",
            "",
            "## Runtime Assets Now Used by the DEMO",
            "",
        ]
    )
    for item in audit["runtime_conversions"]:
        buffer_text = "self-contained GLB" if item.get("embedded_buffer") else f"plus `{item['bin']}`"
        lines.extend(
            [
                f"- Source: `{item['input']}`.",
                f"- Runtime: `{item['output']}` ({buffer_text}).",
                f"- Catalogue: {item.get('geometry_count', 0):,} source geometries and "
                f"{item.get('source_triangle_count', 0):,} source triangles.",
                f"- Selected equipment: `{item.get('selected_equipment_display_name', 'unknown')}`, "
                f"{item.get('selected_mesh_count', 0)} mesh, {item.get('output_triangle_count', 0):,} triangles.",
                f"- Runtime policy: `{item.get('triangle_lod', 'unknown')}`; scene scale "
                f"{item.get('scene_scale', 1.0):g}.",
            ]
        )
    lines.extend(
        [
            "",
            "## Legacy Package Decision",
            "",
            "`.SM3`, `.SMO`, and `.SCN` are binary legacy containers. Samples show an `SM3\\0` header, `.vtx` payload",
            "references, and/or embedded DDS resources, but there is no verified HongTu loader or stable public schema in",
            "this repository. They are therefore not parsed speculatively. The primary path is matching MAX -> FBX/GLB ->",
            "glTF with material review and LOD partitioning; a decoder is acceptable only after fixture-based verification.",
            "",
            "## Largest DCC Export Candidates",
            "",
            "| Source | Category | Bytes | Track |",
            "| --- | --- | ---: | --- |",
        ]
    )
    for item in audit["dcc_export_priority"][:20]:
        lines.append(
            f"| `{item['path']}` | {item['category']} | {item['bytes']:,} | {item['runtime_track']} |"
        )
    lines.extend(
        [
            "",
            "## Reproducible Gates",
            "",
            "The DEM/DOM rebuild requires Pillow to decode the trusted LZW-compressed source DOM. The Codex workspace",
            "Python runtime includes it; a standalone Python environment must install Pillow before running this gate.",
            "",
            "```powershell",
            "cmake --build build --config Release --target assimp_asset_converter mesh_decimator",
            "python tools\\build_huilian_dem_asset.py",
            "build\\tools\\assimp_asset_converter\\Release\\assimp_asset_converter.exe "
            "--input=\"assets\\惠炼\\HERO二期\\F-电-绿青-7___未完成\\F-电-绿青-71.FBX\" "
            "--output=work\\huilian_wz_check_valve.gltf --scale=1.0 --strip-textures --center-ground "
            "--include-name=WZ止回阀绿",
            "build\\tools\\mesh_decimator\\Release\\mesh_decimator.exe "
            "work\\huilian_wz_check_valve.gltf assets\\showcase\\runtime\\huilian_wz_check_valve_runtime.glb "
            "--ratio=1.0 --error=0.001",
            "python tools\\inspect_gltf_geometry.py assets\\showcase\\runtime\\huilian_wz_check_valve_runtime.glb",
            "build\\tools\\assimp_asset_converter\\Release\\assimp_asset_converter.exe "
            "--input=\"assets\\惠炼\\HERO二期\\F-电-绿青-7___未完成\\F-电-绿青-71.FBX\" "
            "--output=assets\\showcase\\runtime\\huilian_production_water_pump_p0401_runtime.glb "
            "--scale=1.0 --strip-textures --center-ground --include-source-name=生产水泵P-0401",
            "python tools\\inspect_gltf_geometry.py "
            "assets\\showcase\\runtime\\huilian_production_water_pump_p0401_runtime.glb",
            "python tools\\build_huilian_equipment_materials.py --check",
            "python tools\\audit_huilian_assets.py --check",
            "```",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def validate(audit: dict[str, Any]) -> list[str]:
    summary = audit["summary"]
    intake = audit["scene_intake"]
    scene_terrain = audit["scene_terrain"]
    geospatial = audit["geospatial_evidence"]
    failures: list[str] = []
    expected = {
        "total_files": summary["total_files"],
        "total_bytes": summary["total_bytes"],
        "model_source_files": summary["model_source_files"],
        "max_files": summary["max_files"],
        "fbx_files": summary["fbx_files"],
        "legacy_package_files": summary["legacy_package_files"],
        "texture_files": summary["texture_files"],
    }
    for key, value in expected.items():
        if intake.get(key) != value:
            failures.append(f"scene huilian_asset_intake.{key}={intake.get(key)!r}, expected {value!r}")
    if summary["runtime_converted_assets"] < summary["fbx_files"]:
        failures.append("not every Huilian FBX has at least one verified glTF runtime conversion")
    if intake.get("direct_runtime_assets") != summary["runtime_converted_assets"]:
        failures.append("scene Huilian direct-runtime asset count does not match the conversion manifest")
    if intake.get("runtime_instances") != summary["runtime_converted_assets"]:
        failures.append("scene Huilian runtime-instance count does not match the conversion manifest")
    if intake.get("runtime_triangles") != sum(
        int(item.get("output_triangle_count", 0)) for item in audit["runtime_conversions"]
    ):
        failures.append("scene Huilian runtime triangle total does not match the conversion manifest")
    if intake.get("material_manifest") != "data/showcase/huilian_equipment_material_manifest.json":
        failures.append("scene Huilian equipment material manifest is not registered")
    if summary["legacy_packages_with_local_max_peer"] <= 0:
        failures.append("legacy packages were not linked to local MAX authoring peers")
    if not geospatial["dem"]["wgs84"] or not geospatial["dom"]["wgs84"]:
        failures.append("Huilian DEM/DOM projection is not WGS84")
    if not geospatial["shared_upper_left_origin"]:
        failures.append("Huilian DEM and DOM do not share the expected upper-left WGS84 origin")
    if not geospatial["covers_all_dem_sample_centers"]:
        failures.append("Huilian DOM does not cover every DEM runtime sample center")
    if geospatial["footprint_overlap_ratio"] < 0.995:
        failures.append("Huilian DEM/DOM WGS84 footprint overlap is unexpectedly low")
    if [geospatial["dem"]["width"], geospatial["dem"]["height"]] != [409, 257]:
        failures.append("Huilian DEM grid does not match the audited 409x257 source")
    if [geospatial["dom"]["width"], geospatial["dom"]["height"]] != [21170, 13283]:
        failures.append("Huilian DOM grid does not match the audited 21170x13283 source")
    if geospatial["dom"]["compression"] != 5 or geospatial["dom"]["predictor"] != 2:
        failures.append("Huilian DOM LZW/predictor evidence changed")
    if not geospatial["runtime_exists"] or geospatial["runtime_bytes"] < 700_000:
        failures.append("Huilian DEM/DOM runtime terrain package is missing")
    runtime_dom_gpu = geospatial["runtime_dom_gpu"]
    if not (
        runtime_dom_gpu["overview_exists"]
        and runtime_dom_gpu["detail_exists"]
        and runtime_dom_gpu["manifest_exists"]
    ):
        failures.append("Huilian paged GPU DOM runtime texture set is incomplete")
    if runtime_dom_gpu["page_files"] != 1890:
        failures.append("Huilian full-coverage GPU DOM tile pyramid must contain exactly 1890 pages")
    if runtime_dom_gpu["level_page_files"] != [1426, 368, 96]:
        failures.append("Huilian GPU DOM tile-pyramid level file counts changed")
    if runtime_dom_gpu["page_bytes"] != 150_922_432:
        failures.append("Huilian GPU DOM tile-pyramid byte count changed")
    quality_report = runtime_dom_gpu["quality_report_data"]
    if not runtime_dom_gpu["quality_report_exists"] or quality_report.get("passed") is not True:
        failures.append("Huilian GPU DOM pixel/seam quality report is missing or failed")
    if quality_report.get("schema") != "hongtu.huilian_dom_pyramid_quality.v5":
        failures.append("Huilian GPU DOM pixel/seam quality report schema mismatch")
    if quality_report.get("total_page_count") != runtime_dom_gpu["page_files"]:
        failures.append("Huilian GPU DOM quality report page count mismatch")
    if quality_report.get("total_page_bytes") != runtime_dom_gpu["page_bytes"]:
        failures.append("Huilian GPU DOM quality report byte count mismatch")
    gpu_manifest = runtime_dom_gpu["manifest_data"]
    if gpu_manifest.get("schema") != "hongtu.huilian_dom_gpu_textures.v7":
        failures.append("Huilian GPU DOM registration manifest schema mismatch")
    if gpu_manifest.get("runtime_sampling") != (
        "overview_plus_async_full_coverage_lod_pyramid_four_slot_priority_prefetch_crossfade"
    ):
        failures.append("Huilian GPU DOM runtime sampling policy mismatch")
    if gpu_manifest.get("overview", {}).get("grid") != [2048, 1386]:
        failures.append("Huilian GPU DOM overview grid mismatch")
    if gpu_manifest.get("detail", {}).get("grid") != [1024, 1024]:
        failures.append("Huilian GPU DOM detail grid mismatch")
    if gpu_manifest.get("detail", {}).get("resolution_m_per_texel") != [0.5, 0.5]:
        failures.append("Huilian GPU DOM detail does not preserve the audited 0.5 m resolution")
    tile_pyramid = gpu_manifest.get("tile_pyramid", {})
    if tile_pyramid.get("coverage") != "full_dem_bounds_with_page_aligned_east_north_padding":
        failures.append("Huilian GPU DOM tile pyramid does not cover the full DEM bounds")
    if tile_pyramid.get("level_count") != 3 or tile_pyramid.get("total_page_count") != 1890:
        failures.append("Huilian GPU DOM tile-pyramid level or page count mismatch")
    if tile_pyramid.get("total_page_bytes") != runtime_dom_gpu["page_bytes"]:
        failures.append("Huilian GPU DOM tile-pyramid manifest byte count mismatch")
    levels = tile_pyramid.get("levels", [])
    if [level.get("grid") for level in levels] != [[46, 31], [23, 16], [12, 8]]:
        failures.append("Huilian GPU DOM tile-pyramid grids mismatch")
    if [level.get("page_count") for level in levels] != [1426, 368, 96]:
        failures.append("Huilian GPU DOM tile-pyramid per-level page counts mismatch")
    if [level.get("resolution_m_per_texel") for level in levels] != [[0.5, 0.5], [1.0, 1.0], [2.0, 2.0]]:
        failures.append("Huilian GPU DOM tile-pyramid resolutions mismatch")
    if tile_pyramid.get("page_grid_px") != [512, 512]:
        failures.append("Huilian GPU DOM page pixel dimensions mismatch")
    if (
        tile_pyramid.get("resident_working_set_pages") != [2, 2]
        or tile_pyramid.get("resident_slot_count") != 4
        or tile_pyramid.get("resident_base_level_gpu_bytes_per_slot") != 4_194_304
        or tile_pyramid.get("resident_gpu_bytes_per_slot") != 5_592_404
        or tile_pyramid.get("resident_gpu_budget_bytes") != 22_369_616
        or tile_pyramid.get("resident_mip_levels") != 11
    ):
        failures.append("Huilian GPU DOM resident working-set budget mismatch")
    if tile_pyramid.get("sampler") != "trilinear_16x_anisotropic_clamp_to_edge":
        failures.append("Huilian GPU DOM trilinear/anisotropic sampler policy mismatch")
    if tile_pyramid.get("lod_height_thresholds_m") != [250.0, 700.0]:
        failures.append("Huilian GPU DOM LOD height thresholds mismatch")
    if tile_pyramid.get("lod_hysteresis_m") != [50.0, 75.0]:
        failures.append("Huilian GPU DOM LOD hysteresis mismatch")
    if tile_pyramid.get("cpu_loading") != "std_async_stb_jpeg_decode_cpu_box_mip_chain":
        failures.append("Huilian GPU DOM asynchronous CPU loading policy mismatch")
    if tile_pyramid.get("request_policy") != (
        "foreground_preempts_directional_prefetch_epoch_cancel_stale_result_discard"
    ):
        failures.append("Huilian GPU DOM foreground-priority request policy mismatch")
    if tile_pyramid.get("request_scheduler") != "single_worker_foreground_preempts_directional_prefetch":
        failures.append("Huilian GPU DOM priority request scheduler mismatch")
    if tile_pyramid.get("prefetch_policy") != "camera_velocity_same_lod_one_atlas_lookahead":
        failures.append("Huilian GPU DOM directional prefetch policy mismatch")
    if tile_pyramid.get("cooperative_cancellation") != "epoch_token_page_mip_delay_checkpoints":
        failures.append("Huilian GPU DOM cooperative cancellation policy mismatch")
    if tile_pyramid.get("cache_policy") != "priority_aware_lru_hash_page_table":
        failures.append("Huilian GPU DOM priority-aware LRU cache policy mismatch")
    if tile_pyramid.get("page_table") != "cpu_hash_residency_to_gpu_atlas_slot":
        failures.append("Huilian GPU DOM residency page-table policy mismatch")
    if tile_pyramid.get("eviction_policy") != (
        "prefetched_first_then_true_lru_excluding_current_and_active_transition"
    ):
        failures.append("Huilian GPU DOM transition-safe LRU eviction policy mismatch")
    if tile_pyramid.get("stream_telemetry") != (
        "resident_upload_cache_frame_time_driver_budget_endurance_accounting"
    ):
        failures.append("Huilian GPU DOM stream telemetry policy mismatch")
    if tile_pyramid.get("driver_memory_budget") != "optional_vk_ext_memory_budget_device_local_heaps":
        failures.append("Huilian GPU DOM driver memory-budget policy mismatch")
    if tile_pyramid.get("driver_resource_lifetime") != "vulkan_texture_raii_vma_image_view_release":
        failures.append("Huilian GPU DOM Vulkan texture lifetime policy mismatch")
    if (
        tile_pyramid.get("endurance_probe_seconds") != 60
        or tile_pyramid.get("driver_peak_growth_allowance_bytes") != 89_478_464
        or tile_pyramid.get("driver_plateau_growth_allowance_bytes") != 44_739_232
    ):
        failures.append("Huilian GPU DOM endurance memory-plateau budget mismatch")
    if tile_pyramid.get("cache_probe_station_count") != 12:
        failures.append("Huilian GPU DOM cache probe station count mismatch")
    if tile_pyramid.get("crossfade_seconds") != 0.35:
        failures.append("Huilian GPU DOM cross-fade duration mismatch")
    if tile_pyramid.get("transition_metadata_grid_px") != [3, 1]:
        failures.append("Huilian GPU DOM transition metadata grid mismatch")
    if tile_pyramid.get("gpu_commit") != "main_thread_fence_safe_descriptor_swap":
        failures.append("Huilian GPU DOM fence-safe commit policy mismatch")
    if scene_terrain.get("huilian_bin") != geospatial["runtime"]:
        failures.append("scene Huilian terrain runtime does not match the audited package")
    scene_dom_gpu = scene_terrain.get("huilian_dom_gpu", {})
    if scene_dom_gpu.get("manifest") != runtime_dom_gpu["manifest"]:
        failures.append("scene Huilian GPU DOM manifest does not match the audited package")
    if scene_dom_gpu.get("overview") != runtime_dom_gpu["overview"]:
        failures.append("scene Huilian GPU DOM overview does not match the audited package")
    if scene_dom_gpu.get("detail") != runtime_dom_gpu["detail"]:
        failures.append("scene Huilian GPU DOM detail does not match the audited package")
    if scene_dom_gpu.get("sampling") != runtime_dom_gpu["policy"]:
        failures.append("scene Huilian GPU DOM sampling policy does not match the audit")
    scene_pyramid = scene_dom_gpu.get("tile_pyramid", {})
    if scene_pyramid.get("directory") != runtime_dom_gpu["pyramid"]:
        failures.append("scene Huilian GPU DOM tile-pyramid directory does not match the audit")
    if scene_pyramid.get("total_page_count") != runtime_dom_gpu["page_files"]:
        failures.append("scene Huilian GPU DOM tile-pyramid page count does not match the audit")
    if scene_pyramid.get("total_page_bytes") != runtime_dom_gpu["page_bytes"]:
        failures.append("scene Huilian GPU DOM tile-pyramid byte count does not match the audit")
    for field in (
        "resident_slot_count",
        "resident_base_level_gpu_bytes_per_slot",
        "resident_gpu_bytes_per_slot",
        "resident_gpu_budget_bytes",
        "resident_mip_levels",
        "sampler",
        "selection",
        "lod_height_thresholds_m",
        "lod_hysteresis_m",
        "cpu_loading",
        "request_policy",
        "request_scheduler",
        "prefetch_policy",
        "cooperative_cancellation",
        "cache_policy",
        "page_table",
        "eviction_policy",
        "stream_telemetry",
        "driver_memory_budget",
        "driver_resource_lifetime",
        "endurance_probe_seconds",
        "driver_peak_growth_allowance_bytes",
        "driver_plateau_growth_allowance_bytes",
        "cache_probe_station_count",
        "crossfade_seconds",
        "transition_metadata_grid_px",
        "gpu_commit",
    ):
        if scene_pyramid.get(field) != tile_pyramid.get(field):
            failures.append(f"scene Huilian GPU DOM tile-pyramid {field} does not match the manifest")
    source = scene_terrain.get("huilian_source", {})
    if source.get("dem") != geospatial["dem"]["path"] or source.get("dom") != geospatial["dom"]["path"]:
        failures.append("scene Huilian DEM/DOM source paths do not match the audit")
    expected_runtime_assets = {
        "WZ_check_valve_green": {
            "triangles": 400,
            "policy": "assimp_pretransform_center_ground_named_wz_mesh_lossless",
        },
        "production_water_pump_p0401": {
            "triangles": 512,
            "policy": "assimp_source_mesh_filter_pretransform_center_ground_named_pump_lossless",
        },
    }
    active_assets = intake.get("active_runtime_assets", [])
    active_by_equipment = {
        item.get("selected_equipment"): item for item in active_assets if isinstance(item, dict)
    }
    if set(active_by_equipment) != set(expected_runtime_assets):
        failures.append("scene Huilian active-runtime asset list must contain the verified valve and pump")
    compatibility_active = intake.get("active_runtime_asset", {})
    if compatibility_active.get("selected_equipment") != "WZ_check_valve_green":
        failures.append("scene Huilian compatibility runtime asset must remain the verified WZ check valve")
    converted_equipment = {item.get("selected_equipment") for item in audit["runtime_conversions"]}
    if converted_equipment != set(expected_runtime_assets):
        failures.append("Huilian conversion manifest must contain exactly the verified valve and pump")
    for item in audit["runtime_conversions"]:
        if not (item["source_exists"] and item["output_exists"] and item["bin_exists"]):
            failures.append(f"incomplete runtime conversion: {item.get('output')}")
        if int(item.get("source_triangle_count", 0)) < 100_000:
            failures.append("Huilian FBX source triangle count is unexpectedly low")
        equipment = item.get("selected_equipment")
        expected_asset = expected_runtime_assets.get(equipment)
        if expected_asset is None:
            failures.append(f"Huilian runtime selection is not a verified equipment asset: {equipment!r}")
            continue
        if int(item.get("selected_mesh_count", 0)) != 1:
            failures.append("Huilian runtime selection must contain exactly one named source mesh")
        if int(item.get("output_triangle_count", 0)) != expected_asset["triangles"]:
            failures.append(f"Huilian {equipment} triangle count is not the verified lossless value")
        if int(item.get("actual_output_triangles") or 0) != int(item.get("output_triangle_count", 0)):
            failures.append("Huilian GLB triangle count does not match its conversion manifest")
        if item.get("triangle_lod") != expected_asset["policy"]:
            failures.append(f"Huilian {equipment} runtime conversion policy mismatch")
        active = active_by_equipment.get(equipment, {})
        if active.get("runtime") != item.get("output"):
            failures.append(f"scene Huilian {equipment} runtime asset does not match the conversion manifest")
    pump = next(
        (item for item in audit["runtime_conversions"] if item.get("selected_equipment") == "production_water_pump_p0401"),
        {},
    )
    if pump.get("source_texture_available") is not False:
        failures.append("Huilian pump must not claim that its missing source diffuse texture is available")
    if pump.get("material_policy") != "real_geometry_legacy_diffuse_proxy_explicit_metallic_roughness":
        failures.append("Huilian pump material policy must disclose the legacy diffuse proxy")
    return failures


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=project_root())
    parser.add_argument("--scene", type=Path, default=DEFAULT_SCENE)
    parser.add_argument("--converted", type=Path, default=DEFAULT_CONVERTED)
    parser.add_argument("--write-json", type=Path, default=DEFAULT_JSON)
    parser.add_argument("--write-markdown", type=Path, default=DEFAULT_MARKDOWN)
    parser.add_argument("--check", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    scene = args.scene if args.scene.is_absolute() else root / args.scene
    converted = args.converted if args.converted.is_absolute() else root / args.converted
    audit = build_audit(root, scene, converted)

    json_path = args.write_json if args.write_json.is_absolute() else root / args.write_json
    json_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(audit, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    if args.write_markdown:
        markdown_path = args.write_markdown if args.write_markdown.is_absolute() else root / args.write_markdown
        write_markdown(markdown_path, audit)

    summary = audit["summary"]
    print(
        "Huilian asset audit: "
        f"{summary['total_files']} files, {summary['model_source_files']} model sources/packages, "
        f"{summary['max_files']} MAX, {summary['fbx_files']} FBX, "
        f"{summary['legacy_package_files']} legacy packages, "
        f"{summary['runtime_converted_assets']} runtime conversions"
    )
    failures = validate(audit) if args.check else []
    if failures:
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
