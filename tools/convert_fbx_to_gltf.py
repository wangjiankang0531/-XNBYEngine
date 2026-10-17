#!/usr/bin/env python3
"""Extract simple static meshes from binary FBX files and write glTF 2.0.

This is intentionally narrow. It handles the static binary FBX assets currently
used by the showcase demo: vertices, polygon indices, material connections, and
local translations/scales. It does not try to preserve animation, skinning,
textures, rotations, or 3ds Max-only data.
"""

from __future__ import annotations

import argparse
import array
import json
import math
import struct
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


FBX_MAGIC = b"Kaydara FBX Binary  \x00\x1a\x00"
FLOAT = 5126
ARRAY_BUFFER = 34962


@dataclass
class FbxNode:
    name: str
    props: list[Any]
    children: list["FbxNode"] = field(default_factory=list)


@dataclass
class MaterialInfo:
    source_id: int | None
    name: str
    color: tuple[float, float, float, float]


@dataclass(frozen=True)
class ConversionJob:
    input_path: Path
    output_path: Path
    max_output_triangles: int | None = None


class BinaryFbxParser:
    def __init__(self, path: Path):
        self.path = path
        self.data = path.read_bytes()
        if not self.data.startswith(FBX_MAGIC):
            raise ValueError(f"{path} is not a binary FBX")
        self.version = struct.unpack_from("<I", self.data, 23)[0]
        self.wide_headers = self.version >= 7500
        self.null_record_size = 25 if self.wide_headers else 13

    def parse(self) -> list[FbxNode]:
        nodes, _ = self._read_nodes(27, len(self.data))
        return nodes

    def _read_header(self, offset: int) -> tuple[int, int, str, int]:
        if self.wide_headers:
            end_offset, prop_count, _ = struct.unpack_from("<QQQ", self.data, offset)
            offset += 24
        else:
            end_offset, prop_count, _ = struct.unpack_from("<III", self.data, offset)
            offset += 12
        name_len = self.data[offset]
        offset += 1
        name = self.data[offset : offset + name_len].decode("utf-8", "replace")
        offset += name_len
        return int(end_offset), int(prop_count), name, offset

    def _read_property(self, offset: int) -> tuple[Any, int]:
        prop_type = chr(self.data[offset])
        offset += 1
        if prop_type == "Y":
            return struct.unpack_from("<h", self.data, offset)[0], offset + 2
        if prop_type == "C":
            return self.data[offset] != 0, offset + 1
        if prop_type == "I":
            return struct.unpack_from("<i", self.data, offset)[0], offset + 4
        if prop_type == "F":
            return struct.unpack_from("<f", self.data, offset)[0], offset + 4
        if prop_type == "D":
            return struct.unpack_from("<d", self.data, offset)[0], offset + 8
        if prop_type == "L":
            return struct.unpack_from("<q", self.data, offset)[0], offset + 8
        if prop_type in "fdiIlb":
            length, encoding, byte_count = struct.unpack_from("<III", self.data, offset)
            offset += 12
            payload = self.data[offset : offset + byte_count]
            offset += byte_count
            if encoding == 1:
                payload = zlib.decompress(payload)
            elif encoding != 0:
                raise ValueError(f"unsupported FBX array encoding {encoding}")
            return _read_array_payload(prop_type, payload, length), offset
        if prop_type in "SR":
            length = struct.unpack_from("<I", self.data, offset)[0]
            offset += 4
            payload = self.data[offset : offset + length]
            offset += length
            if prop_type == "S":
                return payload.decode("utf-8", "replace"), offset
            return bytes(payload), offset
        raise ValueError(f"unknown FBX property type {prop_type!r} at {offset - 1}")

    def _read_nodes(self, offset: int, limit: int) -> tuple[list[FbxNode], int]:
        nodes: list[FbxNode] = []
        while offset < limit:
            end = offset + self.null_record_size
            if end <= len(self.data) and all(b == 0 for b in self.data[offset:end]):
                return nodes, end
            if offset + 13 > len(self.data):
                return nodes, offset

            start = offset
            end_offset, prop_count, name, offset = self._read_header(offset)
            if end_offset == 0 or end_offset > len(self.data) or end_offset <= start:
                return nodes, offset

            props: list[Any] = []
            for _ in range(prop_count):
                prop, offset = self._read_property(offset)
                props.append(prop)

            node = FbxNode(name=name, props=props)
            child_limit = end_offset - self.null_record_size
            if offset < child_limit:
                node.children, offset = self._read_nodes(offset, child_limit)
            if offset < end_offset:
                offset = end_offset
            nodes.append(node)
        return nodes, offset


def _read_array_payload(prop_type: str, payload: bytes, length: int) -> list[Any]:
    typecode = {
        "f": "f",
        "d": "d",
        "i": "i",
        "I": "i",
        "l": "q",
        "L": "q",
        "b": "b",
    }[prop_type]
    values = array.array(typecode)
    byte_length = length * values.itemsize
    values.frombytes(payload[:byte_length])
    if sys.byteorder != "little" and values.itemsize > 1:
        values.byteswap()
    return list(values)


def iter_nodes(nodes: list[FbxNode], name: str | None = None):
    for node in nodes:
        if name is None or node.name == name:
            yield node
        yield from iter_nodes(node.children, name)


def child(node: FbxNode, name: str) -> FbxNode | None:
    for candidate in node.children:
        if candidate.name == name:
            return candidate
    return None


def clean_fbx_name(value: Any) -> str:
    text = str(value)
    if "\x00\x01" in text:
        text = text.split("\x00\x01", 1)[0]
    if "::" in text:
        text = text.split("::", 1)[-1]
    return text.strip()


def prop_vec3(node: FbxNode, prop_name: str, fallback: tuple[float, float, float]) -> tuple[float, float, float]:
    props70 = child(node, "Properties70")
    if not props70:
        return fallback
    for prop in props70.children:
        if prop.name == "P" and len(prop.props) >= 7 and prop.props[0] == prop_name:
            return float(prop.props[4]), float(prop.props[5]), float(prop.props[6])
    return fallback


def material_color(path: Path, material: FbxNode, ordinal: int) -> tuple[float, float, float, float]:
    raw = prop_vec3(material, "DiffuseColor", prop_vec3(material, "Diffuse", (0.56, 0.58, 0.58)))
    stem = path.stem
    name = clean_fbx_name(material.props[1] if len(material.props) > 1 else "")
    is_gray = max(raw) - min(raw) < 0.045

    if "\u5c4f\u5e55" in stem and "\u67b6" not in stem:
        if "\u5b89\u5168" in stem:
            raw = (0.080, 0.560, 0.300)
        elif "\u516c\u544a" in stem:
            raw = (0.085, 0.360, 0.720)
        else:
            raw = (0.060, 0.460, 0.620)
    elif "\u5927\u5c4f\u67b6" in stem:
        raw = (0.16, 0.175, 0.17) if is_gray else raw
    elif "\u68c0\u6d4b\u8bbe\u5907" in stem:
        palette = (
            (0.50, 0.55, 0.56),
            (0.09, 0.105, 0.11),
            (0.24, 0.36, 0.43),
            (0.68, 0.45, 0.18),
            (0.18, 0.21, 0.24),
        )
        raw = palette[ordinal % len(palette)] if is_gray or "Default" in name else raw

    color = tuple(max(0.0, min(1.0, float(c))) for c in raw)
    return color[0], color[1], color[2], 1.0


def node_id(node: FbxNode) -> int | None:
    if not node.props:
        return None
    value = node.props[0]
    return int(value) if isinstance(value, int) else None


def build_connection_maps(nodes: list[FbxNode]):
    models: dict[int, FbxNode] = {}
    geometries: dict[int, FbxNode] = {}
    materials: dict[int, FbxNode] = {}
    for model in iter_nodes(nodes, "Model"):
        nid = node_id(model)
        if nid is not None:
            models[nid] = model
    for geom in iter_nodes(nodes, "Geometry"):
        nid = node_id(geom)
        if nid is not None:
            geometries[nid] = geom
    for material in iter_nodes(nodes, "Material"):
        nid = node_id(material)
        if nid is not None:
            materials[nid] = material

    geom_to_model: dict[int, int] = {}
    model_to_parent: dict[int, int] = {}
    model_to_materials: dict[int, list[int]] = {}
    for conn in iter_nodes(nodes, "C"):
        if len(conn.props) < 3 or conn.props[0] != "OO":
            continue
        src = int(conn.props[1])
        dst = int(conn.props[2])
        if src in geometries and dst in models:
            geom_to_model[src] = dst
        elif src in models and dst in models:
            model_to_parent[src] = dst
        elif src in materials and dst in models:
            model_to_materials.setdefault(dst, []).append(src)
    return models, geometries, materials, geom_to_model, model_to_parent, model_to_materials


def transform_chain(model_id: int | None, models: dict[int, FbxNode], parents: dict[int, int]) -> list[FbxNode]:
    if model_id is None:
        return []
    chain_ids: list[int] = []
    seen: set[int] = set()
    while model_id in models and model_id not in seen:
        seen.add(model_id)
        chain_ids.append(model_id)
        model_id = parents.get(model_id)
    chain_ids.reverse()
    return [models[mid] for mid in chain_ids]


def apply_translation_scale(point: tuple[float, float, float], chain: list[FbxNode]) -> tuple[float, float, float]:
    x, y, z = point
    for model in chain:
        sx, sy, sz = prop_vec3(model, "Lcl Scaling", (1.0, 1.0, 1.0))
        tx, ty, tz = prop_vec3(model, "Lcl Translation", (0.0, 0.0, 0.0))
        x = x * sx + tx
        y = y * sy + ty
        z = z * sz + tz
    return x, y, z


def polygon_material_indices(geometry: FbxNode, face_count: int) -> list[int | None]:
    layer = child(geometry, "LayerElementMaterial")
    if not layer:
        return [None] * face_count

    mapping = "AllSame"
    values: list[int] = []
    for item in layer.children:
        if item.name == "MappingInformationType" and item.props:
            mapping = str(item.props[0])
        elif item.name == "Materials" and item.props and isinstance(item.props[0], list):
            values = [int(v) for v in item.props[0]]

    if not values:
        return [None] * face_count
    if mapping == "ByPolygon":
        out = [values[min(i, len(values) - 1)] for i in range(face_count)]
        return out
    return [values[0]] * face_count


def fbx_polygons(indices: list[int]) -> list[list[int]]:
    polygons: list[list[int]] = []
    current: list[int] = []
    for raw in indices:
        idx = int(raw)
        if idx < 0:
            current.append(-idx - 1)
            if len(current) >= 3:
                polygons.append(current)
            current = []
        else:
            current.append(idx)
    return polygons


def keep_triangle(triangle_index: int, source_triangle_count: int, max_output_triangles: int | None) -> bool:
    if max_output_triangles is None or max_output_triangles <= 0 or source_triangle_count <= max_output_triangles:
        return True
    before = (triangle_index * max_output_triangles) // source_triangle_count
    after = ((triangle_index + 1) * max_output_triangles) // source_triangle_count
    return after > before


def extract_triangles(path: Path, unit_scale: float, max_output_triangles: int | None = None):
    nodes = BinaryFbxParser(path).parse()
    models, geometries, materials, geom_to_model, parents, model_to_materials = build_connection_maps(nodes)

    fallback = MaterialInfo(None, "fbx_default", (0.42, 0.46, 0.46, 1.0))
    output_materials: list[MaterialInfo] = [fallback]
    material_to_output: dict[int, int] = {}
    for ordinal, (mid, material) in enumerate(materials.items()):
        mat = MaterialInfo(
            source_id=mid,
            name=clean_fbx_name(material.props[1] if len(material.props) > 1 else f"mat_{ordinal}"),
            color=material_color(path, material, ordinal),
        )
        material_to_output[mid] = len(output_materials)
        output_materials.append(mat)

    geom_count = 0
    face_count = 0
    min_x = math.inf
    min_y = math.inf
    min_z = math.inf
    max_x = -math.inf
    max_y = -math.inf
    max_z = -math.inf

    def iter_triangles():
        nonlocal min_x, min_y, min_z, max_x, max_y, max_z
        for gid, geometry in geometries.items():
            vertices_node = child(geometry, "Vertices")
            indices_node = child(geometry, "PolygonVertexIndex")
            if not vertices_node or not indices_node:
                continue
            if not vertices_node.props or not indices_node.props:
                continue
            vertices = vertices_node.props[0]
            poly_indices = indices_node.props[0]
            if not isinstance(vertices, list) or not isinstance(poly_indices, list):
                continue

            points = list(zip(vertices[0::3], vertices[1::3], vertices[2::3]))
            model_id = geom_to_model.get(gid)
            chain = transform_chain(model_id, models, parents)
            transformed = [apply_translation_scale((float(x), float(y), float(z)), chain) for x, y, z in points]

            polygons = fbx_polygons(poly_indices)
            mats_by_face = polygon_material_indices(geometry, len(polygons))
            local_mats = model_to_materials.get(model_id or -1, [])

            for face_index, polygon in enumerate(polygons):
                local_mat = mats_by_face[face_index] if face_index < len(mats_by_face) else None
                source_mat = None
                if local_mat is not None and 0 <= local_mat < len(local_mats):
                    source_mat = local_mats[local_mat]
                out_mat = material_to_output.get(source_mat, 0)
                for i in range(1, len(polygon) - 1):
                    a, b, c = polygon[0], polygon[i], polygon[i + 1]
                    if a >= len(transformed) or b >= len(transformed) or c >= len(transformed):
                        continue
                    tri = (transformed[a], transformed[b], transformed[c])
                    for x, y, z in tri:
                        min_x = min(min_x, x)
                        min_y = min(min_y, y)
                        min_z = min(min_z, z)
                        max_x = max(max_x, x)
                        max_y = max(max_y, y)
                        max_z = max(max_z, z)
                    yield out_mat, tri

    for gid, geometry in geometries.items():
        vertices_node = child(geometry, "Vertices")
        indices_node = child(geometry, "PolygonVertexIndex")
        if not vertices_node or not indices_node:
            continue
        if not vertices_node.props or not indices_node.props:
            continue
        vertices = vertices_node.props[0]
        poly_indices = indices_node.props[0]
        if not isinstance(vertices, list) or not isinstance(poly_indices, list):
            continue

        points = list(zip(vertices[0::3], vertices[1::3], vertices[2::3]))
        model_id = geom_to_model.get(gid)
        chain = transform_chain(model_id, models, parents)
        transformed = [apply_translation_scale((float(x), float(y), float(z)), chain) for x, y, z in points]

        polygons = fbx_polygons(poly_indices)
        for polygon in polygons:
            face_count += max(0, len(polygon) - 2)
        geom_count += 1

    if face_count <= 0:
        raise ValueError(f"no mesh triangles extracted from {path}")

    groups: dict[int, list[tuple[float, float, float]]] = {}
    output_face_count = 0
    for triangle_index, (out_mat, tri) in enumerate(iter_triangles()):
        if not keep_triangle(triangle_index, face_count, max_output_triangles):
            continue
        groups.setdefault(out_mat, []).extend(tri)
        output_face_count += 1

    if not groups:
        raise ValueError(f"triangle cap removed all mesh triangles from {path}")

    center_x = (min_x + max_x) * 0.5
    center_y = (min_y + max_y) * 0.5

    normalized: dict[int, list[tuple[float, float, float]]] = {}
    for mat_id, points in groups.items():
        normalized[mat_id] = [
            ((x - center_x) * unit_scale, (y - center_y) * unit_scale, (z - min_z) * unit_scale)
            for x, y, z in points
        ]

    bounds = {
        "min": [(min_x - center_x) * unit_scale, (min_y - center_y) * unit_scale, 0.0],
        "max": [(max_x - center_x) * unit_scale, (max_y - center_y) * unit_scale, (max_z - min_z) * unit_scale],
    }
    return normalized, output_materials, geom_count, face_count, output_face_count, bounds


def align4(data: bytearray) -> None:
    while len(data) % 4:
        data.append(0)


def write_gltf(
    output_path: Path,
    groups: dict[int, list[tuple[float, float, float]]],
    materials: list[MaterialInfo],
    source_path: Path,
    metadata: dict[str, Any],
) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    bin_path = output_path.with_suffix(".bin")

    binary = bytearray()
    accessors: list[dict[str, Any]] = []
    buffer_views: list[dict[str, Any]] = []
    primitives: list[dict[str, Any]] = []

    for mat_id, points in sorted(groups.items()):
        if not points:
            continue
        align4(binary)
        byte_offset = len(binary)
        for x, y, z in points:
            binary.extend(struct.pack("<fff", float(x), float(y), float(z)))
        byte_length = len(points) * 12
        buffer_view = len(buffer_views)
        buffer_views.append(
            {
                "buffer": 0,
                "byteOffset": byte_offset,
                "byteLength": byte_length,
                "target": ARRAY_BUFFER,
            }
        )
        accessor = len(accessors)
        mins = [min(p[i] for p in points) for i in range(3)]
        maxs = [max(p[i] for p in points) for i in range(3)]
        accessors.append(
            {
                "bufferView": buffer_view,
                "byteOffset": 0,
                "componentType": FLOAT,
                "count": len(points),
                "type": "VEC3",
                "min": mins,
                "max": maxs,
            }
        )
        primitives.append(
            {
                "attributes": {"POSITION": accessor},
                "material": min(mat_id, len(materials) - 1),
                "mode": 4,
            }
        )

    gltf = {
        "asset": {"version": "2.0", "generator": "htengine tools/convert_fbx_to_gltf.py"},
        "extras": {
            "source": portable_path(source_path),
            "conversion": metadata,
        },
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": output_path.stem, "mesh": 0}],
        "meshes": [{"name": output_path.stem, "primitives": primitives}],
        "materials": [
            {
                "name": mat.name,
                "pbrMetallicRoughness": {
                    "baseColorFactor": list(mat.color),
                    "metallicFactor": 0.0,
                    "roughnessFactor": 0.62,
                },
            }
            for mat in materials
        ],
        "buffers": [{"uri": bin_path.name, "byteLength": len(binary)}],
        "bufferViews": buffer_views,
        "accessors": accessors,
    }
    output_path.write_text(json.dumps(gltf, indent=2), encoding="utf-8")
    bin_path.write_bytes(binary)


def convert_one(input_path: Path, output_path: Path, unit_scale: float, max_output_triangles: int | None = None) -> dict[str, Any]:
    groups, materials, geom_count, face_count, output_face_count, bounds = extract_triangles(
        input_path, unit_scale, max_output_triangles
    )
    metadata = {
        "unit_scale": unit_scale,
        "geometry_count": geom_count,
        "triangle_count": face_count,
        "source_triangle_count": face_count,
        "output_triangle_count": output_face_count,
        "max_output_triangles": max_output_triangles,
        "triangle_lod": "even_triangle_sampling" if output_face_count < face_count else "full",
        "primitive_count": sum(1 for points in groups.values() if points),
        "bounds_m": bounds,
    }
    write_gltf(output_path, groups, materials, input_path, metadata)
    metadata["input"] = portable_path(input_path)
    metadata["output"] = portable_path(output_path)
    metadata["bin"] = portable_path(output_path.with_suffix(".bin"))
    return metadata


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def portable_path(path: Path) -> str:
    resolved = path.resolve()
    try:
        return resolved.relative_to(project_root().resolve()).as_posix()
    except ValueError:
        return path.as_posix()


def batch_showcase_small(unit_scale: float) -> list[ConversionJob]:
    root = project_root()
    source = root / "assets" / "\u6a21\u578b"
    target = root / "assets" / "showcase" / "runtime"
    return [
        ConversionJob(source / "\u5927\u5c4f\u67b6.fbx", target / "xineng_big_screen_frame.gltf"),
        ConversionJob(source / "\u5c4f\u5e55\u4e8c\u4eba\u4eba\u8bb2\u5b89\u5168.fbx", target / "xineng_safety_screen.gltf"),
        ConversionJob(source / "\u5c4f\u5e55\u8f6e\u64ad.fbx", target / "xineng_rotating_screen.gltf"),
        ConversionJob(source / "\u5c4f\u5e55\u4e00\u6700\u65b0\u516c\u544a.fbx", target / "xineng_notice_screen.gltf"),
        ConversionJob(
            source / "\u4e59-201\u53f7\u5ba4\u5185\u68c0\u6d4b\u8bbe\u5907.fbx",
            target / "xineng_monitor_equipment.gltf",
        ),
    ]


def batch_showcase_xineng(unit_scale: float) -> list[ConversionJob]:
    root = project_root()
    source = root / "assets" / "\u6a21\u578b"
    target = root / "assets" / "showcase" / "runtime"
    jobs = batch_showcase_small(unit_scale)
    jobs.append(ConversionJob(source / "peilou.fbx", target / "xineng_peilou.gltf"))
    return jobs


def batch_showcase_xineng_lod(unit_scale: float) -> list[ConversionJob]:
    root = project_root()
    source = root / "assets" / "\u6a21\u578b"
    target = root / "assets" / "showcase" / "runtime"
    jobs = batch_showcase_xineng(unit_scale)
    jobs.append(ConversionJob(source / "\u4ed3\u5e93\u8d27\u67b6.fbx", target / "xineng_warehouse_rack_lod.gltf", 60000))
    return jobs


def batch_showcase_huilian(unit_scale: float) -> list[ConversionJob]:
    root = project_root()
    source = root / "assets" / "\u60e0\u70bc" / "HERO\u4e8c\u671f" / "F-\u7535-\u7eff\u9752-7___\u672a\u5b8c\u6210"
    target = root / "assets" / "showcase" / "runtime"
    return [
        ConversionJob(
            source / "F-\u7535-\u7eff\u9752-71.FBX",
            target / "huilian_valve_catalog_lod.gltf",
            80000,
        )
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, help="single FBX file to convert")
    parser.add_argument("--output", type=Path, help="output .gltf path for --input")
    parser.add_argument(
        "--batch",
        choices=("showcase-small", "showcase-xineng", "showcase-xineng-lod", "showcase-huilian"),
        help="convert a curated showcase FBX subset",
    )
    parser.add_argument("--unit-scale", type=float, default=0.01, help="scale FBX units to glTF units")
    parser.add_argument(
        "--max-output-triangles",
        type=int,
        help="cap emitted triangles for a single --input/--output conversion",
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=project_root() / "data" / "showcase" / "converted_model_assets.json",
        help="where batch conversion metadata is written",
    )
    args = parser.parse_args()

    jobs: list[ConversionJob]
    if args.batch == "showcase-small":
        jobs = batch_showcase_small(args.unit_scale)
    elif args.batch == "showcase-xineng":
        jobs = batch_showcase_xineng(args.unit_scale)
    elif args.batch == "showcase-xineng-lod":
        jobs = batch_showcase_xineng_lod(args.unit_scale)
    elif args.batch == "showcase-huilian":
        jobs = batch_showcase_huilian(args.unit_scale)
    elif args.input and args.output:
        jobs = [ConversionJob(args.input, args.output, args.max_output_triangles)]
    else:
        parser.error(
            "use --batch showcase-small, --batch showcase-xineng, --batch showcase-xineng-lod, "
            "--batch showcase-huilian, or --input/--output"
        )

    results = []
    for job in jobs:
        input_path = job.input_path
        output_path = job.output_path
        if not input_path.exists():
            raise FileNotFoundError(input_path)
        result = convert_one(input_path, output_path, args.unit_scale, job.max_output_triangles)
        results.append(result)
        print(
            f"converted {input_path.name} -> {output_path.name}: "
            f"{result['geometry_count']} geometries, {result['triangle_count']} source triangles, "
            f"{result['output_triangle_count']} output triangles"
        )

    if args.batch:
        args.manifest.parent.mkdir(parents=True, exist_ok=True)
        args.manifest.write_text(json.dumps({"assets": results}, indent=2), encoding="utf-8")
        print(f"wrote {args.manifest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
