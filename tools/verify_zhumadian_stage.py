#!/usr/bin/env python3
"""Render and verify the independent Zhumadian OSGB stage prototype.

This verifier deliberately stays separate from ``verify_showcase_demo.py``.
The generic Showcase gate requires the complete synthetic/industrial scene,
whereas the Zhumadian preset is an honest, bounded 18-root photogrammetry ROI.

Only the Python standard library and existing repository launcher/image helpers
are used so the script remains suitable for CTest on a clean build machine.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import struct
import sys
from pathlib import Path
from typing import Any

import verify_badaling_demo as image_verify
from run_showcase_demo import run_showcase_process, suppress_windows_error_dialogs


ROOT_IDS = (
    36,
    40,
    41,
    42,
    45,
    46,
    47,
    1124,
    1125,
    1126,
    1211,
    1212,
    1213,
    1214,
    1296,
    1297,
    1298,
    1299,
)
EXPECTED_SOURCE_OSGB_FILES = 531
EXPECTED_SOURCE_OSGB_BYTES = 95_582_518
EXPECTED_ATLAS_SIZE = (2048, 1024)
EXPECTED_ATLAS_MIP_LEVELS = 12
EXPECTED_EPSG = "EPSG:32650"
EXPECTED_SOURCE_ORIGIN = (225_319.5, 3_648_440.1, 30.9)
EXPECTED_WGS84_ORIGIN = (114.062119128308, 32.939760357813)

_MISSING = object()


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def pick(stats: dict[str, Any], *names: str) -> Any:
    for name in names:
        if name in stats:
            return stats[name]
    return _MISSING


def required(stats: dict[str, Any], failures: list[str], *names: str) -> Any:
    value = pick(stats, *names)
    if value is _MISSING:
        failures.append(f"missing stats field (accepted names: {', '.join(names)})")
        return None
    return value


def finite_float(value: Any, label: str, failures: list[str]) -> float:
    try:
        result = float(value)
    except (TypeError, ValueError):
        failures.append(f"{label} is not numeric: {value!r}")
        return 0.0
    if not math.isfinite(result):
        failures.append(f"{label} is not finite: {result!r}")
        return 0.0
    return result


def int_list(value: Any, label: str, failures: list[str]) -> list[int]:
    if not isinstance(value, list):
        failures.append(f"{label} is not an array: {value!r}")
        return []
    try:
        return [int(item) for item in value]
    except (TypeError, ValueError):
        failures.append(f"{label} contains a non-integer: {value!r}")
        return []


def float_vector(value: Any, label: str, size: int, failures: list[str]) -> list[float]:
    if not isinstance(value, list) or len(value) != size:
        failures.append(f"{label} must contain {size} values: {value!r}")
        return [0.0] * size
    return [finite_float(item, f"{label}[{index}]", failures) for index, item in enumerate(value)]


def read_jpeg_size(path: Path) -> tuple[int, int]:
    """Read baseline/progressive JPEG dimensions without Pillow."""

    data = path.read_bytes()
    if len(data) < 4 or data[:2] != b"\xff\xd8":
        raise RuntimeError(f"runtime atlas is not a JPEG file: {path}")

    sof_markers = {
        0xC0,
        0xC1,
        0xC2,
        0xC3,
        0xC5,
        0xC6,
        0xC7,
        0xC9,
        0xCA,
        0xCB,
        0xCD,
        0xCE,
        0xCF,
    }
    offset = 2
    while offset < len(data):
        while offset < len(data) and data[offset] != 0xFF:
            offset += 1
        while offset < len(data) and data[offset] == 0xFF:
            offset += 1
        if offset >= len(data):
            break
        marker = data[offset]
        offset += 1
        if marker in (0x01, 0xD8, 0xD9) or 0xD0 <= marker <= 0xD7:
            continue
        if offset + 2 > len(data):
            break
        segment_length = struct.unpack(">H", data[offset : offset + 2])[0]
        if segment_length < 2 or offset + segment_length > len(data):
            break
        if marker in sof_markers:
            if segment_length < 7:
                break
            height, width = struct.unpack(">HH", data[offset + 3 : offset + 7])
            return width, height
        offset += segment_length
    raise RuntimeError(f"JPEG dimensions were not found: {path}")


def resolve_child(parent: Path, child_uri: str, label: str) -> Path:
    child = (parent / child_uri).resolve()
    try:
        child.relative_to(parent.resolve())
    except ValueError as exc:
        raise RuntimeError(f"{label} escapes its runtime directory: {child_uri}") from exc
    return child


def audit_runtime_assets(runtime_root: Path, detail: str = "coarse") -> dict[str, Any]:
    expected_atlas = (2048, 2048) if detail == "child1" else EXPECTED_ATLAS_SIZE
    failures: list[str] = []
    gltf_path = runtime_root / "zhumadian_roi18.gltf"
    require(gltf_path.is_file(), f"runtime glTF missing: {gltf_path}", failures)
    if failures:
        raise RuntimeError("Zhumadian runtime asset audit failed:\n  - " + "\n  - ".join(failures))

    try:
        gltf = json.loads(gltf_path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"runtime glTF cannot be read: {gltf_path}: {exc}") from exc

    images = gltf.get("images", [])
    buffers = gltf.get("buffers", [])
    materials = gltf.get("materials", [])
    meshes = gltf.get("meshes", [])
    accessors = gltf.get("accessors", [])
    require(len(images) == 1, f"expected one atlas image, got {len(images)}", failures)
    require(len(buffers) == 1, f"expected one geometry buffer, got {len(buffers)}", failures)
    require(len(materials) >= 1, "glTF has no material", failures)
    require(len(meshes) >= 1, "glTF has no mesh", failures)

    image_uri = images[0].get("uri", "") if images else ""
    buffer_uri = buffers[0].get("uri", "") if buffers else ""
    require(image_uri == "zhumadian_roi18_atlas.jpg", f"unexpected atlas URI: {image_uri!r}", failures)
    require(buffer_uri == "zhumadian_roi18.bin", f"unexpected buffer URI: {buffer_uri!r}", failures)

    atlas_path = resolve_child(runtime_root, image_uri, "atlas") if image_uri else runtime_root / "missing-atlas"
    buffer_path = resolve_child(runtime_root, buffer_uri, "buffer") if buffer_uri else runtime_root / "missing-buffer"
    require(atlas_path.is_file(), f"runtime atlas missing: {atlas_path}", failures)
    require(buffer_path.is_file(), f"runtime geometry buffer missing: {buffer_path}", failures)
    if atlas_path.is_file():
        require(
            atlas_path.stat().st_size >= 500_000,
            f"runtime atlas is unexpectedly small: {atlas_path.stat().st_size}",
            failures,
        )
        try:
            atlas_size = read_jpeg_size(atlas_path)
        except RuntimeError as exc:
            failures.append(str(exc))
            atlas_size = (0, 0)
        require(
            atlas_size == expected_atlas,
            f"atlas size mismatch: {atlas_size} != {expected_atlas}",
            failures,
        )
    else:
        atlas_size = (0, 0)
    if buffer_path.is_file():
        declared_bytes = int(buffers[0].get("byteLength", 0)) if buffers else 0
        actual_bytes = buffer_path.stat().st_size
        require(actual_bytes >= 1_000_000, f"runtime geometry buffer is unexpectedly small: {actual_bytes}", failures)
        require(
            declared_bytes == actual_bytes,
            f"buffer byte length mismatch: glTF={declared_bytes}, file={actual_bytes}",
            failures,
        )

    primitive_count = 0
    textured_primitive_count = 0
    triangle_count = 0
    vertex_count = 0
    for mesh in meshes:
        for primitive in mesh.get("primitives", []):
            primitive_count += 1
            attributes = primitive.get("attributes", {})
            material_index = primitive.get("material")
            position_index = attributes.get("POSITION")
            uv_index = attributes.get("TEXCOORD_0")
            index_accessor = primitive.get("indices")
            if material_index is not None and uv_index is not None:
                textured_primitive_count += 1
            if isinstance(position_index, int) and 0 <= position_index < len(accessors):
                vertex_count += int(accessors[position_index].get("count", 0))
            if isinstance(index_accessor, int) and 0 <= index_accessor < len(accessors):
                triangle_count += int(accessors[index_accessor].get("count", 0)) // 3

    base_color_texture_count = 0
    for material in materials:
        pbr = material.get("pbrMetallicRoughness", {})
        if isinstance(pbr.get("baseColorTexture"), dict):
            base_color_texture_count += 1
    require(primitive_count >= 1, "glTF contains no drawable primitive", failures)
    require(textured_primitive_count == primitive_count, "not every glTF primitive has material UVs", failures)
    require(base_color_texture_count >= 1, "glTF material does not reference the real atlas", failures)
    require(vertex_count >= 20_000, f"runtime glTF vertex accessor is too small: {vertex_count}", failures)
    require(triangle_count >= 9_000, f"runtime glTF triangle count is too small: {triangle_count}", failures)

    if failures:
        raise RuntimeError("Zhumadian runtime asset audit failed:\n  - " + "\n  - ".join(failures))
    return {
        "gltf": gltf_path,
        "atlas": atlas_path,
        "atlas_size": atlas_size,
        "buffer": buffer_path,
        "vertices": vertex_count,
        "triangles": triangle_count,
        "primitives": primitive_count,
    }


def audit_source_roi(source_root: Path) -> dict[str, int]:
    failures: list[str] = []
    metadata_path = source_root / "metadata.xml"
    data_root = source_root / "Data"
    require(metadata_path.is_file(), f"ContextCapture metadata missing: {metadata_path}", failures)
    require(data_root.is_dir(), f"ContextCapture Data directory missing: {data_root}", failures)

    if metadata_path.is_file():
        metadata = metadata_path.read_text(encoding="utf-8", errors="replace")
        require(EXPECTED_EPSG in metadata, f"metadata does not declare {EXPECTED_EPSG}", failures)
        require("225319.5,3648440.1000000001,30.899999999999999" in metadata, "metadata SRS origin changed", failures)

    files: list[Path] = []
    for root_id in ROOT_IDS:
        tile_dir = data_root / f"Tile_{root_id}"
        require(tile_dir.is_dir(), f"selected source root missing: {tile_dir}", failures)
        if not tile_dir.is_dir():
            continue
        tile_files = sorted(tile_dir.glob("*.osgb"))
        require(bool(tile_files), f"selected source root has no OSGB files: {tile_dir}", failures)
        files.extend(tile_files)

    source_bytes = sum(path.stat().st_size for path in files)
    require(
        len(files) == EXPECTED_SOURCE_OSGB_FILES,
        f"selected ROI OSGB count mismatch: {len(files)} != {EXPECTED_SOURCE_OSGB_FILES}",
        failures,
    )
    require(
        source_bytes == EXPECTED_SOURCE_OSGB_BYTES,
        f"selected ROI byte count mismatch: {source_bytes} != {EXPECTED_SOURCE_OSGB_BYTES}",
        failures,
    )
    if failures:
        raise RuntimeError("Zhumadian source ROI audit failed:\n  - " + "\n  - ".join(failures))
    return {"files": len(files), "bytes": source_bytes}


def validate_stats(path: Path, expected_view: str, detail: str = "coarse") -> dict[str, Any]:
    if not path.is_file():
        raise RuntimeError(f"stats missing: {path}")
    try:
        stats = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as exc:
        raise RuntimeError(f"stats JSON cannot be read: {path}: {exc}") from exc

    failures: list[str] = []
    expected_atlas = (2048, 2048) if detail == "child1" else EXPECTED_ATLAS_SIZE
    require(stats.get("zhumadian_stage_detail") == detail, "detail tier mismatch", failures)
    expected_triangles = 23189 if detail == "child1" else 9147
    require(stats.get("zhumadian_stage_triangles") == expected_triangles, "detail geometry mismatch", failures)
    require(stats.get("scene") == "industrial_park", f"scene mismatch: {stats.get('scene')!r}", failures)
    require(stats.get("preset") == "zhumadian", f"preset mismatch: {stats.get('preset')!r}", failures)
    require(required(stats, failures, "zhumadian_stage_active") is True, "Zhumadian stage is not active", failures)

    source_class = str(
        required(stats, failures, "zhumadian_stage_source_class", "zhumadian_stage_source_type") or ""
    ).lower()
    require("osgb" in source_class, f"source class does not identify OSGB: {source_class!r}", failures)
    require(
        "photogrammetry" in source_class or "oblique" in source_class or "reality" in source_class,
        f"source class does not identify oblique photogrammetry/reality mesh: {source_class!r}",
        failures,
    )
    scope = str(required(stats, failures, "zhumadian_stage_scope") or "").lower()
    require(
        "18" in scope and ("root" in scope or "roi" in scope),
        f"stage scope is not the bounded 18-root ROI: {scope!r}",
        failures,
    )
    require(
        required(stats, failures, "zhumadian_stage_partial_roi") is True,
        "stage is not marked as a partial ROI",
        failures,
    )
    require(
        required(stats, failures, "zhumadian_stage_full_dataset_streaming") is False,
        "stage incorrectly claims full-dataset streaming",
        failures,
    )
    require(
        required(stats, failures, "zhumadian_stage_not_dem_dom_dlg") is True,
        "OSGB source is not explicitly distinguished from DEM/DOM/DLG",
        failures,
    )

    root_ids = int_list(required(stats, failures, "zhumadian_stage_root_ids"), "zhumadian_stage_root_ids", failures)
    require(root_ids == list(ROOT_IDS), f"stage root IDs mismatch: {root_ids}", failures)
    source_file_count = int(
        required(
            stats,
            failures,
            "zhumadian_stage_source_osgb_files",
            "zhumadian_stage_source_osgb_file_count",
        )
        or 0
    )
    source_bytes = int(required(stats, failures, "zhumadian_stage_source_osgb_bytes") or 0)
    rendered_roots = int(required(stats, failures, "zhumadian_stage_rendered_root_count") or 0)
    require(
        source_file_count == EXPECTED_SOURCE_OSGB_FILES,
        f"stats source file count mismatch: {source_file_count}",
        failures,
    )
    require(source_bytes == EXPECTED_SOURCE_OSGB_BYTES, f"stats source byte count mismatch: {source_bytes}", failures)
    require(rendered_roots == len(ROOT_IDS), f"stats rendered root count mismatch: {rendered_roots}", failures)

    epsg = str(required(stats, failures, "zhumadian_stage_epsg") or "").upper()
    require(epsg == EXPECTED_EPSG, f"stage EPSG mismatch: {epsg!r}", failures)
    source_origin = float_vector(
        required(stats, failures, "zhumadian_stage_source_origin"),
        "zhumadian_stage_source_origin",
        3,
        failures,
    )
    require(
        all(abs(actual - expected) <= 1e-3 for actual, expected in zip(source_origin, EXPECTED_SOURCE_ORIGIN)),
        f"stage source origin mismatch: {source_origin}",
        failures,
    )
    wgs84_origin = float_vector(
        required(stats, failures, "zhumadian_stage_wgs84_origin", "zhumadian_stage_wgs84_origin_approx"),
        "zhumadian_stage_wgs84_origin",
        2,
        failures,
    )
    require(
        all(abs(actual - expected) <= 1e-6 for actual, expected in zip(wgs84_origin, EXPECTED_WGS84_ORIGIN)),
        f"stage WGS84 origin mismatch: {wgs84_origin}",
        failures,
    )

    require(
        required(stats, failures, "zhumadian_stage_model_loaded", "zhumadian_stage_runtime_loaded") is True,
        "runtime glTF was not loaded",
        failures,
    )
    require(
        required(stats, failures, "zhumadian_stage_texture_loaded", "zhumadian_stage_atlas_loaded") is True,
        "real OSGB texture atlas was not loaded",
        failures,
    )
    runtime_asset = str(required(stats, failures, "zhumadian_stage_runtime_asset") or "").replace("\\", "/")
    require(
        runtime_asset.endswith("/zhumadian/" + ("child1/" if detail == "child1" else "") + "zhumadian_roi18.gltf"),
        f"unexpected runtime asset: {runtime_asset!r}",
        failures,
    )
    textured_primitives = int(required(stats, failures, "zhumadian_stage_textured_primitives") or 0)
    untextured_primitives = int(required(stats, failures, "zhumadian_stage_untextured_primitives") or 0)
    require(textured_primitives >= 1, "no textured runtime primitive was reported", failures)
    require(untextured_primitives == 0, f"untextured runtime primitives reported: {untextured_primitives}", failures)
    require(
        int(required(stats, failures, "zhumadian_stage_vertices") or 0) >= 20_000,
        "runtime vertex count is too low",
        failures,
    )
    require(
        int(required(stats, failures, "zhumadian_stage_triangles") or 0) >= 9_000,
        "runtime triangle count is too low",
        failures,
    )

    atlas_size_value = pick(stats, "zhumadian_stage_atlas_size_px", "zhumadian_stage_atlas_dimensions")
    if atlas_size_value is _MISSING:
        atlas_size = [
            int(required(stats, failures, "zhumadian_stage_atlas_width") or 0),
            int(required(stats, failures, "zhumadian_stage_atlas_height") or 0),
        ]
    else:
        atlas_size = int_list(atlas_size_value, "zhumadian_stage_atlas_size_px", failures)
    require(atlas_size == list(expected_atlas), f"stats atlas size mismatch: {atlas_size}", failures)
    mip_levels = int(required(stats, failures, "zhumadian_stage_atlas_mip_levels") or 0)
    require(mip_levels == EXPECTED_ATLAS_MIP_LEVELS, f"atlas mip level count mismatch: {mip_levels}", failures)
    require(
        float(required(stats, failures, "zhumadian_stage_atlas_anisotropy") or 0.0) >= 8.0,
        "atlas anisotropic filtering is below 8x",
        failures,
    )
    require(
        required(stats, failures, "zhumadian_stage_texture_fallback") is False,
        "runtime fell back to a diagnostic texture",
        failures,
    )

    bounds_min = float_vector(
        required(stats, failures, "zhumadian_stage_bounds_min_m", "zhumadian_stage_bounds_min"),
        "zhumadian_stage_bounds_min_m",
        3,
        failures,
    )
    bounds_max = float_vector(
        required(stats, failures, "zhumadian_stage_bounds_max_m", "zhumadian_stage_bounds_max"),
        "zhumadian_stage_bounds_max_m",
        3,
        failures,
    )
    extents = [maximum - minimum for minimum, maximum in zip(bounds_min, bounds_max)]
    require(
        extents[0] >= 250.0 and extents[1] >= 290.0 and extents[2] >= 50.0,
        f"runtime bounds are too small: {extents}",
        failures,
    )

    view = str(required(stats, failures, "zhumadian_stage_view", "zhumadian_view") or "")
    require(view == expected_view, f"Zhumadian view mismatch: {view!r} != {expected_view!r}", failures)
    near_clip = finite_float(stats.get("camera_near_clip_m"), "camera_near_clip_m", failures)
    far_clip = finite_float(stats.get("camera_far_clip_m"), "camera_far_clip_m", failures)
    maximum_distance = finite_float(
        stats.get("camera_maximum_scene_distance_m"),
        "camera_maximum_scene_distance_m",
        failures,
    )
    far_margin = finite_float(stats.get("camera_far_clip_margin_m"), "camera_far_clip_margin_m", failures)
    require(near_clip > 0.0, f"near clip is invalid: {near_clip}", failures)
    require(far_clip > near_clip, f"far clip does not exceed near clip: {near_clip}, {far_clip}", failures)
    require(maximum_distance > 0.0, f"maximum scene distance is invalid: {maximum_distance}", failures)
    require(
        far_clip > maximum_distance,
        f"far clip does not cover ROI: far={far_clip}, max={maximum_distance}",
        failures,
    )
    require(far_margin >= 50.0, f"far-clip safety margin is too small: {far_margin}", failures)
    require(
        far_clip - maximum_distance >= min(far_margin, 50.0) - 1e-3,
        "reported far-clip margin is not reflected by the projection: "
        f"far={far_clip}, max={maximum_distance}, margin={far_margin}",
        failures,
    )
    explicit_coverage = pick(stats, "zhumadian_stage_camera_clip_coverage", "camera_far_clip_coverage_pass")
    if explicit_coverage is not _MISSING:
        require(explicit_coverage is True, "explicit camera clipping coverage gate failed", failures)

    license_state = str(required(stats, failures, "zhumadian_stage_license_state") or "").lower().replace("-", "_")
    require(
        "local" in license_state and ("only" in license_state or "non_public" in license_state),
        f"source is not marked local/non-public-only: {license_state!r}",
        failures,
    )

    if failures:
        raise RuntimeError(
            "Zhumadian stage stats validation failed:\n  - " + "\n  - ".join(failures) + f"\nstats={stats}"
        )
    return stats


def validate_image(path: Path, width: int, height: int, min_bytes: int) -> dict[str, float]:
    if not path.is_file():
        raise RuntimeError(f"screenshot missing: {path}")
    if path.stat().st_size < min_bytes:
        raise RuntimeError(f"screenshot too small: {path.stat().st_size} bytes < {min_bytes} bytes")

    actual_width, actual_height, pixels = image_verify.read_png_rgb(path)
    metrics = image_verify.analyze_pixels(pixels, actual_width)
    failures: list[str] = []
    require(
        (actual_width, actual_height) == (width, height),
        f"expected {width}x{height}, got {actual_width}x{actual_height}",
        failures,
    )
    require(metrics["unique_q"] >= 64.0, f"too few quantized colors: {metrics['unique_q']:.0f}", failures)
    require(metrics["std_luma"] >= 8.0, f"luma variation too low: {metrics['std_luma']:.2f}", failures)
    require(metrics["luma_range"] >= 40.0, f"luma range too low: {metrics['luma_range']:.2f}", failures)
    require(
        3.0 <= metrics["mean_luma"] <= 250.0,
        f"frame is effectively black/white: mean={metrics['mean_luma']:.2f}",
        failures,
    )
    require(metrics["dark_ratio"] < 0.96, f"frame is overwhelmingly dark: {metrics['dark_ratio']:.4f}", failures)
    require(
        metrics["mean_chroma"] >= 2.0,
        f"frame has no meaningful color variation: {metrics['mean_chroma']:.2f}",
        failures,
    )
    if failures:
        raise RuntimeError(
            "Zhumadian screenshot validation failed:\n  - " + "\n  - ".join(failures) + f"\nmetrics={metrics}"
        )
    return metrics


def run_demo(args: argparse.Namespace, image: Path, stats: Path) -> None:
    demo_args = [
        "--scene=industrial_park",
        "--preset=zhumadian",
        f"--zhumadian-view={args.view}",
        f"--zhumadian-detail={args.detail}",
        f"--frames={args.frames}",
        f"--screenshot={image}",
        f"--stats={stats}",
        f"--width={args.width}",
        f"--height={args.height}",
    ]
    for output in (image, stats):
        try:
            output.unlink()
        except FileNotFoundError:
            pass

    cwd = Path(args.cwd).resolve()
    demo = Path(args.demo)
    if not demo.is_absolute():
        demo = cwd / demo
    run_showcase_process(
        demo,
        demo_args,
        cwd,
        attempts=args.demo_attempts,
        timeout=args.demo_timeout,
        fallback_root=cwd,
        copy_fallback=True,
        label=f"showcase_demo zhumadian/{args.view}",
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", required=True)
    parser.add_argument("--view", choices=("overview", "oblique", "close"), default="overview")
    parser.add_argument("--detail", choices=("coarse", "child1"), default="coarse")
    parser.add_argument("--output", required=True)
    parser.add_argument("--stats-output", required=True)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--frames", type=int, default=30)
    parser.add_argument("--cwd", default=os.getcwd())
    parser.add_argument("--runtime-root")
    parser.add_argument("--source-root")
    parser.add_argument("--min-bytes", type=int, default=50_000)
    parser.add_argument("--demo-timeout", type=float, default=45.0)
    parser.add_argument("--demo-attempts", type=int, default=2)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    suppress_windows_error_dialogs()
    cwd = Path(args.cwd).resolve()
    image = Path(args.output).resolve()
    stats_path = Path(args.stats_output).resolve()
    runtime_root = (
        Path(args.runtime_root).resolve()
        if args.runtime_root
        else cwd / "assets" / "showcase" / "runtime" / "zhumadian"
    )
    source_root = Path(args.source_root).resolve() if args.source_root else cwd / "assets" / "驻马店园区"
    if args.detail == "child1" and not args.runtime_root:
        runtime_root /= "child1"
    image.parent.mkdir(parents=True, exist_ok=True)
    stats_path.parent.mkdir(parents=True, exist_ok=True)

    try:
        runtime = audit_runtime_assets(runtime_root, args.detail)
        source = audit_source_roi(source_root)
        run_demo(args, image, stats_path)
        metrics = validate_image(image, args.width, args.height, args.min_bytes)
        stats = validate_stats(stats_path, args.view, args.detail)
    except Exception as exc:
        print(f"[zhumadian_stage_verify] {exc}", file=sys.stderr)
        return 1

    print(
        f"[zhumadian_stage_verify] {args.view}: ok "
        f"mean={metrics['mean_luma']:.1f} std={metrics['std_luma']:.1f} "
        f"runtime_vertices={runtime['vertices']} runtime_triangles={runtime['triangles']} "
        f"roots={len(ROOT_IDS)} source_osgb={source['files']} "
        f"far={float(stats['camera_far_clip_m']):.1f}m"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
