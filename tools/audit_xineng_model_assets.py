#!/usr/bin/env python3
"""Audit Xineng source model assets against the showcase scene manifest.

The runtime cannot consume native .max files directly. This tool keeps the
authoring-source inventory explicit so new MAX/FBX drops can be triaged before
they are wired into procedural proxies, FBX/glTF conversions, or future DCC
export batches.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any


MODEL_DIR_NAME = "\u6a21\u578b"
MODEL_EXTENSIONS = {".max", ".fbx"}
TEXTURE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".tga"}
DEFAULT_SCENE = Path("data/showcase/industrial_park.scene.json")
DEFAULT_CONVERTED = Path("data/showcase/converted_model_assets.json")
DEFAULT_JSON = Path("data/showcase/xineng_model_asset_audit.json")
DEFAULT_MARKDOWN = Path("docs/project/xineng_model_asset_audit.md")


@dataclass(frozen=True)
class SourceAsset:
    path: Path
    relative_path: str
    name: str
    extension: str
    bytes: int
    category: str
    conversion_track: str
    priority: int


def project_root() -> Path:
    return Path(__file__).resolve().parents[1]


def slash(path: Path) -> str:
    return path.as_posix()


def rel_to_root(root: Path, path: Path) -> str:
    return slash(path.relative_to(root))


def read_json(path: Path) -> Any:
    return json.loads(path.read_text(encoding="utf-8"))


def iter_scene_model_refs(value: Any) -> set[str]:
    refs: set[str] = set()

    def walk(node: Any) -> None:
        if isinstance(node, dict):
            for child in node.values():
                walk(child)
        elif isinstance(node, list):
            for child in node:
                walk(child)
        elif isinstance(node, str):
            normalized = node.replace("\\", "/")
            lower = normalized.lower()
            source_prefix = f"assets/{MODEL_DIR_NAME}/"
            if normalized.startswith(source_prefix) and any(lower.endswith(ext) for ext in MODEL_EXTENSIONS):
                refs.add(normalized)

    walk(value)
    return refs


def classify(name: str) -> str:
    lower = name.lower()
    if "\u5929\u6d25\u5316\u5de5" in name or lower.startswith("peilou") or "\u5730\u5f62" in name:
        return "whole_plant_context"
    if "\u4ed3\u5e93" in name or "\u8d27\u67b6" in name or "\u53c9\u8f66" in name:
        return "warehouse_logistics"
    if "\u6d88\u9632" in name or "\u55b7" in name:
        return "fire_emergency"
    if any(token in name for token in ("\u95e8\u536b", "\u9053\u95f8", "\u76d1\u63a7", "\u6444\u50cf", "\u5c4f", "\u5b57\u4f53", "\u6807\u8bc6")):
        return "security_access_display"
    if any(token in name for token in ("\u9ad8\u538b\u7535\u5854", "\u6362\u70ed", "\u5730\u78c5", "\u8def\u706f")):
        return "utility_power"
    if any(token in name for token in ("\u7a3b\u7530", "\u77f3\u5b50", "\u884c\u8f66\u7ebf", "\u505c\u8f66")):
        return "terrain_road_fusion"
    return "equipment_process"


def conversion_track(extension: str, size: int, converted: bool) -> str:
    if extension == ".fbx":
        return "runtime_gltf_ready" if converted else "fbx_converter_candidate"
    if size >= 100_000_000:
        return "dcc_export_segment_lod_first"
    if size >= 25_000_000:
        return "dcc_export_lod_proxy_first"
    return "dcc_batch_export_candidate"


def priority(extension: str, size: int, category: str, converted: bool) -> int:
    score = 0
    if extension == ".max":
        score += 30
    if size >= 500_000_000:
        score += 50
    elif size >= 100_000_000:
        score += 40
    elif size >= 25_000_000:
        score += 25
    elif size >= 1_000_000:
        score += 10
    if category in {"whole_plant_context", "fire_emergency", "warehouse_logistics"}:
        score += 12
    if converted:
        score -= 20
    return max(score, 0)


def converted_source_names(root: Path, manifest_path: Path) -> set[str]:
    if not manifest_path.exists():
        return set()
    manifest = read_json(manifest_path)
    names: set[str] = set()
    for item in manifest.get("assets", []):
        raw = item.get("input")
        if not raw:
            continue
        try:
            path = Path(raw)
            if not path.is_absolute():
                path = root / path
            names.add(path.name)
        except (OSError, ValueError):
            continue
    return names


def scan_sources(root: Path, converted_names: set[str]) -> list[SourceAsset]:
    source_dir = root / "assets" / MODEL_DIR_NAME
    if not source_dir.exists():
        raise FileNotFoundError(source_dir)
    assets: list[SourceAsset] = []
    for path in sorted(source_dir.iterdir(), key=lambda item: item.name.lower()):
        if not path.is_file():
            continue
        extension = path.suffix.lower()
        if extension not in MODEL_EXTENSIONS:
            continue
        size = path.stat().st_size
        category = classify(path.name)
        converted = path.name in converted_names
        assets.append(
            SourceAsset(
                path=path,
                relative_path=rel_to_root(root, path),
                name=path.name,
                extension=extension,
                bytes=size,
                category=category,
                conversion_track=conversion_track(extension, size, converted),
                priority=priority(extension, size, category, converted),
            )
        )
    return assets


def scan_texture_sidecars(root: Path) -> dict[str, Any]:
    source_dir = root / "assets" / MODEL_DIR_NAME
    files = [p for p in source_dir.rglob("*") if p.is_file() and p.suffix.lower() in TEXTURE_EXTENSIONS]
    by_extension: dict[str, dict[str, int]] = {}
    for path in files:
        ext = path.suffix.lower()
        entry = by_extension.setdefault(ext, {"count": 0, "bytes": 0})
        entry["count"] += 1
        entry["bytes"] += path.stat().st_size
    return {
        "count": len(files),
        "bytes": sum(path.stat().st_size for path in files),
        "by_extension": dict(sorted(by_extension.items())),
    }


def build_audit(root: Path, scene_path: Path, converted_path: Path) -> dict[str, Any]:
    scene = read_json(scene_path)
    scene_refs = iter_scene_model_refs(scene)
    converted_names = converted_source_names(root, converted_path)
    sources = scan_sources(root, converted_names)
    existing = {asset.relative_path for asset in sources}
    folder_files = [p for p in (root / "assets" / MODEL_DIR_NAME).rglob("*") if p.is_file()]

    by_category: dict[str, dict[str, int]] = {}
    by_extension: dict[str, dict[str, int]] = {}
    for asset in sources:
        for bucket, key in ((by_category, asset.category), (by_extension, asset.extension)):
            entry = bucket.setdefault(key, {"count": 0, "bytes": 0})
            entry["count"] += 1
            entry["bytes"] += asset.bytes

    heavy = sorted((asset for asset in sources if asset.bytes >= 25_000_000), key=lambda item: (-item.priority, -item.bytes))
    export_queue = sorted(
        (asset for asset in sources if asset.extension == ".max"),
        key=lambda item: (-item.priority, -item.bytes, item.name.lower()),
    )
    fbx_runtime = sorted(
        (asset for asset in sources if asset.extension == ".fbx"),
        key=lambda item: (-item.priority, -item.bytes, item.name.lower()),
    )

    return {
        "schema_version": 1,
        "source_folder": slash(Path("assets") / MODEL_DIR_NAME),
        "scene_manifest": slash(scene_path.relative_to(root)),
        "converted_manifest": slash(converted_path.relative_to(root)),
        "summary": {
            "model_sources": len(sources),
            "max_sources": sum(1 for asset in sources if asset.extension == ".max"),
            "fbx_sources": sum(1 for asset in sources if asset.extension == ".fbx"),
            "model_source_bytes": sum(asset.bytes for asset in sources),
            "source_folder_files": len(folder_files),
            "source_folder_bytes": sum(path.stat().st_size for path in folder_files),
            "scene_model_refs": len(scene_refs),
            "missing_scene_refs": len(existing - scene_refs),
            "stale_scene_refs": len(scene_refs - existing),
            "converted_fbx_sources": sum(1 for asset in fbx_runtime if asset.conversion_track == "runtime_gltf_ready"),
            "texture_sidecars": scan_texture_sidecars(root),
        },
        "by_category": dict(sorted(by_category.items())),
        "by_extension": dict(sorted(by_extension.items())),
        "coverage": {
            "missing_scene_refs": sorted(existing - scene_refs),
            "stale_scene_refs": sorted(scene_refs - existing),
        },
        "heavy_candidates": [asset_row(asset) for asset in heavy],
        "dcc_export_queue": [asset_row(asset) for asset in export_queue],
        "fbx_runtime_queue": [asset_row(asset) for asset in fbx_runtime],
    }


def asset_row(asset: SourceAsset) -> dict[str, Any]:
    return {
        "path": asset.relative_path,
        "name": asset.name,
        "extension": asset.extension,
        "bytes": asset.bytes,
        "category": asset.category,
        "conversion_track": asset.conversion_track,
        "priority": asset.priority,
    }


def write_markdown(path: Path, audit: dict[str, Any]) -> None:
    summary = audit["summary"]
    lines = [
        "# Xineng Model Asset Audit",
        "",
        "This file is generated by `tools/audit_xineng_model_assets.py`.",
        "",
        "## Summary",
        "",
        f"- Model sources: {summary['model_sources']} ({summary['max_sources']} MAX, {summary['fbx_sources']} FBX).",
        f"- Model source bytes: {summary['model_source_bytes']:,}.",
        f"- Scene model references: {summary['scene_model_refs']}.",
        f"- Missing scene references: {summary['missing_scene_refs']}.",
        f"- Stale scene references: {summary['stale_scene_refs']}.",
        f"- Converted FBX runtime sources: {summary['converted_fbx_sources']}.",
        f"- Texture sidecars: {summary['texture_sidecars']['count']} files, {summary['texture_sidecars']['bytes']:,} bytes.",
        "",
        "## Heavy Candidates",
        "",
        "| Asset | Category | Bytes | Track | Priority |",
        "| --- | --- | ---: | --- | ---: |",
    ]
    for item in audit["heavy_candidates"]:
        lines.append(
            f"| `{item['path']}` | {item['category']} | {item['bytes']:,} | "
            f"{item['conversion_track']} | {item['priority']} |"
        )
    lines.extend(
        [
            "",
            "## Category Totals",
            "",
            "| Category | Count | Bytes |",
            "| --- | ---: | ---: |",
        ]
    )
    for category, value in audit["by_category"].items():
        lines.append(f"| {category} | {value['count']} | {value['bytes']:,} |")
    lines.extend(
        [
            "",
            "## Next Gate",
            "",
            "A run is healthy only when `missing_scene_refs` and `stale_scene_refs` are both zero. "
            "MAX files over 100 MB should be exported as segmented FBX/GLB with LODs before runtime loading.",
            "",
        ]
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=project_root())
    parser.add_argument("--scene", type=Path, default=DEFAULT_SCENE)
    parser.add_argument("--converted", type=Path, default=DEFAULT_CONVERTED)
    parser.add_argument("--write-json", type=Path, default=DEFAULT_JSON)
    parser.add_argument("--write-markdown", type=Path, default=DEFAULT_MARKDOWN)
    parser.add_argument("--check", action="store_true", help="fail if scene references are missing or stale")
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
        "xineng model audit: "
        f"{summary['model_sources']} sources, "
        f"{summary['max_sources']} MAX, {summary['fbx_sources']} FBX, "
        f"missing={summary['missing_scene_refs']}, stale={summary['stale_scene_refs']}"
    )
    if args.check and (summary["missing_scene_refs"] or summary["stale_scene_refs"]):
        print("model source coverage check failed", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
