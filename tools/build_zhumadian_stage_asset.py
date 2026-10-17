#!/usr/bin/env python3
"""Build or audit the local Zhumadian OSGB coarse-ROI stage package.

The stage package intentionally exports only the embedded coarse geometry from
one root PagedLOD file in each of 18 selected ContextCapture tiles.  It is not a
recursive conversion of the selected OSGB hierarchy, not full-dataset
streaming, and not an independent DEM/DOM/DLG product.
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any


SCHEMA = "hongtu.zhumadian_stage_osgb_roi.v1"
ROOT_TILE_IDS = (
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
EXPECTED_SELECTED_OSGB_FILES = 531
EXPECTED_SELECTED_OSGB_BYTES = 95_582_518
EXPECTED_SRS = "EPSG:32650"
EXPECTED_SRS_ORIGIN = (225_319.5, 3_648_440.1, 30.9)
APPROX_WGS84_ORIGIN = (114.062119128308, 32.939760357813)
EXPECTED_ATLAS_SIZE = (2048, 1024)
EXPECTED_LOCAL_BOUNDS_MIN = (799.831, 499.827, 22.3346)
EXPECTED_LOCAL_BOUNDS_MAX = (1058.19, 800.055, 75.502)
RUNTIME_FILENAMES = (
    "zhumadian_roi18.gltf",
    "zhumadian_roi18.bin",
    "zhumadian_roi18_atlas.jpg",
)
ROOT_FILE_PATTERN = re.compile(r"^Tile_(?P<tile>\d+)_L(?P<lod>\d+)_(?P<path>0u.*)\.osgb$", re.IGNORECASE)


class StageAssetError(RuntimeError):
    pass


@dataclass(frozen=True)
class RootTile:
    tile_id: int
    level: int
    path: Path


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def resolve_path(root: Path, value: str) -> Path:
    path = Path(value)
    return path if path.is_absolute() else root / path


def manifest_path(root: Path, path: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return path.resolve().as_posix()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def fail(message: str) -> None:
    raise StageAssetError(message)


def read_metadata(source: Path) -> tuple[str, tuple[float, float, float]]:
    metadata = source / "metadata.xml"
    if not metadata.is_file():
        fail(f"missing ContextCapture metadata: {metadata}")
    try:
        root = ET.parse(metadata).getroot()
        srs = (root.findtext("SRS") or "").strip()
        origin_text = (root.findtext("SRSOrigin") or "").strip()
        origin = tuple(float(value.strip()) for value in origin_text.split(","))
    except (ET.ParseError, ValueError) as exc:
        fail(f"invalid ContextCapture metadata {metadata}: {exc}")
    if srs != EXPECTED_SRS:
        fail(f"unexpected SRS: expected {EXPECTED_SRS}, got {srs!r}")
    if len(origin) != 3 or any(abs(actual - expected) > 1.0e-4 for actual, expected in zip(origin, EXPECTED_SRS_ORIGIN)):
        fail(f"unexpected SRS origin: expected {EXPECTED_SRS_ORIGIN}, got {origin}")
    return srs, (origin[0], origin[1], origin[2])


def choose_root_file(tile_directory: Path, tile_id: int) -> RootTile:
    candidates: list[tuple[int, int, str, Path]] = []
    for path in tile_directory.glob("*.osgb"):
        match = ROOT_FILE_PATTERN.match(path.name)
        if match is None or int(match.group("tile")) != tile_id:
            continue
        branch = match.group("path")
        candidates.append((int(match.group("lod")), len(branch), branch.lower(), path))
    if not candidates:
        fail(f"no root OSGB candidate found for Tile_{tile_id}: {tile_directory}")
    candidates.sort(key=lambda entry: (entry[0], entry[1], entry[2]))
    level, branch_length, branch, selected = candidates[0]
    if branch != "0u" or branch_length != 2:
        fail(f"Tile_{tile_id} shallowest OSGB is not the expected 0u root: {selected.name}")
    return RootTile(tile_id=tile_id, level=level, path=selected)


def audit_source(source: Path) -> tuple[list[RootTile], int, int]:
    read_metadata(source)
    data = source / "Data"
    if not data.is_dir():
        fail(f"missing ContextCapture Data directory: {data}")

    roots: list[RootTile] = []
    file_count = 0
    byte_count = 0
    for tile_id in ROOT_TILE_IDS:
        tile_directory = data / f"Tile_{tile_id}"
        if not tile_directory.is_dir():
            fail(f"missing selected root tile directory: {tile_directory}")
        tile_files = sorted(tile_directory.rglob("*.osgb"))
        if not tile_files:
            fail(f"selected root tile has no OSGB files: {tile_directory}")
        file_count += len(tile_files)
        byte_count += sum(path.stat().st_size for path in tile_files)
        roots.append(choose_root_file(tile_directory, tile_id))

    if file_count != EXPECTED_SELECTED_OSGB_FILES:
        fail(
            "selected 18-tile hierarchy changed: "
            f"expected {EXPECTED_SELECTED_OSGB_FILES} OSGB files, got {file_count}"
        )
    if byte_count != EXPECTED_SELECTED_OSGB_BYTES:
        fail(
            "selected 18-tile hierarchy changed: "
            f"expected {EXPECTED_SELECTED_OSGB_BYTES} bytes, got {byte_count}"
        )
    return roots, file_count, byte_count


def jpeg_dimensions(path: Path) -> tuple[int, int]:
    with path.open("rb") as source:
        if source.read(2) != b"\xff\xd8":
            fail(f"not a JPEG image: {path}")
        while True:
            marker_prefix = source.read(1)
            if not marker_prefix:
                break
            if marker_prefix != b"\xff":
                continue
            marker = source.read(1)
            while marker == b"\xff":
                marker = source.read(1)
            if not marker or marker in (b"\xd8", b"\xd9"):
                continue
            length_bytes = source.read(2)
            if len(length_bytes) != 2:
                break
            segment_length = struct.unpack(">H", length_bytes)[0]
            if segment_length < 2:
                break
            marker_value = marker[0]
            if marker_value in {0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF}:
                payload = source.read(5)
                if len(payload) != 5:
                    break
                height, width = struct.unpack(">HH", payload[1:5])
                return width, height
            source.seek(segment_length - 2, os.SEEK_CUR)
    fail(f"could not read JPEG dimensions: {path}")


def sgi_dimensions(path: Path) -> tuple[int, int, int]:
    with path.open("rb") as source:
        header = source.read(12)
    if len(header) != 12 or struct.unpack(">H", header[:2])[0] != 474:
        fail(f"not an SGI RGB image: {path}")
    width, height, channels = struct.unpack(">HHH", header[6:12])
    return width, height, channels


def inspect_gltf(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"could not read glTF {path}: {exc}")
    if document.get("asset", {}).get("version") != "2.0":
        fail(f"runtime asset is not glTF 2.0: {path}")
    buffers = document.get("buffers", [])
    images = document.get("images", [])
    if [entry.get("uri") for entry in buffers] != ["zhumadian_roi18.bin"]:
        fail("Zhumadian glTF must reference the external zhumadian_roi18.bin buffer")
    if [entry.get("uri") for entry in images] != ["zhumadian_roi18_atlas.jpg"]:
        fail("Zhumadian glTF must reference the external zhumadian_roi18_atlas.jpg image")

    accessors = document.get("accessors", [])
    position_elements = 0
    index_elements = 0
    triangles = 0
    primitive_count = 0
    textured_primitives = 0
    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            primitive_count += 1
            position_index = primitive.get("attributes", {}).get("POSITION")
            index_index = primitive.get("indices")
            if not isinstance(position_index, int) or not (0 <= position_index < len(accessors)):
                fail("glTF primitive is missing a valid POSITION accessor")
            position_elements += int(accessors[position_index].get("count", 0))
            if isinstance(index_index, int) and 0 <= index_index < len(accessors):
                primitive_indices = int(accessors[index_index].get("count", 0))
                index_elements += primitive_indices
                if int(primitive.get("mode", 4)) == 4:
                    triangles += primitive_indices // 3
            material_index = primitive.get("material")
            if isinstance(material_index, int) and 0 <= material_index < len(document.get("materials", [])):
                material = document["materials"][material_index]
                if "baseColorTexture" in material.get("pbrMetallicRoughness", {}):
                    textured_primitives += 1
    if primitive_count < 1 or triangles < 1 or textured_primitives != primitive_count:
        fail(
            "runtime glTF must contain triangle geometry and every primitive must use the real atlas: "
            f"primitives={primitive_count}, triangles={triangles}, textured={textured_primitives}"
        )
    return {
        "mesh_count": len(document.get("meshes", [])),
        "primitive_count": primitive_count,
        "material_count": len(document.get("materials", [])),
        "texture_count": len(document.get("textures", [])),
        "position_elements": position_elements,
        "index_elements": index_elements,
        "triangles": triangles,
        "textured_primitives": textured_primitives,
    }


def runtime_file_records(root: Path, runtime_output: Path) -> list[dict[str, Any]]:
    roles = {
        "zhumadian_roi18.gltf": "glTF scene and material metadata",
        "zhumadian_roi18.bin": "external glTF geometry buffer",
        "zhumadian_roi18_atlas.jpg": "real OSGB photogrammetry texture atlas",
    }
    records: list[dict[str, Any]] = []
    for filename in RUNTIME_FILENAMES:
        path = runtime_output / filename
        if not path.is_file():
            fail(f"missing Zhumadian runtime artifact: {path}")
        records.append(
            {
                "path": manifest_path(root, path),
                "role": roles[filename],
                "bytes": path.stat().st_size,
                "sha256": sha256(path),
            }
        )
    return records


def obj_counts(path: Path) -> dict[str, int]:
    counts = {"positions": 0, "texture_coordinates": 0, "normals": 0, "faces": 0}
    prefixes = {b"v ": "positions", b"vt ": "texture_coordinates", b"vn ": "normals", b"f ": "faces"}
    with path.open("rb") as source:
        for line in source:
            for prefix, key in prefixes.items():
                if line.startswith(prefix):
                    counts[key] += 1
                    break
    return counts


def make_manifest(
    root: Path,
    source: Path,
    roots: list[RootTile],
    file_count: int,
    byte_count: int,
    runtime_output: Path,
    work_directory: Path | None,
) -> dict[str, Any]:
    srs, origin = read_metadata(source)
    gltf_stats = inspect_gltf(runtime_output / "zhumadian_roi18.gltf")
    atlas_size = jpeg_dimensions(runtime_output / "zhumadian_roi18_atlas.jpg")
    if atlas_size != EXPECTED_ATLAS_SIZE:
        fail(f"unexpected runtime atlas dimensions: expected {EXPECTED_ATLAS_SIZE}, got {atlas_size}")

    intermediate: dict[str, Any] | None = None
    if work_directory is not None:
        obj = work_directory / "zhumadian_roi18.obj"
        mtl = work_directory / "zhumadian_roi18.mtl"
        rgb = work_directory / "atlas_0.rgb"
        if obj.is_file() and mtl.is_file() and rgb.is_file():
            intermediate = {
                "obj": manifest_path(root, obj),
                "obj_bytes": obj.stat().st_size,
                "obj_counts": obj_counts(obj),
                "mtl": manifest_path(root, mtl),
                "mtl_bytes": mtl.stat().st_size,
                "sgi_atlas": manifest_path(root, rgb),
                "sgi_atlas_bytes": rgb.stat().st_size,
            }

    return {
        "schema": SCHEMA,
        "generated_at": datetime.now().astimezone().isoformat(timespec="seconds"),
        "generator": "tools/build_zhumadian_stage_asset.py",
        "stage": {
            "name": "Zhumadian ContextCapture OSGB coarse ROI stage",
            "name_zh_cn": "驻马店园区 OSGB 倾斜摄影/实景三维阶段切片",
            "partial_roi": True,
            "root_tile_count": len(roots),
            "root_tile_ids": [entry.tile_id for entry in roots],
            "selected_source_hierarchy_osgb_files": file_count,
            "selected_source_hierarchy_bytes": byte_count,
            "runtime_lod": "embedded coarse geometry from one root PagedLOD file per selected tile",
            "recursive_child_lod_exported": False,
            "full_dataset_streaming": False,
            "full_park_coverage": False,
        },
        "classification": {
            "source_type": "ContextCapture OSGB oblique photogrammetry/reality mesh",
            "independent_dem": False,
            "independent_dom": False,
            "dlg": False,
            "note_zh_cn": "该数据是带纹理实景三维网格，不得标记为独立 DEM、DOM 或 DLG 数据。",
        },
        "spatial_reference": {
            "srs": srs,
            "srs_origin": list(origin),
            "approx_wgs84_origin_lon_lat": list(APPROX_WGS84_ORIGIN),
            "approximation_note": "WGS84 origin is derived from EPSG:32650 metadata for traceability.",
            "runtime_local_bounds_min": list(EXPECTED_LOCAL_BOUNDS_MIN),
            "runtime_local_bounds_max": list(EXPECTED_LOCAL_BOUNDS_MAX),
        },
        "source": {
            "root": manifest_path(root, source),
            "metadata": manifest_path(root, source / "metadata.xml"),
            "data": manifest_path(root, source / "Data"),
            "root_files": [
                {
                    "tile_id": entry.tile_id,
                    "level": entry.level,
                    "path": manifest_path(root, entry.path),
                }
                for entry in roots
            ],
            "license_declaration_found": False,
            "distribution_policy": "local_non_public_demo_only",
        },
        "conversion": {
            "osg_toolchain": "OpenSceneGraph 3.6.5 osgconv",
            "osg_optimizer": "DEFAULT TEXTURE_ATLAS_BUILDER",
            "osg_output_option": "OutputTextureFiles",
            "selection_policy": "only the 18 selected root OSGB files; PagedLOD children are not recursively exported",
            "runtime_converter": "HongTu assimp_asset_converter (Assimp glTF 2.0 exporter)",
            "windows_subprocess_policy": "CREATE_NO_WINDOW + hidden STARTUPINFO + inherited fail-critical-error suppression",
            "intermediate": intermediate,
        },
        "runtime": {
            "format": "glTF 2.0 with external BIN and JPEG",
            "is_glb": False,
            "files": runtime_file_records(root, runtime_output),
            "total_bytes": sum((runtime_output / name).stat().st_size for name in RUNTIME_FILENAMES),
            "geometry": gltf_stats,
            "atlas": {
                "width": atlas_size[0],
                "height": atlas_size[1],
                "stored_mip_levels": 1,
                "expected_runtime_generated_mip_levels": 12,
            },
        },
        "limitations_zh_cn": [
            "当前阶段仅覆盖 18 个根瓦片构成的局部 ROI，不代表完整驻马店园区数据已接入。",
            "当前运行时只包含根 PagedLOD 内嵌粗层级几何，不包含所选 531 个 OSGB 子层级的递归流式加载。",
            "该源数据不是独立 DEM、DOM 或 DLG；不能据此宣称完成 DEM/DOM/DLG 能力。",
            "数据目录未发现可公开再分发的授权声明，因此阶段资源只用于本地、非公开演示。",
            "该阶段包用于真实数据接入和相机/材质检查，不代表已经达到 UE5 最终视觉质量。",
        ],
    }


def validate_manifest(
    root: Path,
    manifest_file: Path,
    source: Path,
    roots: list[RootTile],
    file_count: int,
    byte_count: int,
    runtime_output: Path,
) -> None:
    try:
        manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"could not read stage manifest {manifest_file}: {exc}")
    if manifest.get("schema") != SCHEMA:
        fail(f"unexpected manifest schema: {manifest.get('schema')!r}")
    stage = manifest.get("stage", {})
    expected_values = {
        "partial_roi": True,
        "root_tile_count": len(ROOT_TILE_IDS),
        "root_tile_ids": list(ROOT_TILE_IDS),
        "selected_source_hierarchy_osgb_files": file_count,
        "selected_source_hierarchy_bytes": byte_count,
        "recursive_child_lod_exported": False,
        "full_dataset_streaming": False,
        "full_park_coverage": False,
    }
    for key, expected in expected_values.items():
        if stage.get(key) != expected:
            fail(f"manifest stage.{key} expected {expected!r}, got {stage.get(key)!r}")
    classification = manifest.get("classification", {})
    for key in ("independent_dem", "independent_dom", "dlg"):
        if classification.get(key) is not False:
            fail(f"manifest classification.{key} must be false for this OSGB reality-mesh package")
    if manifest.get("spatial_reference", {}).get("srs") != EXPECTED_SRS:
        fail(f"manifest must retain {EXPECTED_SRS}")
    if manifest.get("runtime", {}).get("is_glb") is not False:
        fail("manifest must describe the current runtime as glTF + BIN + JPEG, not GLB")

    actual_records = {Path(record["path"]).name: record for record in runtime_file_records(root, runtime_output)}
    manifest_records = {
        Path(record.get("path", "")).name: record for record in manifest.get("runtime", {}).get("files", [])
    }
    if set(actual_records) != set(RUNTIME_FILENAMES) or set(manifest_records) != set(RUNTIME_FILENAMES):
        fail("manifest runtime file set must be exactly glTF + BIN + JPEG")
    for filename in RUNTIME_FILENAMES:
        actual = actual_records[filename]
        recorded = manifest_records[filename]
        if recorded.get("bytes") != actual["bytes"] or recorded.get("sha256", "").lower() != actual["sha256"]:
            fail(f"runtime artifact does not match manifest: {filename}")

    root_records = manifest.get("source", {}).get("root_files", [])
    recorded_roots = [(record.get("tile_id"), record.get("level")) for record in root_records]
    actual_roots = [(entry.tile_id, entry.level) for entry in roots]
    if recorded_roots != actual_roots:
        fail("manifest root-file selection no longer matches the audited source")
    inspect_gltf(runtime_output / "zhumadian_roi18.gltf")
    if jpeg_dimensions(runtime_output / "zhumadian_roi18_atlas.jpg") != EXPECTED_ATLAS_SIZE:
        fail(f"runtime atlas must be {EXPECTED_ATLAS_SIZE[0]}x{EXPECTED_ATLAS_SIZE[1]}")


def subprocess_settings() -> dict[str, Any]:
    settings: dict[str, Any] = {
        "stdout": subprocess.PIPE,
        "stderr": subprocess.STDOUT,
        "text": True,
        "errors": "replace",
    }
    if os.name == "nt":
        settings["creationflags"] = getattr(subprocess, "CREATE_NO_WINDOW", 0x08000000)
        startupinfo = subprocess.STARTUPINFO()
        startupinfo.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startupinfo.wShowWindow = 0
        settings["startupinfo"] = startupinfo
    return settings


def set_windows_error_mode() -> int | None:
    if os.name != "nt":
        return None
    kernel32 = ctypes.windll.kernel32
    sem_failcriticalerrors = 0x0001
    sem_nogpfaulterrorbox = 0x0002
    sem_noopenfileerrorbox = 0x8000
    old_mode = int(kernel32.SetErrorMode(sem_failcriticalerrors | sem_nogpfaulterrorbox | sem_noopenfileerrorbox))
    kernel32.SetErrorMode(old_mode | sem_failcriticalerrors | sem_nogpfaulterrorbox | sem_noopenfileerrorbox)
    return old_mode


def restore_windows_error_mode(old_mode: int | None) -> None:
    if os.name == "nt" and old_mode is not None:
        ctypes.windll.kernel32.SetErrorMode(old_mode)


def run_hidden(command: list[str], cwd: Path, env: dict[str, str], label: str) -> None:
    old_mode = set_windows_error_mode()
    try:
        completed = subprocess.run(command, cwd=str(cwd), env=env, **subprocess_settings())
    except OSError as exc:
        fail(f"{label} could not start: {exc}")
    finally:
        restore_windows_error_mode(old_mode)
    output = completed.stdout or ""
    if completed.returncode != 0:
        tail = "\n".join(output.splitlines()[-40:])
        fail(f"{label} failed with exit code {completed.returncode}\n{tail}")
    if output.strip():
        print(f"[{label}]\n{output.rstrip()}")


def ensure_ascii_source_alias(source: Path, alias: Path) -> Path:
    if os.name != "nt" or all(ord(character) < 128 for character in str(source)):
        return source
    # A moved workspace can leave a broken derived junction. Remove only the
    # junction itself, never its old target or a normal directory.
    if alias.is_junction() and not alias.exists():
        if alias.name != "zmd_source" or not alias.parent.resolve().is_relative_to((repo_root() / "work").resolve()):
            fail(f"refusing to replace an alias outside the derived work directory: {alias}")
        os.rmdir(alias)
    if alias.exists():
        try:
            if os.path.samefile(source, alias):
                return alias
        except OSError:
            pass
        fail(f"source alias exists but points elsewhere: {alias}")
    alias.parent.mkdir(parents=True, exist_ok=True)
    old_mode = set_windows_error_mode()
    try:
        completed = subprocess.run(
            ["cmd.exe", "/d", "/c", "mklink", "/J", str(alias), str(source)],
            cwd=str(alias.parent),
            **subprocess_settings(),
        )
    except OSError as exc:
        fail(f"could not create ASCII source junction {alias}: {exc}")
    finally:
        restore_windows_error_mode(old_mode)
    if completed.returncode != 0 or not alias.exists():
        fail(f"could not create ASCII source junction {alias}: {(completed.stdout or '').strip()}")
    return alias


def require_toolchain(osgconv: Path, plugin_directory: Path, assimp_converter: Path, assimp_runtime: Path) -> None:
    for path, label in ((osgconv, "osgconv"), (assimp_converter, "assimp_asset_converter")):
        if not path.is_file():
            fail(f"missing {label}: {path}")
    required_osg_plugins = (
        "osgdb_osg.dll",
        "osgdb_serializers_osg.dll",
        "osgdb_obj.dll",
        "osgdb_rgb.dll",
        "osgdb_jpeg.dll",
    )
    for filename in required_osg_plugins:
        if os.name == "nt" and not (plugin_directory / filename).is_file():
            fail(f"missing OSG plugin {filename}: {plugin_directory}")
    required_osg_runtime = (
        "OpenThreads.dll",
        "osg.dll",
        "osgDB.dll",
        "osgUtil.dll",
        "zlib1.dll",
        "jpeg62.dll",
        "turbojpeg.dll",
        "libexpat.dll",
    )
    for filename in required_osg_runtime:
        if os.name == "nt" and not (osgconv.parent / filename).is_file():
            fail(f"missing OSG runtime DLL {filename}; conversion is blocked before launch: {osgconv.parent}")
    required_assimp_runtime = (
        "assimp-vc143-mt.dll",
        "draco.dll",
        "fmt.dll",
        "kubazip.dll",
        "minizip.dll",
        "poly2tri.dll",
        "pugixml.dll",
        "zlib1.dll",
    )
    for filename in required_assimp_runtime:
        if os.name == "nt" and not (assimp_runtime / filename).is_file():
            fail(f"missing Assimp runtime DLL {filename}: {assimp_runtime}")


def remove_known_outputs(work_directory: Path) -> None:
    names = (
        "zhumadian_roi18.obj",
        "zhumadian_roi18.mtl",
        "atlas_0.rgb",
        "zhumadian_roi18_atlas.jpg",
        "zhumadian_roi18.gltf",
        "zhumadian_roi18.bin",
    )
    for name in names:
        path = work_directory / name
        if path.is_dir():
            fail(f"refusing to replace directory at generated-file path: {path}")
        if path.exists():
            path.unlink()


def build_package(
    root: Path,
    source: Path,
    roots: list[RootTile],
    file_count: int,
    byte_count: int,
    source_alias: Path,
    work_directory: Path,
    runtime_output: Path,
    manifest_file: Path,
    osgconv: Path,
    plugin_directory: Path,
    assimp_converter: Path,
    assimp_runtime: Path,
) -> None:
    require_toolchain(osgconv, plugin_directory, assimp_converter, assimp_runtime)
    tool_source = ensure_ascii_source_alias(source, source_alias)
    work_directory.mkdir(parents=True, exist_ok=True)
    runtime_output.mkdir(parents=True, exist_ok=True)
    manifest_file.parent.mkdir(parents=True, exist_ok=True)
    remove_known_outputs(work_directory)

    mapped_roots = [tool_source / entry.path.relative_to(source) for entry in roots]
    for path in mapped_roots:
        if not path.is_file():
            fail(f"OSGB root is unavailable through tool source path: {path}")

    obj = work_directory / "zhumadian_roi18.obj"
    mtl = work_directory / "zhumadian_roi18.mtl"
    rgb = work_directory / "atlas_0.rgb"
    jpg = work_directory / "zhumadian_roi18_atlas.jpg"
    gltf = work_directory / "zhumadian_roi18.gltf"
    binary = work_directory / "zhumadian_roi18.bin"

    osg_env = os.environ.copy()
    osg_env["PATH"] = os.pathsep.join((str(osgconv.parent), osg_env.get("PATH", "")))
    previous_library_path = osg_env.get("OSG_LIBRARY_PATH", "")
    osg_env["OSG_LIBRARY_PATH"] = os.pathsep.join(
        value for value in (str(plugin_directory), previous_library_path) if value
    )
    osg_env["OSG_OPTIMIZER"] = "DEFAULT TEXTURE_ATLAS_BUILDER"
    osg_env["OSG_NOTIFY_LEVEL"] = "WARN"
    run_hidden(
        [str(osgconv), "-O", "OutputTextureFiles", *(str(path) for path in mapped_roots), str(obj)],
        work_directory,
        osg_env,
        "osgconv root-ROI export",
    )
    if not obj.is_file() or not mtl.is_file() or not rgb.is_file():
        fail("osgconv did not produce the expected OBJ, MTL, and atlas_0.rgb outputs")
    extra_atlases = sorted(work_directory.glob("atlas_*.rgb"))
    if extra_atlases != [rgb]:
        fail(f"stage contract requires exactly one texture atlas, got {[path.name for path in extra_atlases]}")
    if sgi_dimensions(rgb) != (*EXPECTED_ATLAS_SIZE, 3):
        fail(f"unexpected SGI atlas dimensions/channels: {sgi_dimensions(rgb)}")

    image_env = osg_env.copy()
    image_env["OSG_OPTIMIZER"] = "OFF"
    run_hidden([str(osgconv), str(rgb), str(jpg)], work_directory, image_env, "osgconv atlas JPEG export")
    if jpeg_dimensions(jpg) != EXPECTED_ATLAS_SIZE:
        fail(f"JPEG atlas must be {EXPECTED_ATLAS_SIZE[0]}x{EXPECTED_ATLAS_SIZE[1]}")

    material_text = mtl.read_text(encoding="utf-8", errors="replace")
    if "atlas_0.rgb" not in material_text:
        fail(f"OBJ material does not reference atlas_0.rgb: {mtl}")
    mtl.write_text(material_text.replace("atlas_0.rgb", jpg.name), encoding="utf-8", newline="\n")

    assimp_env = os.environ.copy()
    assimp_env["PATH"] = os.pathsep.join(
        (str(assimp_runtime), str(assimp_converter.parent), assimp_env.get("PATH", ""))
    )
    run_hidden(
        [str(assimp_converter), f"--input={obj}", f"--output={gltf}"],
        work_directory,
        assimp_env,
        "assimp glTF export",
    )
    for path in (gltf, binary, jpg):
        if not path.is_file():
            fail(f"conversion did not produce expected runtime file: {path}")
    inspect_gltf(gltf)

    for path in (gltf, binary, jpg):
        shutil.copy2(path, runtime_output / path.name)
    manifest = make_manifest(root, source, roots, file_count, byte_count, runtime_output, work_directory)
    manifest_file.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    validate_manifest(root, manifest_file, source, roots, file_count, byte_count, runtime_output)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Audit or rebuild the 18-root-tile Zhumadian OSGB coarse-ROI runtime package. "
            "The default action is a non-mutating contract check."
        ),
        epilog=(
            "Examples:\n"
            "  python tools/build_zhumadian_stage_asset.py --check\n"
            "  python tools/build_zhumadian_stage_asset.py --build\n\n"
            "--build converts only the 18 root PagedLOD files. It does not recursively convert all 531 OSGB files."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--check", action="store_true", help="Audit source, runtime files, hashes, and manifest (default).")
    action.add_argument("--build", action="store_true", help="Rebuild OBJ/atlas/glTF and refresh the manifest.")
    parser.add_argument("--source", default="assets/驻马店园区", help="ContextCapture source directory.")
    parser.add_argument("--source-alias", default="work/zmd_source", help="ASCII junction used by legacy OSG on Windows.")
    parser.add_argument("--work-dir", default="work/zmd_stage_convert", help="Intermediate conversion directory.")
    parser.add_argument(
        "--runtime-output",
        default="assets/showcase/runtime/zhumadian",
        help="Runtime glTF + BIN + JPEG output directory.",
    )
    parser.add_argument(
        "--manifest",
        default="data/showcase/zhumadian_stage_manifest.json",
        help="Stage provenance and hash manifest.",
    )
    parser.add_argument("--osgconv", default="work/osg-build-min/bin/osgconv.exe", help="OSG 3.6.5 osgconv executable.")
    parser.add_argument(
        "--osg-plugin-dir",
        default="work/osg-build-min/bin/osgPlugins-3.6.5",
        help="OSG plugin directory.",
    )
    parser.add_argument(
        "--assimp-converter",
        default="build_assimp_converter/Release/assimp_asset_converter.exe",
        help="HongTu Assimp conversion executable.",
    )
    parser.add_argument(
        "--assimp-runtime-dir",
        default="build_v7_unit_tests/vcpkg_installed/x64-windows-noexe/bin",
        help="Directory containing the Assimp converter runtime DLLs.",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    root = repo_root()
    args = parse_args(argv)
    source = resolve_path(root, args.source)
    source_alias = resolve_path(root, args.source_alias)
    work_directory = resolve_path(root, args.work_dir)
    runtime_output = resolve_path(root, args.runtime_output)
    manifest_file = resolve_path(root, args.manifest)
    osgconv = resolve_path(root, args.osgconv)
    plugin_directory = resolve_path(root, args.osg_plugin_dir)
    assimp_converter = resolve_path(root, args.assimp_converter)
    assimp_runtime = resolve_path(root, args.assimp_runtime_dir)

    try:
        roots, file_count, byte_count = audit_source(source)
        if args.build:
            build_package(
                root,
                source,
                roots,
                file_count,
                byte_count,
                source_alias,
                work_directory,
                runtime_output,
                manifest_file,
                osgconv,
                plugin_directory,
                assimp_converter,
                assimp_runtime,
            )
            print("Zhumadian stage package rebuild PASS")
        else:
            validate_manifest(root, manifest_file, source, roots, file_count, byte_count, runtime_output)
            print("Zhumadian stage package check PASS")
        print(
            f"source contract: {len(roots)} roots, {file_count} selected-hierarchy OSGB files, "
            f"{byte_count} bytes, {EXPECTED_SRS}"
        )
        print("runtime contract: glTF 2.0 + external BIN + JPEG; coarse root-geometry ROI only")
        print("classification: ContextCapture reality mesh; not independent DEM/DOM/DLG; not full-dataset streaming")
        return 0
    except StageAssetError as exc:
        print(f"Zhumadian stage package ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
