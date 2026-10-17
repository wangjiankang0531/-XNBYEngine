#!/usr/bin/env python3
"""Build a HongTu showcase terrain package from the 20260729 Binhai GIS assets.

The source directory contains large uncompressed RGB GeoTIFF imagery plus a KML
boundary. This converter intentionally avoids GDAL/Pillow so it can run in the
minimal Codex workspace Python environment.
"""

from __future__ import annotations

import argparse
import math
import re
import struct
import sys
from pathlib import Path


MAGIC = b"HTBDALNG"
WEB_MERCATOR_RADIUS = 6378137.0


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    raise RuntimeError(message)


def smoothstep(edge0: float, edge1: float, x: float) -> float:
    if edge0 == edge1:
        return 1.0 if x >= edge1 else 0.0
    t = max(0.0, min(1.0, (x - edge0) / (edge1 - edge0)))
    return t * t * (3.0 - 2.0 * t)


def hash01(ix: int, iy: int) -> float:
    n = (ix * 374761393 + iy * 668265263) & 0xFFFFFFFF
    n = (n ^ (n >> 13)) * 1274126177 & 0xFFFFFFFF
    n ^= n >> 16
    return (n & 0xFFFFFF) / float(0x1000000)


def value_noise(x: float, y: float) -> float:
    x0 = math.floor(x)
    y0 = math.floor(y)
    tx = x - x0
    ty = y - y0
    sx = tx * tx * (3.0 - 2.0 * tx)
    sy = ty * ty * (3.0 - 2.0 * ty)
    a = hash01(x0, y0)
    b = hash01(x0 + 1, y0)
    c = hash01(x0, y0 + 1)
    d = hash01(x0 + 1, y0 + 1)
    return (a * (1.0 - sx) + b * sx) * (1.0 - sy) + (c * (1.0 - sx) + d * sx) * sy


def fbm(x: float, y: float) -> float:
    total = 0.0
    amp = 0.5
    freq = 1.0
    norm = 0.0
    for _ in range(4):
        total += value_noise(x * freq, y * freq) * amp
        norm += amp
        amp *= 0.5
        freq *= 2.03
    return total / max(norm, 0.001)


class TiffReader:
    TYPE_SIZES = {1: 1, 2: 1, 3: 2, 4: 4, 5: 8, 11: 4, 12: 8, 16: 8, 17: 8, 18: 8}

    def __init__(self, path: Path) -> None:
        self.path = path
        self.file = path.open("rb")
        header = self.file.read(8)
        if header[:2] == b"II":
            self.endian = "<"
        elif header[:2] == b"MM":
            self.endian = ">"
        else:
            fail(f"not a TIFF file: {path}")
        magic = struct.unpack(self.endian + "H", header[2:4])[0]
        if magic != 42:
            fail(f"unsupported TIFF magic {magic}; BigTIFF is not expected here")
        ifd_offset = struct.unpack(self.endian + "I", header[4:8])[0]
        self.tags = self._read_ifd(ifd_offset)
        self.width = int(self.value(256)[0])
        self.height = int(self.value(257)[0])
        self.bits = tuple(int(v) for v in self.value(258))
        self.compression = int(self.value(259)[0])
        self.samples = int(self.value(277)[0])
        self.planar = int(self.value(284, [1])[0])
        self.has_strips = 273 in self.tags and 279 in self.tags
        self.has_tiles = 324 in self.tags and 325 in self.tags
        self.rows_per_strip = int(self.value(278, [self.height])[0]) if self.has_strips else 0
        self.strip_offsets = [int(v) for v in self.value(273)] if self.has_strips else []
        self.strip_counts = [int(v) for v in self.value(279)] if self.has_strips else []
        self.tile_width = int(self.value(322, [0])[0]) if self.has_tiles else 0
        self.tile_height = int(self.value(323, [0])[0]) if self.has_tiles else 0
        if self.compression != 1:
            fail(f"compressed TIFF is unsupported: compression={self.compression}")
        if self.bits != (8, 8, 8) or self.samples != 3 or self.planar != 1:
            fail(f"unsupported TIFF layout: bits={self.bits} samples={self.samples} planar={self.planar}")
        if not self.has_strips:
            storage = "tiled" if self.has_tiles else "unknown"
            fail(f"{storage} TIFF storage is not used by this converter; use the strip GeoTIFF sibling")

    def close(self) -> None:
        self.file.close()

    def _read_ifd(self, offset: int) -> dict[int, tuple[int, int, int, bytes]]:
        self.file.seek(offset)
        count = struct.unpack(self.endian + "H", self.file.read(2))[0]
        tags: dict[int, tuple[int, int, int, bytes]] = {}
        for _ in range(count):
            entry = self.file.read(12)
            tag, typ, cnt, val = struct.unpack(self.endian + "HHII", entry)
            tags[tag] = (typ, cnt, val, entry[8:12])
        return tags

    def value(self, tag: int, default: list[int] | None = None):
        if tag not in self.tags:
            if default is None:
                fail(f"missing TIFF tag {tag} in {self.path}")
            return default
        typ, cnt, val, inline = self.tags[tag]
        item_size = self.TYPE_SIZES.get(typ)
        if item_size is None:
            fail(f"unsupported TIFF tag type {typ} for tag {tag}")
        total = item_size * cnt
        if total <= 4:
            data = inline[:total]
        else:
            self.file.seek(val)
            data = self.file.read(total)
        fmt_map = {1: "B", 2: "c", 3: "H", 4: "I", 11: "f", 12: "d", 16: "Q", 17: "Q", 18: "Q"}
        if typ == 2:
            return data.split(b"\0")[0].decode("utf-8", "replace")
        fmt = fmt_map.get(typ)
        if fmt is None:
            return (typ, cnt, val)
        return struct.unpack(self.endian + fmt * cnt, data)

    def read_row(self, row: int) -> bytes:
        row = max(0, min(self.height - 1, row))
        strip = row // self.rows_per_strip
        row_in_strip = row - strip * self.rows_per_strip
        row_bytes = self.width * self.samples
        offset = self.strip_offsets[strip] + row_in_strip * row_bytes
        if row_in_strip * row_bytes + row_bytes > self.strip_counts[strip]:
            fail(f"row {row} exceeds strip byte count in {self.path}")
        self.file.seek(offset)
        data = self.file.read(row_bytes)
        if len(data) != row_bytes:
            fail(f"short read for row {row} in {self.path}")
        return data

    def geokey_epsg(self) -> int | None:
        if 34735 not in self.tags:
            return None
        keys = self.value(34735)
        if len(keys) < 4:
            return None
        key_count = int(keys[3])
        for i in range(key_count):
            base = 4 + i * 4
            if base + 3 >= len(keys):
                break
            key_id, tiff_tag, count, value = (int(keys[base]), int(keys[base + 1]), int(keys[base + 2]), int(keys[base + 3]))
            if key_id == 3072 and tiff_tag == 0 and count == 1:
                return value
        return None

    def pixel_scale(self) -> tuple[float, float, float] | None:
        if 33550 not in self.tags:
            return None
        vals = self.value(33550)
        return float(vals[0]), float(vals[1]), float(vals[2] if len(vals) > 2 else 0.0)

    def tiepoint(self) -> tuple[float, float, float, float, float, float] | None:
        if 33922 not in self.tags:
            return None
        vals = self.value(33922)
        if len(vals) < 6:
            return None
        return tuple(float(v) for v in vals[:6])  # type: ignore[return-value]


def find_source_tiff(root: Path) -> Path:
    candidates = sorted(root.rglob("*.tif"), key=lambda p: p.stat().st_size, reverse=True)
    best: tuple[int, Path] | None = None
    for path in candidates:
        try:
            reader = TiffReader(path)
        except RuntimeError as exc:
            if "TIFF storage" in str(exc):
                continue
            raise
        try:
            epsg = reader.geokey_epsg()
            score = (3 if reader.has_strips else 0) * 1_000_000_000
            score += (2 if epsg == 3857 else 1) * 100_000_000
            score += reader.width * reader.height
            if best is None or score > best[0]:
                best = (score, path)
        finally:
            reader.close()
    if best is None:
        fail(f"no GeoTIFF found below {root}")
    return best[1]


def lonlat_to_web_mercator(lon: float, lat: float) -> tuple[float, float]:
    lat = max(-85.05112878, min(85.05112878, lat))
    x = WEB_MERCATOR_RADIUS * math.radians(lon)
    y = WEB_MERCATOR_RADIUS * math.log(math.tan(math.pi * 0.25 + math.radians(lat) * 0.5))
    return x, y


def parse_kml_boundary(path: Path, bounds_m: tuple[float, float, float, float]) -> list[tuple[float, float]]:
    text = path.read_text(encoding="gb2312", errors="replace")
    blocks = re.findall(r"<coordinates>(.*?)</coordinates>", text, flags=re.S)
    if not blocks:
        return []
    coords: list[tuple[float, float]] = []
    min_x, max_x, min_y, max_y = bounds_m
    cx = (min_x + max_x) * 0.5
    cy = (min_y + max_y) * 0.5
    sx = 3200.0 / max(max_x - min_x, 1.0)
    sy = 2400.0 / max(max_y - min_y, 1.0)
    scale = min(sx, sy)
    for token in blocks[0].split():
        parts = token.split(",")
        if len(parts) < 2:
            continue
        mx, my = lonlat_to_web_mercator(float(parts[0]), float(parts[1]))
        coords.append(((mx - cx) * scale, (my - cy) * scale))
    return coords


def sample_rgb(row: bytes, width: int, x: float) -> tuple[int, int, int]:
    ix = max(0, min(width - 1, int(round(x))))
    offset = ix * 3
    return row[offset], row[offset + 1], row[offset + 2]


def terrain_height_from_color(r: int, g: int, b: int, xn: float, yn: float) -> float:
    rf = r / 255.0
    gf = g / 255.0
    bf = b / 255.0
    luma = 0.2126 * rf + 0.7152 * gf + 0.0722 * bf
    water = smoothstep(0.05, 0.22, bf - max(rf, gf)) * smoothstep(0.18, 0.42, bf)
    vegetation = smoothstep(0.03, 0.18, gf - max(rf, bf)) * smoothstep(0.16, 0.45, gf)
    urban = smoothstep(0.45, 0.82, luma) * (1.0 - smoothstep(0.24, 0.50, max(rf, gf, bf) - min(rf, gf, bf)))
    coast_noise = fbm(xn * 8.0 + 11.0, yn * 8.0 - 4.0)
    fine = fbm(xn * 34.0 - 3.0, yn * 34.0 + 9.0)
    basin = 1.0 - smoothstep(0.46, 0.78, math.hypot(xn - 0.50, yn - 0.52))
    height = 2.5 + vegetation * 5.2 + urban * 3.0 + luma * 2.8
    height += (coast_noise - 0.5) * 9.5 + (fine - 0.5) * 2.2 + basin * 2.4
    height -= water * 3.8
    return height


def build_asset(args: argparse.Namespace) -> Path:
    root = Path(args.source_root)
    if not root.is_absolute():
        root = repo_root() / root
    if args.tiff:
        tiff_path = Path(args.tiff)
        if not tiff_path.is_absolute():
            tiff_path = repo_root() / tiff_path
    else:
        tiff_path = find_source_tiff(root)
    kml_files = sorted(root.glob("*.kml"))
    kml_path = kml_files[0] if kml_files else None

    reader = TiffReader(tiff_path)
    try:
        scale = reader.pixel_scale()
        tie = reader.tiepoint()
        if scale is None or tie is None:
            fail("GeoTIFF has no ModelPixelScale/ModelTiepoint tags")
        px, py, _ = scale
        _, _, _, tie_x, tie_y, _ = tie
        epsg = reader.geokey_epsg()
        if epsg == 3857 or abs(tie_x) > 1000.0:
            min_x = tie_x
            max_y = tie_y
            max_x = tie_x + px * float(reader.width - 1)
            min_y = tie_y - py * float(reader.height - 1)
            source_bounds = (min_x, max_x, min_y, max_y)
            source_projection = "EPSG:3857"
        else:
            lon_w = tie_x
            lat_n = tie_y
            lon_e = tie_x + px * float(reader.width - 1)
            lat_s = tie_y - py * float(reader.height - 1)
            min_x, min_y = lonlat_to_web_mercator(lon_w, lat_s)
            max_x, max_y = lonlat_to_web_mercator(lon_e, lat_n)
            source_bounds = (min_x, max_x, min_y, max_y)
            source_projection = "geographic_lonlat"

        out_size = args.out_size
        pixel_count = out_size * out_size
        heights = [0.0] * pixel_count
        colors = bytearray(pixel_count * 3)
        min_h = 1e9
        max_h = -1e9
        for oy in range(out_size):
            yn = oy / max(out_size - 1, 1)
            src_y = (1.0 - yn) * float(reader.height - 1)
            row = reader.read_row(int(round(src_y)))
            if oy % max(1, out_size // 16) == 0:
                print(f"sampling row {oy + 1}/{out_size}")
            for ox in range(out_size):
                xn = ox / max(out_size - 1, 1)
                r, g, b = sample_rgb(row, reader.width, xn * float(reader.width - 1))
                height = terrain_height_from_color(r, g, b, xn, yn)
                idx = oy * out_size + ox
                heights[idx] = height
                min_h = min(min_h, height)
                max_h = max(max_h, height)
                ci = idx * 3
                colors[ci + 0] = max(0, min(255, int((r - 8) * 1.08 + 7)))
                colors[ci + 1] = max(0, min(255, int((g - 8) * 1.08 + 7)))
                colors[ci + 2] = max(0, min(255, int((b - 8) * 1.08 + 7)))

        # Rebase to a small positive coastal relief range; the runtime applies its
        # own local erosion and central cut/fill shaping.
        base_height = min_h
        heights = [(h - base_height) * args.z_scale for h in heights]
        paths = [parse_kml_boundary(kml_path, source_bounds)] if kml_path else []
        paths = [p for p in paths if len(p) >= 2]

        out = Path(args.output)
        if not out.is_absolute():
            out = repo_root() / out
        out.parent.mkdir(parents=True, exist_ok=True)

        blob = bytearray()
        blob.extend(MAGIC)
        blob.extend(struct.pack("<IIII", 3, out_size, out_size, len(paths)))
        blob.extend(struct.pack("<fffff", -1600.0, 1600.0, -1200.0, 1200.0, base_height))
        blob.extend(struct.pack(f"<{len(heights)}f", *heights))
        blob.extend(colors)
        for path in paths:
            blob.extend(struct.pack("<I", len(path)))
            for x, y in path:
                blob.extend(struct.pack("<ff", x, y))
        out.write_bytes(blob)

        manifest = out.with_suffix(".txt")
        manifest.write_text(
            "HongTu Binhai showcase terrain package\n"
            f"Source root: {root}\n"
            f"GeoTIFF: {tiff_path}\n"
            f"KML: {kml_path if kml_path else 'none'}\n"
            "Type: DOM imagery with synthetic low-relief coastal height field\n"
            f"Projection: {source_projection}\n"
            f"Source bounds EPSG:3857: x=[{min_x:.3f},{max_x:.3f}] y=[{min_y:.3f},{max_y:.3f}]\n"
            f"Output grid: {out_size}x{out_size}\n"
            f"Height range: {min(heights):.3f}..{max(heights):.3f} m\n"
            f"KML path count: {len(paths)}\n",
            encoding="utf-8",
        )
        print(
            f"wrote {out} grid={out_size}x{out_size} height_range={max(heights)-min(heights):.2f} "
            f"paths={len(paths)} size={out.stat().st_size}"
        )
        return out
    finally:
        reader.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", default="assets/20260729")
    parser.add_argument("--tiff", default="")
    parser.add_argument("--output", default="assets/20260729/binhai_terrain.bin")
    parser.add_argument("--out-size", type=int, default=1024)
    parser.add_argument("--z-scale", type=float, default=1.35)
    return parser.parse_args()


def main() -> int:
    try:
        build_asset(parse_args())
        return 0
    except Exception as exc:
        print(f"[build_binhai_terrain_asset] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
