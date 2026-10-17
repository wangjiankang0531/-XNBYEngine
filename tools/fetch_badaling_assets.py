#!/usr/bin/env python3
"""Fetch and build the Badaling terrain package used by badaling_demo.

The generated binary is intentionally small enough for the demo and tests:
it samples public DEM/imagery tiles into one local terrain grid, then stores
OpenStreetMap wall paths in the same file so the demo can run offline.
"""

import argparse
import json
import math
import struct
import sys
import urllib.parse
import urllib.request
from pathlib import Path


DEFAULT_CENTER_LAT = 40.3560
DEFAULT_CENTER_LON = 116.0140
DEFAULT_ZOOM = 14
DEFAULT_RADIUS = 2
DEFAULT_OUT_SIZE = 513
DEFAULT_XY_SCALE = 0.18
DEFAULT_Z_SCALE = 0.12


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def tile_xy(lat: float, lon: float, zoom: int) -> tuple[int, int]:
    n = 2**zoom
    x = int((lon + 180.0) / 360.0 * n)
    y = int((1.0 - math.asinh(math.tan(math.radians(lat))) / math.pi) / 2.0 * n)
    return x, y


def tile_float_to_lonlat(x: float, y: float, zoom: int) -> tuple[float, float]:
    n = 2.0**zoom
    lon = x / n * 360.0 - 180.0
    lat = math.degrees(math.atan(math.sinh(math.pi * (1.0 - 2.0 * y / n))))
    return lon, lat


def lonlat_to_local(lon: float, lat: float, center_lat: float, center_lon: float, xy_scale: float) -> tuple[float, float]:
    meters_per_deg_lat = 111_320.0
    meters_per_deg_lon = math.cos(math.radians(center_lat)) * meters_per_deg_lat
    x = (lon - center_lon) * meters_per_deg_lon * xy_scale
    y = (lat - center_lat) * meters_per_deg_lat * xy_scale
    return x, y


def download(url: str, path: Path, user_agent: str, offline: bool) -> None:
    if path.exists() and path.stat().st_size > 100:
        return
    if offline:
        raise FileNotFoundError(f"missing cached asset while offline: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    req = urllib.request.Request(url, headers={"User-Agent": user_agent})
    with urllib.request.urlopen(req, timeout=90) as response:
        path.write_bytes(response.read())


def sample_rgb_bilinear(img, u: float, v: float) -> tuple[int, int, int]:
    w, h = img.size
    u = max(0.0, min(float(w - 1), u))
    v = max(0.0, min(float(h - 1), v))
    x0 = int(math.floor(u))
    y0 = int(math.floor(v))
    x1 = min(x0 + 1, w - 1)
    y1 = min(y0 + 1, h - 1)
    tx = u - x0
    ty = v - y0
    p00 = img.getpixel((x0, y0))
    p10 = img.getpixel((x1, y0))
    p01 = img.getpixel((x0, y1))
    p11 = img.getpixel((x1, y1))
    out = []
    for i in range(3):
        a = p00[i] * (1.0 - tx) + p10[i] * tx
        b = p01[i] * (1.0 - tx) + p11[i] * tx
        out.append(int(round(a * (1.0 - ty) + b * ty)))
    return tuple(out)


def terrarium_height(rgb: tuple[int, int, int]) -> float:
    r, g, b = rgb
    return (r * 256.0 + g + b / 256.0) - 32768.0


def despike_heights(values: list[float], width: int, height: int) -> list[float]:
    result = values[:]
    for y in range(1, height - 1):
        for x in range(1, width - 1):
            idx = y * width + x
            around = []
            for yy in range(y - 1, y + 2):
                for xx in range(x - 1, x + 2):
                    around.append(values[yy * width + xx])
            around.sort()
            median = around[len(around) // 2]
            if abs(values[idx] - median) > 55.0:
                result[idx] = median
    return result


def smooth_heights(values: list[float], width: int, height: int, passes: int = 3) -> list[float]:
    current = values[:]
    kernel = (1.0, 4.0, 6.0, 4.0, 1.0)
    for _ in range(passes):
        temp = current[:]
        for y in range(height):
            for x in range(width):
                total = 0.0
                weight_sum = 0.0
                for k, weight in enumerate(kernel):
                    xx = min(width - 1, max(0, x + k - 2))
                    total += current[y * width + xx] * weight
                    weight_sum += weight
                temp[y * width + x] = total / weight_sum
        current = temp[:]
        for y in range(height):
            for x in range(width):
                total = 0.0
                weight_sum = 0.0
                for k, weight in enumerate(kernel):
                    yy = min(height - 1, max(0, y + k - 2))
                    total += temp[yy * width + x] * weight
                    weight_sum += weight
                current[y * width + x] = total / weight_sum
    return current


def fetch_osm(source_dir: Path, user_agent: str, offline: bool) -> dict:
    out = source_dir / "badaling_osm.json"
    if out.exists() and out.stat().st_size > 1000:
        return json.loads(out.read_text(encoding="utf-8"))
    if offline:
        raise FileNotFoundError(f"missing cached OSM response while offline: {out}")

    query = """[out:json][timeout:40];
(
way["name"~"Great Wall|Badaling",i](40.31,115.94,40.40,116.10);
way["historic"="citywalls"](40.31,115.94,40.40,116.10);
way["barrier"="wall"](40.31,115.94,40.40,116.10);
);
(._;>;);
out body;"""
    data = urllib.parse.urlencode({"data": query}).encode("utf-8")
    req = urllib.request.Request(
        "https://overpass-api.de/api/interpreter",
        data=data,
        headers={
            "User-Agent": user_agent,
            "Content-Type": "application/x-www-form-urlencoded",
        },
    )
    source_dir.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(req, timeout=90) as response:
        out.write_bytes(response.read())
    return json.loads(out.read_text(encoding="utf-8"))


def simplify_path(points: list[tuple[float, float]], min_dist: float = 1.8) -> list[tuple[float, float]]:
    if not points:
        return []
    out = [points[0]]
    for p in points[1:]:
        dx = p[0] - out[-1][0]
        dy = p[1] - out[-1][1]
        if dx * dx + dy * dy >= min_dist * min_dist:
            out.append(p)
    if len(out) > 1 and out[-1] != points[-1]:
        out.append(points[-1])
    return out


def extract_wall_paths(osm: dict, bounds: tuple[float, float, float, float], args: argparse.Namespace) -> list:
    min_x, max_x, min_y, max_y = bounds
    nodes = {
        e["id"]: (e["lon"], e["lat"])
        for e in osm.get("elements", [])
        if e.get("type") == "node" and "lat" in e and "lon" in e
    }
    paths = []
    for way in osm.get("elements", []):
        if way.get("type") != "way":
            continue
        tags = way.get("tags", {})
        text = " ".join(str(v) for v in tags.values())
        score = 0
        if tags.get("historic") == "citywalls":
            score += 4
        if tags.get("barrier") in ("city_wall", "wall"):
            score += 3
        if "Badaling" in text or "Great Wall" in text or "\u957f\u57ce" in text:
            score += 4
        if score < 7:
            continue

        pts = []
        for node_id in way.get("nodes", []):
            if node_id not in nodes:
                continue
            lon, lat = nodes[node_id]
            x, y = lonlat_to_local(lon, lat, args.center_lat, args.center_lon, args.xy_scale)
            if min_x - 40.0 <= x <= max_x + 40.0 and min_y - 40.0 <= y <= max_y + 40.0:
                pts.append((x, y))
        pts = simplify_path(pts)
        if len(pts) >= 3:
            length = sum(math.hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1]) for i in range(len(pts) - 1))
            if length >= 8.0:
                paths.append((length, pts))

    paths.sort(reverse=True, key=lambda item: item[0])
    return [pts for _, pts in paths[:28]]


def load_tile_mosaics(cache_dir: Path, args: argparse.Namespace):
    try:
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError("Pillow is required to build Badaling assets. Install pillow or run with --offline if the asset exists.") from exc

    cx, cy = tile_xy(args.center_lat, args.center_lon, args.zoom)
    xs = list(range(cx - args.radius, cx + args.radius + 1))
    ys = list(range(cy - args.radius, cy + args.radius + 1))
    size = 256 * len(xs)

    dem = Image.new("RGB", (size, size))
    color = Image.new("RGB", (size, size))
    for row, y in enumerate(ys):
        for col, x in enumerate(xs):
            dem_path = cache_dir / "terrarium" / str(args.zoom) / str(x) / f"{y}.png"
            sat_path = cache_dir / "s2cloudless-2020" / str(args.zoom) / str(x) / f"{y}.jpg"
            download(f"https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{args.zoom}/{x}/{y}.png",
                     dem_path, args.user_agent, args.offline)
            download(f"https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless-2020_3857/default/g/{args.zoom}/{y}/{x}.jpg",
                     sat_path, args.user_agent, args.offline)
            dem.paste(Image.open(dem_path).convert("RGB"), (col * 256, row * 256))
            color.paste(Image.open(sat_path).convert("RGB"), (col * 256, row * 256))
            print(f"tile z{args.zoom}/{x}/{y}")

    return dem, color, min(xs), min(ys), len(xs)


def write_manifest(path: Path, args: argparse.Namespace) -> None:
    path.write_text(
        "Badaling demo real-world asset cache\n"
        "\n"
        "DEM: AWS Open Data Registry Terrain Tiles, Terrarium PNG format, "
        "https://registry.opendata.aws/terrain-tiles/\n"
        "Imagery: EOX Sentinel-2 cloudless 2020 Web Mercator tiles, "
        "https://s2maps.eu/ and https://tiles.maps.eox.at/; license summary: "
        "https://cloudless.eox.at/documentation/license\n"
        "Vector wall paths: OpenStreetMap contributors via Overpass API, "
        "https://www.openstreetmap.org/copyright\n"
        "\n"
        f"Center: lat={args.center_lat}, lon={args.center_lon}; zoom={args.zoom}; radius={args.radius}; "
        f"output={args.out_size}x{args.out_size}; xy_scale={args.xy_scale}; z_scale={args.z_scale}\n",
        encoding="utf-8",
    )


def build_asset(args: argparse.Namespace) -> Path:
    root = Path(args.root).resolve()
    asset_dir = root / "assets" / "badaling"
    cache_dir = asset_dir / "cache"
    source_dir = asset_dir / "source"
    asset_path = asset_dir / "badaling_terrain.bin"
    manifest_path = asset_dir / "SOURCE_ATTRIBUTION.txt"

    if args.offline and asset_path.exists() and not args.force:
        print(f"offline: existing asset is available: {asset_path}")
        return asset_path

    cache_dir.mkdir(parents=True, exist_ok=True)
    source_dir.mkdir(parents=True, exist_ok=True)
    asset_dir.mkdir(parents=True, exist_ok=True)

    dem, color, tile_x0, tile_y0, tile_count = load_tile_mosaics(cache_dir, args)
    mosaic_px = tile_count * 256

    lon_w, lat_n = tile_float_to_lonlat(tile_x0, tile_y0, args.zoom)
    lon_e, lat_s = tile_float_to_lonlat(tile_x0 + tile_count, tile_y0 + tile_count, args.zoom)
    min_x, min_y = lonlat_to_local(lon_w, lat_s, args.center_lat, args.center_lon, args.xy_scale)
    max_x, max_y = lonlat_to_local(lon_e, lat_n, args.center_lat, args.center_lon, args.xy_scale)

    raw_heights = []
    raw_colors = bytearray()
    for iy in range(args.out_size):
        yn = iy / (args.out_size - 1)
        v = (1.0 - yn) * (mosaic_px - 1)
        for ix in range(args.out_size):
            xn = ix / (args.out_size - 1)
            u = xn * (mosaic_px - 1)
            h = terrarium_height(sample_rgb_bilinear(dem, u, v))
            r, g, b = sample_rgb_bilinear(color, u, v)
            r = max(0, min(255, int((r - 8) * 1.16 + 8)))
            g = max(0, min(255, int((g - 8) * 1.16 + 8)))
            b = max(0, min(255, int((b - 8) * 1.16 + 8)))
            raw_heights.append(h)
            raw_colors.extend((r, g, b))

    filtered_heights = despike_heights(raw_heights, args.out_size, args.out_size)
    smoothed_heights = smooth_heights(filtered_heights, args.out_size, args.out_size)
    base_height = min(smoothed_heights)
    heights = [(h - base_height) * args.z_scale for h in smoothed_heights]

    osm = fetch_osm(source_dir, args.user_agent, args.offline)
    paths = extract_wall_paths(osm, (min_x, max_x, min_y, max_y), args)
    print(f"asset bounds x=[{min_x:.1f},{max_x:.1f}] y=[{min_y:.1f},{max_y:.1f}] base_elev={base_height:.2f}")
    print(f"wall paths={len(paths)} points={sum(len(p) for p in paths)}")

    blob = bytearray()
    blob.extend(b"HTBDALNG")
    blob.extend(struct.pack("<IIII", 2, args.out_size, args.out_size, len(paths)))
    blob.extend(struct.pack("<fffff", min_x, max_x, min_y, max_y, base_height))
    blob.extend(struct.pack(f"<{len(heights)}f", *heights))
    blob.extend(raw_colors)
    for path in paths:
        blob.extend(struct.pack("<I", len(path)))
        for x, y in path:
            blob.extend(struct.pack("<ff", x, y))

    asset_path.write_bytes(blob)
    write_manifest(manifest_path, args)
    print(f"wrote {asset_path} ({asset_path.stat().st_size} bytes)")
    return asset_path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=str(repo_root()), help="Repository root. Defaults to the parent of this script.")
    parser.add_argument("--offline", action="store_true", help="Use existing output/cache only; do not download missing data.")
    parser.add_argument("--force", action="store_true", help="Rebuild even when --offline finds an existing output package.")
    parser.add_argument("--center-lat", type=float, default=DEFAULT_CENTER_LAT)
    parser.add_argument("--center-lon", type=float, default=DEFAULT_CENTER_LON)
    parser.add_argument("--zoom", type=int, default=DEFAULT_ZOOM)
    parser.add_argument("--radius", type=int, default=DEFAULT_RADIUS)
    parser.add_argument("--out-size", type=int, default=DEFAULT_OUT_SIZE)
    parser.add_argument("--xy-scale", type=float, default=DEFAULT_XY_SCALE)
    parser.add_argument("--z-scale", type=float, default=DEFAULT_Z_SCALE)
    parser.add_argument("--user-agent", default="HongTu htengine Badaling asset builder")
    return parser.parse_args()


def main() -> int:
    try:
        build_asset(parse_args())
        return 0
    except Exception as exc:
        print(f"asset generation failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
