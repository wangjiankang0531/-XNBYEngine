#!/usr/bin/env python3
"""Fetch and normalize the public OSM DLG layer covering the Huilian DEM/DOM.

The checked-in GeoJSON is the deterministic runtime input.  Network access is
only required when deliberately refreshing the source data; ``--check`` never
contacts the network.
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = ROOT / "data" / "showcase" / "huilian_dlg_osm.geojson"
OVERPASS_URLS = (
    "https://overpass.kumi.systems/api/interpreter",
    "https://overpass-api.de/api/interpreter",
    "https://lz4.overpass-api.de/api/interpreter",
)
# west, south, east, north -- the audited WGS84 bounds of hero_DEM_30M.tif.
BOUNDS = (114.542834905631, 22.712074101085, 114.656446016742, 22.783462989974)


def _query() -> str:
    west, south, east, north = BOUNDS
    bbox = f"{south:.12f},{west:.12f},{north:.12f},{east:.12f}"
    selectors = (
        'way["highway"]',
        'way["railway"]',
        'way["waterway"]',
        'way["natural"="water"]',
        'way["boundary"]',
        'way["building"]',
        'way["landuse"="industrial"]',
        'way["man_made"="pipeline"]',
        'way["power"="line"]',
        'way["barrier"]',
    )
    body = "\n".join(f"  {selector}({bbox});" for selector in selectors)
    return f"[out:json][timeout:120];\n(\n{body}\n);\nout tags geom;\n"


def _clip_segment(a: list[float], b: list[float]) -> tuple[list[float], list[float]] | None:
    west, south, east, north = BOUNDS
    dx, dy = b[0] - a[0], b[1] - a[1]
    t0, t1 = 0.0, 1.0
    for p, q in ((-dx, a[0] - west), (dx, east - a[0]), (-dy, a[1] - south), (dy, north - a[1])):
        if p == 0.0:
            if q < 0.0:
                return None
            continue
        r = q / p
        if p < 0.0:
            if r > t1:
                return None
            t0 = max(t0, r)
        else:
            if r < t0:
                return None
            t1 = min(t1, r)
    return ([a[0] + t0 * dx, a[1] + t0 * dy], [a[0] + t1 * dx, a[1] + t1 * dy])


def _clip_line(coordinates: list[list[float]]) -> list[list[list[float]]]:
    parts: list[list[list[float]]] = []
    current: list[list[float]] = []
    for a, b in zip(coordinates, coordinates[1:]):
        clipped = _clip_segment(a, b)
        if clipped is None:
            if len(current) >= 2:
                parts.append(current)
            current = []
            continue
        start, end = clipped
        if not current or current[-1] != start:
            if len(current) >= 2:
                parts.append(current)
            current = [start]
        if current[-1] != end:
            current.append(end)
    if len(current) >= 2:
        parts.append(current)
    return parts


def _clip_ring(coordinates: list[list[float]]) -> list[list[float]]:
    west, south, east, north = BOUNDS
    points = coordinates[:-1] if coordinates and coordinates[0] == coordinates[-1] else list(coordinates)
    boundaries = (
        (lambda p: p[0] >= west, lambda a, b: [west, a[1] + (b[1] - a[1]) * (west - a[0]) / (b[0] - a[0])]),
        (lambda p: p[0] <= east, lambda a, b: [east, a[1] + (b[1] - a[1]) * (east - a[0]) / (b[0] - a[0])]),
        (lambda p: p[1] >= south, lambda a, b: [a[0] + (b[0] - a[0]) * (south - a[1]) / (b[1] - a[1]), south]),
        (lambda p: p[1] <= north, lambda a, b: [a[0] + (b[0] - a[0]) * (north - a[1]) / (b[1] - a[1]), north]),
    )
    for inside, intersection in boundaries:
        if not points:
            break
        clipped: list[list[float]] = []
        previous = points[-1]
        previous_inside = inside(previous)
        for point in points:
            point_inside = inside(point)
            if point_inside:
                if not previous_inside:
                    clipped.append(intersection(previous, point))
                clipped.append(point)
            elif previous_inside:
                clipped.append(intersection(previous, point))
            previous, previous_inside = point, point_inside
        points = clipped
    if len(points) < 3:
        return []
    if points[0] != points[-1]:
        points.append(points[0])
    return points


def _feature(element: dict[str, Any]) -> dict[str, Any] | None:
    geometry = element.get("geometry")
    if not isinstance(geometry, list) or len(geometry) < 2:
        return None
    coordinates = [[float(point["lon"]), float(point["lat"])] for point in geometry]
    tags = element.get("tags") if isinstance(element.get("tags"), dict) else {}
    closed = len(coordinates) >= 4 and coordinates[0] == coordinates[-1]
    polygon = closed and any(key in tags for key in ("building", "landuse", "natural"))
    geom: dict[str, Any]
    if polygon:
        clipped_ring = _clip_ring(coordinates)
        if not clipped_ring:
            return None
        geom = {"type": "Polygon", "coordinates": [clipped_ring]}
    else:
        clipped_parts = _clip_line(coordinates)
        if not clipped_parts:
            return None
        geom = (
            {"type": "LineString", "coordinates": clipped_parts[0]}
            if len(clipped_parts) == 1
            else {"type": "MultiLineString", "coordinates": clipped_parts}
        )

    properties = {str(key): str(value) for key, value in sorted(tags.items())}
    properties.update(
        {
            "osm_id": int(element["id"]),
            "osm_type": str(element.get("type", "way")),
            "source_dataset": "OpenStreetMap",
        }
    )
    return {
        "type": "Feature",
        "id": int(element["id"]),
        "properties": properties,
        "geometry": geom,
    }


def fetch() -> dict[str, Any]:
    payload = None
    errors = []
    for url in OVERPASS_URLS:
        request = urllib.request.Request(
            url,
            data=urllib.parse.urlencode({"data": _query()}).encode("utf-8"),
            headers={"User-Agent": "HongTu-Engine-DLG-Prototype/1.0"},
            method="POST",
        )
        try:
            with urllib.request.urlopen(request, timeout=150) as response:
                payload = json.load(response)
            break
        except (OSError, json.JSONDecodeError) as exc:
            errors.append(f"{url}: {exc}")
    if payload is None:
        raise OSError("; ".join(errors))

    features = []
    seen: set[int] = set()
    for element in payload.get("elements", []):
        osm_id = element.get("id")
        if not isinstance(osm_id, int) or osm_id in seen:
            continue
        feature = _feature(element)
        if feature is not None:
            features.append(feature)
            seen.add(osm_id)
    features.sort(key=lambda item: int(item["id"]))
    return {
        "type": "FeatureCollection",
        "name": "Huilian public DLG prototype",
        "bbox": list(BOUNDS),
        "source": "OpenStreetMap via Overpass API",
        "copyright": "OpenStreetMap contributors",
        "license": "Open Data Commons Open Database License (ODbL) 1.0",
        "source_url": "https://www.openstreetmap.org/copyright",
        "features": features,
    }


def validate(document: dict[str, Any]) -> tuple[int, int]:
    if document.get("type") != "FeatureCollection":
        raise ValueError("root must be a GeoJSON FeatureCollection")
    if document.get("bbox") != list(BOUNDS):
        raise ValueError("GeoJSON bbox does not match the audited Huilian DEM bounds")
    features = document.get("features")
    if not isinstance(features, list) or not features:
        raise ValueError("GeoJSON contains no DLG features")
    vertices = 0
    for feature in features:
        geometry = feature.get("geometry", {})
        coordinates = geometry.get("coordinates", [])
        if geometry.get("type") == "LineString":
            vertices += len(coordinates)
        elif geometry.get("type") == "MultiLineString":
            vertices += sum(len(line) for line in coordinates)
        elif geometry.get("type") == "Polygon":
            vertices += sum(len(ring) for ring in coordinates)
        else:
            raise ValueError(f"unsupported normalized geometry: {geometry.get('type')}")
    if vertices < len(features) * 2:
        raise ValueError("invalid DLG vertex count")
    return len(features), vertices


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true", help="validate the checked-in file without network access")
    args = parser.parse_args()
    output = args.output if args.output.is_absolute() else ROOT / args.output

    if args.check:
        document = json.loads(output.read_text(encoding="utf-8"))
    else:
        document = fetch()
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(document, ensure_ascii=False, separators=(",", ":")) + "\n", encoding="utf-8")

    feature_count, vertex_count = validate(document)
    print(f"[huilian_dlg] path={output} features={feature_count} vertices={vertex_count} bbox={BOUNDS}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"[huilian_dlg] error: {exc}", file=sys.stderr)
        raise SystemExit(1)
