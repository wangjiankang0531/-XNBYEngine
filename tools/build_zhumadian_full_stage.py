#!/usr/bin/env python3
"""Export every source root, preserving the full reality-mesh footprint.

The full-coverage base tier is static; finer child tiers remain future paging
work. Batch conversion avoids Windows command-line limits. Each root's source
hash, exported group and geometry bounds are recorded for coverage auditing.
"""
from __future__ import annotations

import argparse
import array
import json
import math
import os
import re
import sys
from pathlib import Path

import build_zhumadian_stage_asset as base

SIZE = 8192
BATCH = 48


def text_lines(path):
    with path.open(encoding="utf-8") as source:
        yield from source


def roots_from(source: Path):
    base.read_metadata(source)
    directories = sorted((source / "Data").iterdir(), key=lambda p: int(p.name.split("_")[1]))
    return [base.choose_root_file(p, int(p.name.split("_")[1])) for p in directories if p.is_dir()]


def source_records(root, roots):
    return [{"tile_id": r.tile_id, "level": r.level, "path": base.manifest_path(root, r.path),
             "bytes": r.path.stat().st_size, "sha256": base.sha256(r.path)} for r in roots]


def export_batches(root, source, roots, work):
    defaults = base.parse_args([])
    osg = base.resolve_path(root, defaults.osgconv)
    plugins = base.resolve_path(root, defaults.osg_plugin_dir)
    base.require_toolchain(osg, plugins, base.resolve_path(root, defaults.assimp_converter),
                           base.resolve_path(root, defaults.assimp_runtime_dir))
    alias = base.ensure_ascii_source_alias(source, root / "work/zmd_source")
    env = os.environ.copy()
    env["PATH"] = str(osg.parent) + os.pathsep + env.get("PATH", "")
    env["OSG_LIBRARY_PATH"] = str(plugins)
    env["OSG_OPTIMIZER"] = "DEFAULT TEXTURE_ATLAS_BUILDER"
    env["OSG_NOTIFY_LEVEL"] = "WARN"
    outputs = []
    for start in range(0, len(roots), BATCH):
        batch = roots[start:start + BATCH]
        folder = work / f"batch_{start:04d}"
        folder.mkdir(parents=True, exist_ok=True)
        obj = folder / "roots.obj"
        stamp = folder / "sources.json"
        records = source_records(root, batch)
        # Resume only when the exact source hashes match and the prior export completed.
        if not (obj.exists() and stamp.exists() and json.loads(stamp.read_text()) == records):
            base.run_hidden([str(osg), "-O", "OutputTextureFiles",
                             *[str(alias / r.path.relative_to(source)) for r in batch], str(obj)],
                            folder, env, f"roots {start + 1}..{start + len(batch)}")
            if not obj.exists() or not obj.with_suffix(".mtl").exists():
                base.fail(f"missing batch output: {obj}")
            stamp.write_text(json.dumps(records), encoding="utf-8")
        outputs.append(obj)
        print(f"exported {start + len(batch)}/{len(roots)} source roots", flush=True)
    return outputs


def material_images(obj):
    result, current = {}, None
    for line in obj.with_suffix(".mtl").read_text().splitlines():
        fields = line.strip().split(maxsplit=1)
        if not fields:
            continue
        if fields[0] == "newmtl":
            current = fields[1]
        elif fields[0] == "map_Kd":
            result[current] = obj.parent / fields[1]
    if not result or any(not p.is_file() for p in result.values()):
        base.fail(f"missing real textures: {obj}")
    return result


def audit_root_geometry(roots, objects):
    """Prove each source root has actual faces, not just an entry in a file list."""
    covered = 0
    for start, obj in zip(range(0, len(roots), BATCH), objects):
        expected = set()
        for root in roots[start:start + BATCH]:
            names = set(re.findall(rb"Tile_[+-][0-9]+_[+-][0-9]+_L[0-9]+_0u", root.path.read_bytes()))
            if len(names) != 1:
                base.fail(f"ambiguous source root geometry name: {root.path}")
            expected.add(next(iter(names)).decode("ascii"))
        counts, current = {}, None
        for line in text_lines(obj):
            if line.startswith("g "):
                match = re.search(r"Tile_[+-][0-9]+_[+-][0-9]+_L[0-9]+_0u", line)
                current = match.group() if match else None
            elif line.startswith("f "):
                if current is None:
                    base.fail(f"unattributed geometry in {obj}")
                counts[current] = counts.get(current, 0) + 1
        if set(counts) != expected or len(expected) != len(roots[start:start + BATCH]):
            base.fail(f"root geometry coverage mismatch in {obj}: missing={expected - set(counts)}")
        covered += len(counts)
    if covered != len(roots):
        base.fail("incomplete source root geometry coverage")
    return covered


def pack_atlas(objects, output):
    from PIL import Image
    images = list(dict.fromkeys(p for obj in objects for p in material_images(obj).values()))
    columns = math.ceil(math.sqrt(len(images)))
    cell = SIZE // columns
    if cell < 64:
        base.fail("too many atlas pages for full-base texture budget")
    atlas = Image.new("RGB", (SIZE, SIZE))
    mapping = {}
    for index, path in enumerate(images):
        # Four-pixel gutters preserve the edge under linear filtering.
        x, y = index % columns * cell + 4, index // columns * cell + 4
        with Image.open(path) as source:
            width, height = source.size
            scale = (cell - 8) / max(width, height)
            width, height = max(1, round(width * scale)), max(1, round(height * scale))
            tile = source.convert("RGB").resize((width, height), Image.Resampling.LANCZOS)
        atlas.paste(tile, (x, y))
        for offset in range(1, 5):
            atlas.paste(tile.crop((0, 0, 1, height)), (x - offset, y))
            atlas.paste(tile.crop((width - 1, 0, width, height)), (x + width - 1 + offset, y))
            atlas.paste(tile.crop((0, 0, width, 1)), (x, y - offset))
            atlas.paste(tile.crop((0, height - 1, width, height)), (x, y + height - 1 + offset))
        mapping[path] = (x, y, width, height)
    atlas.save(output, quality=94)
    return mapping, len(images)


def write_geometry(objects, mapping, output):
    positions, normals, uvs, indices = (array.array("f"), array.array("f"), array.array("f"), array.array("I"))
    low, high = [math.inf] * 3, [-math.inf] * 3
    batches = []
    for obj in objects:
        vertices, texcoords, ns = [], [], []
        materials = material_images(obj)
        texture, unique = None, {}
        before = len(indices)
        batch_min, batch_max = [math.inf] * 3, [-math.inf] * 3
        for line in text_lines(obj):
            fields = line.split()
            if not fields:
                continue
            kind = fields[0]
            if kind == "v":
                vertices.append(tuple(map(float, fields[1:4])))
            elif kind == "vn":
                ns.append(tuple(map(float, fields[1:4])))
            elif kind == "vt":
                texcoords.append(tuple(map(float, fields[1:3])))
            elif kind == "usemtl":
                texture = materials[fields[1]]
            elif kind == "f":
                if texture is None or len(fields) != 4:
                    base.fail(f"expected textured triangles: {obj}: {line}")
                x, y, width, height = mapping[texture]
                corners = [vertices[int(token.split("/")[0]) - 1] for token in fields[1:]]
                a = [corners[1][i] - corners[0][i] for i in range(3)]
                b = [corners[2][i] - corners[0][i] for i in range(3)]
                cross = [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]]
                length = math.sqrt(sum(v*v for v in cross))
                face_normal = tuple(v / length for v in cross) if length > 1e-12 else (0.0, 0.0, 1.0)
                for token in fields[1:]:
                    components = token.split("/")
                    vi, ti = int(components[0]), int(components[1])
                    ni = int(components[2]) if len(components) > 2 and components[2] else 0
                    key = (texture, token, face_normal if ni == 0 else None)
                    if key not in unique:
                        if min(vi, ti) <= 0 or ni < 0:
                            base.fail("negative/empty OBJ indices unsupported")
                        pos, uv = vertices[vi - 1], texcoords[ti - 1]
                        if not all(math.isfinite(v) for v in pos + uv):
                            base.fail("non-finite source geometry")
                        if not all(-0.001 <= v <= 1.001 for v in uv):
                            base.fail(f"source UV outside its atlas: {uv}")
                        unique[key] = len(positions) // 3
                        positions.extend(pos)
                        normals.extend(ns[ni - 1] if ni else face_normal)
                        # OBJ is lower-left; glTF texture coordinates are upper-left.
                        uvs.extend(((x + uv[0] * width) / SIZE, (y + (1 - uv[1]) * height) / SIZE))
                        low = [min(a, b) for a, b in zip(low, pos)]
                        high = [max(a, b) for a, b in zip(high, pos)]
                        batch_min = [min(a, b) for a, b in zip(batch_min, pos)]
                        batch_max = [max(a, b) for a, b in zip(batch_max, pos)]
                    indices.append(unique[key])
        if len(indices) == before:
            base.fail(f"empty root batch: {obj}")
        batches.append({"batch": obj.parent.name, "triangles": (len(indices) - before) // 3,
                        "bounds_min": batch_min, "bounds_max": batch_max})
        print(f"geometry {obj.parent.name}: {len(indices)//3} cumulative triangles", flush=True)
    views = []
    offset = 0
    with output.open("wb") as target:
        for data in (positions, normals, uvs, indices):
            if sys.byteorder != "little":
                data.byteswap()
            data.tofile(target)
            size = len(data) * data.itemsize
            views.append({"buffer": 0, "byteOffset": offset, "byteLength": size})
            offset += size
    count = len(positions) // 3
    return views, count, len(indices), low, high, batches


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", action="store_true")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    root = base.repo_root()
    source = root / "assets/驻马店园区"
    roots = roots_from(source)
    records = source_records(root, roots)
    output = root / "assets/showcase/runtime/zhumadian/full"
    manifest_path = root / "data/showcase/zhumadian_full_stage_manifest.json"
    names = ["zhumadian_full.gltf", "zhumadian_full.bin", "zhumadian_full_atlas.jpg"]
    if args.build:
        work = root / "work/zmd_full_stage_convert"
        objects = export_batches(root, source, roots, work)
        audit_root_geometry(roots, objects)
        output.mkdir(parents=True, exist_ok=True)
        mapping, page_count = pack_atlas(objects, output / names[2])
        views, vertices, indices, low, high, batches = write_geometry(objects, mapping, output / names[1])
        doc = {"asset": {"version": "2.0", "generator": "HongTu full-root OSGB base exporter"},
               "buffers": [{"uri": names[1], "byteLength": (output / names[1]).stat().st_size}],
               "bufferViews": views,
               "accessors": [
                   {"bufferView": 0, "componentType": 5126, "count": vertices, "type": "VEC3", "min": low, "max": high},
                   {"bufferView": 1, "componentType": 5126, "count": vertices, "type": "VEC3"},
                   {"bufferView": 2, "componentType": 5126, "count": vertices, "type": "VEC2"},
                   {"bufferView": 3, "componentType": 5125, "count": indices, "type": "SCALAR"}],
               "images": [{"uri": names[2]}], "textures": [{"source": 0}],
               "materials": [{"doubleSided": True, "pbrMetallicRoughness": {
                   "baseColorTexture": {"index": 0}, "metallicFactor": 0, "roughnessFactor": 1}}],
               "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                           "indices": 3, "material": 0, "mode": 4}]}],
               "nodes": [{"mesh": 0}], "scenes": [{"nodes": [0]}], "scene": 0}
        (output / names[0]).write_text(json.dumps(doc, indent=2), encoding="utf-8")
        manifest = {"schema": "hongtu.zhumadian_full_root_stage.v1", "srs": base.EXPECTED_SRS,
                    "srs_origin": list(base.EXPECTED_SRS_ORIGIN), "source_roots": records,
                    "root_count": len(roots), "all_source_roots_exported": True,
                    "child_lod_streaming": False, "base_only": True,
                    "atlas_size": [SIZE, SIZE], "source_atlas_pages": page_count,
                    "vertices": vertices, "triangles": indices // 3, "bounds_min": low, "bounds_max": high,
                    "batches": batches, "distribution_policy": "local_non_public_demo_only",
                    "files": [{"name": n, "bytes": (output / n).stat().st_size,
                               "sha256": base.sha256(output / n)} for n in names]}
        manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest["source_roots"] != records or manifest["root_count"] != len(roots):
        base.fail("full source root selection/hash mismatch")
    for entry in manifest["files"]:
        path = output / entry["name"]
        if path.stat().st_size != entry["bytes"] or base.sha256(path) != entry["sha256"]:
            base.fail(f"full-base runtime hash mismatch: {path}")
    objects = [root / "work/zmd_full_stage_convert" / f"batch_{start:04d}" / "roots.obj"
               for start in range(0, len(roots), BATCH)]
    audit_root_geometry(roots, objects)
    print(f"FULL ROOT COVERAGE PASS: {len(roots)} roots, {manifest['triangles']} triangles, "
          f"bounds={manifest['bounds_min']}..{manifest['bounds_max']}; child streaming still pending", flush=True)


if __name__ == "__main__":
    main()
