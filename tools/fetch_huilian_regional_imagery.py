#!/usr/bin/env python3
"""Fetch and audit the bounded Esri imagery branches used by Huilian approach LOD."""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import math
import os
import struct
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "assets" / "tiles" / "esri_huilian"
DEFAULT_MANIFEST = ROOT / "data" / "showcase" / "huilian_regional_imagery_manifest.json"
URL_TEMPLATE = (
    "https://server.arcgisonline.com/ArcGIS/rest/services/"
    "World_Imagery/MapServer/tile/{z}/{y}/{x}"
)
ANCHOR_LON_LAT = (114.599640461187, 22.747768545530)


@dataclass(frozen=True)
class TileSet:
    name: str
    zoom: int
    minimum_x: int
    minimum_y: int
    columns: int
    rows: int

    def coordinates(self) -> list[tuple[int, int, int]]:
        return [
            (self.zoom, x, y)
            for y in range(self.minimum_y, self.minimum_y + self.rows)
            for x in range(self.minimum_x, self.minimum_x + self.columns)
        ]


TILE_SETS = (
    TileSet("south_china_context", 8, 205, 107, 8, 8),
    TileSet("huilian_near_region", 10, 833, 441, 8, 8),
)


def tile_path(root: Path, coordinate: tuple[int, int, int]) -> Path:
    z, x, y = coordinate
    return root / str(z) / str(x) / f"{y}.jpg"


def jpeg_dimensions(data: bytes) -> tuple[int, int] | None:
    if len(data) < 4 or data[:2] != b"\xff\xd8":
        return None
    offset = 2
    while offset + 4 <= len(data):
        if data[offset] != 0xFF:
            offset += 1
            continue
        while offset < len(data) and data[offset] == 0xFF:
            offset += 1
        if offset >= len(data):
            break
        marker = data[offset]
        offset += 1
        if marker in (0xD8, 0xD9):
            continue
        if offset + 2 > len(data):
            break
        segment_length = struct.unpack(">H", data[offset : offset + 2])[0]
        if segment_length < 2 or offset + segment_length > len(data):
            break
        if marker in {
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
        }:
            if segment_length < 7:
                return None
            height, width = struct.unpack(">HH", data[offset + 3 : offset + 7])
            return width, height
        offset += segment_length
    return None


def valid_tile(path: Path) -> bool:
    try:
        data = path.read_bytes()
    except OSError:
        return False
    return len(data) >= 512 and jpeg_dimensions(data) == (256, 256)


def download_tile(root: Path, coordinate: tuple[int, int, int], retries: int) -> tuple[tuple[int, int, int], str]:
    destination = tile_path(root, coordinate)
    if valid_tile(destination):
        return coordinate, "cached"
    destination.parent.mkdir(parents=True, exist_ok=True)
    z, x, y = coordinate
    url = URL_TEMPLATE.format(z=z, x=x, y=y)
    request = urllib.request.Request(
        url,
        headers={
            "User-Agent": "XNBYEngine/1.0 regional-imagery-cache",
            "Referer": "https://www.arcgis.com/",
            "Accept": "image/jpeg,image/*;q=0.8",
        },
    )
    last_error: Exception | None = None
    for attempt in range(retries):
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                data = response.read()
            if len(data) < 512 or jpeg_dimensions(data) != (256, 256):
                raise RuntimeError(f"unexpected tile payload ({len(data)} bytes)")
            temporary = destination.with_suffix(destination.suffix + ".download")
            temporary.write_bytes(data)
            os.replace(temporary, destination)
            return coordinate, "downloaded"
        except (OSError, RuntimeError, urllib.error.URLError) as exc:
            last_error = exc
            time.sleep(0.4 * (attempt + 1))
    raise RuntimeError(f"failed {z}/{x}/{y}: {last_error}")


def tile_bounds(tile_set: TileSet) -> list[float]:
    n = float(1 << tile_set.zoom)
    west = tile_set.minimum_x / n * 360.0 - 180.0
    east = (tile_set.minimum_x + tile_set.columns) / n * 360.0 - 180.0

    def latitude(row: int) -> float:
        return math.degrees(math.atan(math.sinh(math.pi * (1.0 - 2.0 * row / n))))

    north = latitude(tile_set.minimum_y)
    south = latitude(tile_set.minimum_y + tile_set.rows)
    return [west, south, east, north]


def build_manifest(root: Path) -> dict:
    manifest_sets = []
    total_tiles = 0
    total_bytes = 0
    aggregate = hashlib.sha256()
    for tile_set in TILE_SETS:
        entries = []
        set_bytes = 0
        for coordinate in tile_set.coordinates():
            path = tile_path(root, coordinate)
            if not valid_tile(path):
                raise RuntimeError(f"missing or invalid tile: {path}")
            data = path.read_bytes()
            digest = hashlib.sha256(data).hexdigest()
            relative = path.relative_to(ROOT).as_posix()
            entries.append({"zxy": list(coordinate), "path": relative, "bytes": len(data), "sha256": digest})
            aggregate.update(relative.encode("utf-8"))
            aggregate.update(bytes.fromhex(digest))
            set_bytes += len(data)
        total_tiles += len(entries)
        total_bytes += set_bytes
        manifest_sets.append(
            {
                "name": tile_set.name,
                "zoom": tile_set.zoom,
                "minimum_xy": [tile_set.minimum_x, tile_set.minimum_y],
                "grid": [tile_set.columns, tile_set.rows],
                "bounds_lon_lat_deg": tile_bounds(tile_set),
                "tile_count": len(entries),
                "tile_bytes": set_bytes,
                "atlas_grid_px": [tile_set.columns * 256, tile_set.rows * 256],
                "tiles": entries,
            }
        )
    return {
        "schema": "hongtu.huilian_regional_imagery.v1",
        "provider": "Esri World Imagery",
        "service": "ArcGIS World_Imagery MapServer",
        "url_template": URL_TEMPLATE,
        "scheme": "WebMercatorQuad_XYZ_EPSG3857",
        "anchor_lon_lat_deg": list(ANCHOR_LON_LAT),
        "tile_grid_px": [256, 256],
        "attribution": "assets/tiles/esri_huilian/SOURCE_ATTRIBUTION.md",
        "tile_sets": manifest_sets,
        "total_tile_count": total_tiles,
        "total_tile_bytes": total_bytes,
        "aggregate_sha256": aggregate.hexdigest(),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--download", action="store_true", help="download missing/invalid selected tiles")
    parser.add_argument("--check", action="store_true", help="audit all selected tiles")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--workers", type=int, default=8)
    parser.add_argument("--retries", type=int, default=3)
    args = parser.parse_args()

    coordinates = [coordinate for tile_set in TILE_SETS for coordinate in tile_set.coordinates()]
    if args.download:
        downloaded = 0
        cached = 0
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(args.workers, 16))) as executor:
            futures = [executor.submit(download_tile, args.output, coordinate, max(1, args.retries)) for coordinate in coordinates]
            for future in concurrent.futures.as_completed(futures):
                coordinate, status = future.result()
                downloaded += status == "downloaded"
                cached += status == "cached"
                completed = downloaded + cached
                if completed % 16 == 0 or completed == len(coordinates):
                    print(f"regional imagery: {completed}/{len(coordinates)} downloaded={downloaded} cached={cached}")

    try:
        manifest = build_manifest(args.output)
    except RuntimeError as exc:
        print(f"regional imagery audit: FAIL: {exc}")
        return 1
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(
        "regional imagery audit: PASS "
        f"tiles={manifest['total_tile_count']} bytes={manifest['total_tile_bytes']} "
        f"sha256={manifest['aggregate_sha256']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
