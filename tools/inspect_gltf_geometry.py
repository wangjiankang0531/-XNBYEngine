#!/usr/bin/env python3
"""Report indexed triangle quality for a glTF/GLB geometry asset."""

from __future__ import annotations

import argparse
import json
import math
import struct
from pathlib import Path


COMPONENTS = {5121: ("B", 1), 5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}


def load_glb(path: Path) -> tuple[dict, bytes]:
    payload = path.read_bytes()
    magic, version, _ = struct.unpack_from("<III", payload, 0)
    if magic != 0x46546C67 or version != 2:
        raise ValueError(f"not a glTF 2.0 GLB: {path}")
    json_size, json_type = struct.unpack_from("<II", payload, 12)
    if json_type != 0x4E4F534A:
        raise ValueError("first GLB chunk is not JSON")
    document = json.loads(payload[20 : 20 + json_size].decode("utf-8").rstrip(" \x00"))
    chunk_offset = 20 + json_size
    binary_size, binary_type = struct.unpack_from("<II", payload, chunk_offset)
    if binary_type != 0x004E4942:
        raise ValueError("second GLB chunk is not BIN")
    return document, payload[chunk_offset + 8 : chunk_offset + 8 + binary_size]


def accessor_values(document: dict, binary: bytes, accessor_index: int, components: int) -> list[tuple]:
    accessor = document["accessors"][accessor_index]
    view = document["bufferViews"][accessor["bufferView"]]
    fmt, component_bytes = COMPONENTS[accessor["componentType"]]
    element_bytes = component_bytes * components
    stride = view.get("byteStride", element_bytes)
    offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    unpack = struct.Struct("<" + fmt * components).unpack_from
    return [unpack(binary, offset + index * stride) for index in range(accessor["count"])]


def inspect(path: Path) -> dict:
    document, binary = load_glb(path)
    areas: list[float] = []
    normal_axis = [0.0, 0.0, 0.0]
    triangle_count = 0
    vertex_count = 0
    index_min = math.inf
    index_max = -math.inf
    triangle_primitives = 0
    mesh_summaries: list[dict] = []
    for mesh in document.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            if primitive.get("mode", 4) != 4 or "indices" not in primitive:
                continue
            positions = accessor_values(document, binary, primitive["attributes"]["POSITION"], 3)
            indices = [row[0] for row in accessor_values(document, binary, primitive["indices"], 1)]
            vertex_count += len(positions)
            triangle_count += len(indices) // 3
            triangle_primitives += 1
            index_min = min(index_min, min(indices))
            index_max = max(index_max, max(indices))
            local_areas: list[float] = []
            for offset in range(0, len(indices), 3):
                p0, p1, p2 = (positions[indices[offset + corner]] for corner in range(3))
                u = tuple(p1[axis] - p0[axis] for axis in range(3))
                v = tuple(p2[axis] - p0[axis] for axis in range(3))
                cross = (
                    u[1] * v[2] - u[2] * v[1],
                    u[2] * v[0] - u[0] * v[2],
                    u[0] * v[1] - u[1] * v[0],
                )
                area = 0.5 * math.sqrt(sum(value * value for value in cross))
                areas.append(area)
                local_areas.append(area)
                for axis in range(3):
                    normal_axis[axis] += abs(cross[axis])
            minimum = [min(position[axis] for position in positions) for axis in range(3)]
            maximum = [max(position[axis] for position in positions) for axis in range(3)]
            dimensions = [maximum[axis] - minimum[axis] for axis in range(3)]
            box_surface = 2.0 * (
                dimensions[0] * dimensions[1]
                + dimensions[1] * dimensions[2]
                + dimensions[0] * dimensions[2]
            )
            mesh_summaries.append(
                {
                    "name": mesh.get("name", ""),
                    "triangles": len(indices) // 3,
                    "dimensions": dimensions,
                    "surface_area": sum(local_areas),
                    "surface_to_box_ratio": sum(local_areas) / box_surface if box_surface > 0.0 else 0.0,
                    "median_triangle_area": sorted(local_areas)[len(local_areas) // 2] if local_areas else 0.0,
                }
            )
    ordered = sorted(areas)
    return {
        "path": path.as_posix(),
        "bytes": path.stat().st_size,
        "meshes": len(document.get("meshes", [])),
        "triangle_primitives": triangle_primitives,
        "vertices": vertex_count,
        "triangles": triangle_count,
        "index_min": None if index_min == math.inf else index_min,
        "index_max": None if index_max == -math.inf else index_max,
        "zero_area_triangles": sum(area < 1.0e-12 for area in areas),
        "tiny_area_triangles_lt_1e_8": sum(area < 1.0e-8 for area in areas),
        "median_triangle_area": ordered[len(ordered) // 2] if ordered else 0.0,
        "max_triangle_area": max(areas, default=0.0),
        "surface_area": sum(areas),
        "absolute_normal_axis_sum": normal_axis,
        "largest_surface_meshes": sorted(mesh_summaries, key=lambda item: item["surface_area"], reverse=True)[:20],
        "most_coherent_meshes": sorted(
            (item for item in mesh_summaries if item["triangles"] >= 100),
            key=lambda item: item["surface_to_box_ratio"],
            reverse=True,
        )[:20],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("asset", type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.asset), ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
