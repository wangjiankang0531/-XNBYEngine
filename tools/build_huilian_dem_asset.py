#!/usr/bin/env python3
"""Build a HongTu terrain package from the user-provided Huilian WGS84 DEM/DOM."""

from __future__ import annotations

import argparse
import json
import math
import struct
import sys
from pathlib import Path

from build_binhai_terrain_asset import MAGIC, TiffReader


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


class FloatDemTiff(TiffReader):
    def __init__(self, path: Path) -> None:
        self.path = path
        self.file = path.open("rb")
        header = self.file.read(8)
        if header[:2] == b"II":
            self.endian = "<"
        elif header[:2] == b"MM":
            self.endian = ">"
        else:
            raise RuntimeError(f"not a TIFF file: {path}")
        if struct.unpack(self.endian + "H", header[2:4])[0] != 42:
            raise RuntimeError("BigTIFF is not supported")
        self.tags = self._read_ifd(struct.unpack(self.endian + "I", header[4:8])[0])
        self.width = int(self.value(256)[0])
        self.height = int(self.value(257)[0])
        self.bits = tuple(int(value) for value in self.value(258))
        self.compression = int(self.value(259)[0])
        self.samples = int(self.value(277, [1])[0])
        self.sample_format = int(self.value(339, [3])[0])
        self.planar = int(self.value(284, [1])[0])
        self.has_strips = 273 in self.tags and 279 in self.tags
        self.has_tiles = 324 in self.tags and 325 in self.tags
        self.rows_per_strip = int(self.value(278, [self.height])[0]) if self.has_strips else 0
        self.strip_offsets = [int(value) for value in self.value(273)] if self.has_strips else []
        self.strip_counts = [int(value) for value in self.value(279)] if self.has_strips else []
        if self.compression != 1 or self.bits != (32,) or self.samples != 1 or self.sample_format != 3:
            raise RuntimeError(
                f"unsupported DEM TIFF: compression={self.compression} bits={self.bits} "
                f"samples={self.samples} sample_format={self.sample_format}"
            )
        if not self.has_strips or self.planar != 1:
            raise RuntimeError("DEM TIFF must use contiguous strip storage")

    def read_float_row(self, row: int) -> tuple[float, ...]:
        row = max(0, min(self.height - 1, row))
        strip = row // self.rows_per_strip
        row_in_strip = row - strip * self.rows_per_strip
        row_bytes = self.width * 4
        if row_in_strip * row_bytes + row_bytes > self.strip_counts[strip]:
            raise RuntimeError(f"row {row} exceeds TIFF strip byte count")
        self.file.seek(self.strip_offsets[strip] + row_in_strip * row_bytes)
        payload = self.file.read(row_bytes)
        if len(payload) != row_bytes:
            raise RuntimeError(f"short TIFF row read: {row}")
        return struct.unpack(self.endian + "f" * self.width, payload)


def read_world_file(path: Path) -> tuple[float, float, float, float]:
    values = [float(line.strip()) for line in path.read_text(encoding="ascii").splitlines() if line.strip()]
    if len(values) != 6 or abs(values[1]) > 1.0e-12 or abs(values[2]) > 1.0e-12:
        raise RuntimeError(f"rotated or malformed world file: {path}")
    return values[0], values[3], values[4], values[5]


def raster_bounds(
    width: int, height: int, pixel_x: float, pixel_y: float, origin_lon: float, origin_lat: float
) -> tuple[float, float, float, float]:
    west = origin_lon - pixel_x * 0.5
    east = origin_lon + pixel_x * (width - 0.5)
    north = origin_lat - pixel_y * 0.5
    south = origin_lat + pixel_y * (height - 0.5)
    return west, east, south, north


def read_projection(path: Path, label: str) -> str:
    text = path.read_text(encoding="ascii")
    if "WGS_1984" not in text:
        raise RuntimeError(f"Huilian {label} projection is not WGS84: {path}")
    return text


def load_dom_colors(
    path: Path,
    world: Path,
    projection: Path,
    output_width: int,
    output_height: int,
    dem_bounds: tuple[float, float, float, float],
) -> tuple[bytes, tuple[int, int], tuple[float, float, float, float]]:
    try:
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError(
            "Pillow is required to decode the LZW-compressed Huilian DOM; "
            "run this converter with the Codex workspace Python or install pillow"
        ) from exc

    read_projection(projection, "DOM")
    Image.MAX_IMAGE_PIXELS = None
    with Image.open(path) as image:
        source_size = image.size
        if image.mode != "RGB":
            image = image.convert("RGB")
        pixel_x, pixel_y, origin_lon, origin_lat = read_world_file(world)
        bounds = raster_bounds(image.width, image.height, pixel_x, pixel_y, origin_lon, origin_lat)
        dem_west, dem_east, dem_south, dem_north = dem_bounds
        dem_pixel_x = (dem_east - dem_west) / output_width
        dem_pixel_y = -(dem_north - dem_south) / output_height
        dem_origin_lon = dem_west + dem_pixel_x * 0.5
        dem_origin_lat = dem_north + dem_pixel_y * 0.5
        dem_last_lon = dem_origin_lon + dem_pixel_x * (output_width - 1)
        dem_last_lat = dem_origin_lat + dem_pixel_y * (output_height - 1)
        tolerance = max(abs(pixel_x), abs(pixel_y), 1.0e-10) * 1.5
        if not (
            bounds[0] - tolerance <= dem_origin_lon <= bounds[1] + tolerance
            and bounds[0] - tolerance <= dem_last_lon <= bounds[1] + tolerance
            and bounds[2] - tolerance <= dem_last_lat <= bounds[3] + tolerance
            and bounds[2] - tolerance <= dem_origin_lat <= bounds[3] + tolerance
        ):
            raise RuntimeError(f"Huilian DOM does not cover all DEM sample centers: DOM={bounds} DEM={dem_bounds}")

        # The runtime terrain package stores rows south-to-north, while GeoTIFF rows
        # are north-to-south. Use both world files for affine resampling because the
        # source footprints share an origin but differ slightly at the east/south edges.
        affine = (
            dem_pixel_x / pixel_x,
            0.0,
            (dem_origin_lon - origin_lon) / pixel_x,
            0.0,
            dem_pixel_y / pixel_y,
            (dem_origin_lat - origin_lat) / pixel_y,
        )
        resized = image.transform(
            (output_width, output_height),
            Image.Transform.AFFINE,
            affine,
            resample=Image.Resampling.BICUBIC,
        )
        pixels = resized.load()
        colors = bytearray(output_width * output_height * 3)
        cursor = 0
        for output_y in range(output_height):
            source_y = output_height - 1 - output_y
            for x in range(output_width):
                red, green, blue = pixels[x, source_y]
                colors[cursor : cursor + 3] = bytes((red, green, blue))
                cursor += 3
        return bytes(colors), source_size, bounds


def transform_dom_region(
    image: object,
    source_world: tuple[float, float, float, float],
    target_bounds: tuple[float, float, float, float],
    output_size: tuple[int, int],
) -> object:
    from PIL import Image

    pixel_x, pixel_y, origin_lon, origin_lat = source_world
    west, east, south, north = target_bounds
    output_width, output_height = output_size
    output_pixel_x = (east - west) / output_width
    output_pixel_y = -(north - south) / output_height
    output_origin_lon = west + output_pixel_x * 0.5
    output_origin_lat = north + output_pixel_y * 0.5
    affine = (
        output_pixel_x / pixel_x,
        0.0,
        (output_origin_lon - origin_lon) / pixel_x,
        0.0,
        output_pixel_y / pixel_y,
        (output_origin_lat - origin_lat) / pixel_y,
    )
    return image.transform(
        output_size,
        Image.Transform.AFFINE,
        affine,
        resample=Image.Resampling.BICUBIC,
    )


def build_dom_gpu_textures(
    dom: Path,
    dom_world: Path,
    dom_projection: Path,
    dem_bounds: tuple[float, float, float, float],
    terrain_extent_m: tuple[float, float],
    overview_output: Path,
    detail_output: Path,
    pyramid_output: Path,
    manifest_output: Path,
    detail_scene_bounds: tuple[float, float, float, float],
) -> dict[str, object]:
    try:
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError(
            "Pillow is required to build the Huilian GPU DOM textures; "
            "run this converter with the Codex workspace Python or install pillow"
        ) from exc

    read_projection(dom_projection, "DOM")
    Image.MAX_IMAGE_PIXELS = None
    source_world = read_world_file(dom_world)
    with Image.open(dom) as opened:
        image = opened.convert("RGB") if opened.mode != "RGB" else opened.copy()

    source_bounds = raster_bounds(image.width, image.height, *source_world)
    dem_west, dem_east, dem_south, dem_north = dem_bounds
    terrain_width_m, terrain_height_m = terrain_extent_m
    overview_width = 2048
    overview_height = max(1, round(overview_width * terrain_height_m / terrain_width_m))
    overview = transform_dom_region(image, source_world, dem_bounds, (overview_width, overview_height))

    scene_min_x = -terrain_width_m * 0.5
    scene_min_y = -terrain_height_m * 0.5
    detail_min_x, detail_min_y, detail_max_x, detail_max_y = detail_scene_bounds
    detail_uv_bounds = (
        (detail_min_x - scene_min_x) / terrain_width_m,
        (detail_min_y - scene_min_y) / terrain_height_m,
        (detail_max_x - scene_min_x) / terrain_width_m,
        (detail_max_y - scene_min_y) / terrain_height_m,
    )
    detail_geo_bounds = (
        dem_west + detail_uv_bounds[0] * (dem_east - dem_west),
        dem_west + detail_uv_bounds[2] * (dem_east - dem_west),
        dem_south + detail_uv_bounds[1] * (dem_north - dem_south),
        dem_south + detail_uv_bounds[3] * (dem_north - dem_south),
    )
    detail_size = (1024, 1024)
    detail = transform_dom_region(image, source_world, detail_geo_bounds, detail_size)

    overview_output.parent.mkdir(parents=True, exist_ok=True)
    detail_output.parent.mkdir(parents=True, exist_ok=True)
    pyramid_output.mkdir(parents=True, exist_ok=True)
    manifest_output.parent.mkdir(parents=True, exist_ok=True)
    overview.save(overview_output, format="PNG", optimize=True)
    detail.save(detail_output, format="PNG", optimize=True)

    page_size_px = 512
    pyramid_levels: list[dict[str, object]] = []
    total_page_count = 0
    total_page_bytes = 0
    for lod, resolution_m_per_texel in enumerate((0.5, 1.0, 2.0)):
        page_size_m = page_size_px * resolution_m_per_texel
        page_grid = (
            math.ceil(terrain_width_m / page_size_m),
            math.ceil(terrain_height_m / page_size_m),
        )
        level_scene_bounds = (
            scene_min_x,
            scene_min_y,
            scene_min_x + page_grid[0] * page_size_m,
            scene_min_y + page_grid[1] * page_size_m,
        )
        level_uv_bounds = (
            0.0,
            0.0,
            (level_scene_bounds[2] - scene_min_x) / terrain_width_m,
            (level_scene_bounds[3] - scene_min_y) / terrain_height_m,
        )
        level_dir = pyramid_output / f"lod{lod}"
        level_dir.mkdir(parents=True, exist_ok=True)
        pages: list[dict[str, object]] = []
        level_bytes = 0
        for page_y in range(page_grid[1]):
            for page_x in range(page_grid[0]):
                scene_west = scene_min_x + page_x * page_size_m
                scene_south = scene_min_y + page_y * page_size_m
                scene_east = scene_west + page_size_m
                scene_north = scene_south + page_size_m
                page_uv_bounds = (
                    (scene_west - scene_min_x) / terrain_width_m,
                    (scene_south - scene_min_y) / terrain_height_m,
                    (scene_east - scene_min_x) / terrain_width_m,
                    (scene_north - scene_min_y) / terrain_height_m,
                )
                page_geo_bounds = (
                    dem_west + page_uv_bounds[0] * (dem_east - dem_west),
                    dem_west + page_uv_bounds[2] * (dem_east - dem_west),
                    dem_south + page_uv_bounds[1] * (dem_north - dem_south),
                    dem_south + page_uv_bounds[3] * (dem_north - dem_south),
                )
                page = transform_dom_region(image, source_world, page_geo_bounds, (page_size_px, page_size_px))
                page_path = level_dir / f"l{lod}_x{page_x:02d}_y{page_y:02d}.jpg"
                page.save(page_path, format="JPEG", quality=92, subsampling=0)
                page_bytes = page_path.stat().st_size
                level_bytes += page_bytes
                pages.append(
                    {
                        "x": page_x,
                        "y": page_y,
                        "path": page_path.relative_to(repo_root()).as_posix(),
                        "bytes": page_bytes,
                        "scene_bounds_m": [scene_west, scene_south, scene_east, scene_north],
                        "terrain_uv_bounds": list(page_uv_bounds),
                        "wgs84_bounds": [
                            page_geo_bounds[0],
                            page_geo_bounds[2],
                            page_geo_bounds[1],
                            page_geo_bounds[3],
                        ],
                    }
                )
                if len(pages) % 100 == 0 or len(pages) == page_grid[0] * page_grid[1]:
                    print(
                        f"DOM pyramid lod={lod} pages={len(pages)}/{page_grid[0] * page_grid[1]}",
                        flush=True,
                    )
        level_page_count = page_grid[0] * page_grid[1]
        total_page_count += level_page_count
        total_page_bytes += level_bytes
        pyramid_levels.append(
            {
                "lod": lod,
                "grid": list(page_grid),
                "page_count": level_page_count,
                "page_grid_px": [page_size_px, page_size_px],
                "page_size_m": [page_size_m, page_size_m],
                "resolution_m_per_texel": [resolution_m_per_texel, resolution_m_per_texel],
                "scene_bounds_m": list(level_scene_bounds),
                "terrain_uv_bounds": list(level_uv_bounds),
                "page_bytes": level_bytes,
                "pages": pages,
            }
        )

    source_pixel_x, source_pixel_y, source_origin_lon, source_origin_lat = source_world
    detail_source_pixels = (
        (detail_geo_bounds[0] - source_origin_lon) / source_pixel_x,
        (detail_geo_bounds[3] - source_origin_lat) / source_pixel_y,
        (detail_geo_bounds[1] - source_origin_lon) / source_pixel_x,
        (detail_geo_bounds[2] - source_origin_lat) / source_pixel_y,
    )
    resident_atlas_width = page_size_px * 2
    resident_atlas_height = page_size_px * 2
    resident_mip_levels = int(math.floor(math.log2(max(resident_atlas_width, resident_atlas_height)))) + 1
    resident_base_level_gpu_bytes = resident_atlas_width * resident_atlas_height * 4
    resident_gpu_bytes = sum(
        max(1, resident_atlas_width >> mip) * max(1, resident_atlas_height >> mip) * 4
        for mip in range(resident_mip_levels)
    )
    manifest: dict[str, object] = {
        "schema": "hongtu.huilian_dom_gpu_textures.v7",
        "source_dom": dom.relative_to(repo_root()).as_posix(),
        "source_grid": [image.width, image.height],
        "source_resolution_m": 0.5,
        "source_wgs84_bounds": list(source_bounds),
        "dem_wgs84_bounds": list(dem_bounds),
        "terrain_extent_m": [terrain_width_m, terrain_height_m],
        "registration": "wgs84_world_file_pixel_center_affine",
        "runtime_sampling": "overview_plus_async_full_coverage_lod_pyramid_four_slot_priority_prefetch_crossfade",
        "row_order": "north_to_south_shader_flips_v",
        "overview": {
            "path": overview_output.relative_to(repo_root()).as_posix(),
            "grid": [overview_width, overview_height],
            "coverage": "full_dem_bounds",
            "resolution_m_per_texel": [terrain_width_m / overview_width, terrain_height_m / overview_height],
        },
        "detail": {
            "path": detail_output.relative_to(repo_root()).as_posix(),
            "grid": list(detail_size),
            "scene_bounds_m": list(detail_scene_bounds),
            "terrain_uv_bounds": list(detail_uv_bounds),
            "wgs84_bounds": list(detail_geo_bounds),
            "source_pixel_bounds": list(detail_source_pixels),
            "resolution_m_per_texel": [
                (detail_max_x - detail_min_x) / detail_size[0],
                (detail_max_y - detail_min_y) / detail_size[1],
            ],
        },
        "tile_pyramid": {
            "directory": pyramid_output.relative_to(repo_root()).as_posix(),
            "coverage": "full_dem_bounds_with_page_aligned_east_north_padding",
            "page_format": "jpeg_quality_92_444",
            "page_grid_px": [page_size_px, page_size_px],
            "level_count": len(pyramid_levels),
            "total_page_count": total_page_count,
            "total_page_bytes": total_page_bytes,
            "resident_working_set_pages": [2, 2],
            "resident_atlas_grid_px": [resident_atlas_width, resident_atlas_height],
            "resident_slot_count": 4,
            "resident_base_level_gpu_bytes_per_slot": resident_base_level_gpu_bytes,
            "resident_gpu_bytes_per_slot": resident_gpu_bytes,
            "resident_gpu_budget_bytes": resident_gpu_bytes * 4,
            "resident_mip_levels": resident_mip_levels,
            "sampler": "trilinear_16x_anisotropic_clamp_to_edge",
            "selection": "camera_xyz_height_lod_hysteresis_nearest_2x2_clamped_to_full_coverage",
            "lod_height_thresholds_m": [250.0, 700.0],
            "lod_hysteresis_m": [50.0, 75.0],
            "cpu_loading": "std_async_stb_jpeg_decode_cpu_box_mip_chain",
            "request_policy": "foreground_preempts_directional_prefetch_epoch_cancel_stale_result_discard",
            "request_scheduler": "single_worker_foreground_preempts_directional_prefetch",
            "prefetch_policy": "camera_velocity_same_lod_one_atlas_lookahead",
            "cooperative_cancellation": "epoch_token_page_mip_delay_checkpoints",
            "cache_policy": "priority_aware_lru_hash_page_table",
            "page_table": "cpu_hash_residency_to_gpu_atlas_slot",
            "eviction_policy": "prefetched_first_then_true_lru_excluding_current_and_active_transition",
            "stream_telemetry": "resident_upload_cache_frame_time_driver_budget_endurance_accounting",
            "driver_memory_budget": "optional_vk_ext_memory_budget_device_local_heaps",
            "driver_resource_lifetime": "vulkan_texture_raii_vma_image_view_release",
            "endurance_probe_seconds": 60,
            "driver_peak_growth_allowance_bytes": resident_gpu_bytes * 16,
            "driver_plateau_growth_allowance_bytes": resident_gpu_bytes * 8,
            "cache_probe_station_count": 12,
            "crossfade_seconds": 0.35,
            "transition_metadata_grid_px": [3, 1],
            "gpu_commit": "main_thread_fence_safe_descriptor_swap",
            "levels": pyramid_levels,
        },
    }
    manifest_output.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return manifest


def mix(a: tuple[float, float, float], b: tuple[float, float, float], t: float) -> tuple[float, float, float]:
    t = max(0.0, min(1.0, t))
    return tuple(a[index] * (1.0 - t) + b[index] * t for index in range(3))


def terrain_color(height: float, x: int, y: int) -> tuple[int, int, int]:
    if height <= 0.0:
        color = mix((35.0, 72.0, 88.0), (70.0, 112.0, 126.0), (height + 20.0) / 20.0)
    elif height < 30.0:
        color = mix((96.0, 111.0, 72.0), (76.0, 116.0, 61.0), height / 30.0)
    elif height < 150.0:
        color = mix((76.0, 116.0, 61.0), (111.0, 111.0, 82.0), (height - 30.0) / 120.0)
    else:
        color = mix((111.0, 111.0, 82.0), (151.0, 151.0, 142.0), (height - 150.0) / 270.0)
    grain = (((x * 73856093) ^ (y * 19349663)) & 31) / 31.0 - 0.5
    return tuple(max(0, min(255, int(channel + grain * 7.0))) for channel in color)


def build(args: argparse.Namespace) -> Path:
    source = Path(args.dem)
    if not source.is_absolute():
        source = repo_root() / source
    world = Path(args.world_file) if args.world_file else source.with_suffix(".tfw")
    projection = Path(args.projection) if args.projection else source.with_suffix(".prj")
    if not world.is_absolute():
        world = repo_root() / world
    if not projection.is_absolute():
        projection = repo_root() / projection
    read_projection(projection, "DEM")

    dom = Path(args.dom)
    if not dom.is_absolute():
        dom = repo_root() / dom
    dom_world = Path(args.dom_world_file) if args.dom_world_file else dom.with_suffix(".tfw")
    dom_projection = Path(args.dom_projection) if args.dom_projection else dom.with_suffix(".prj")
    if not dom_world.is_absolute():
        dom_world = repo_root() / dom_world
    if not dom_projection.is_absolute():
        dom_projection = repo_root() / dom_projection

    reader = FloatDemTiff(source)
    try:
        pixel_x, pixel_y, origin_lon, origin_lat = read_world_file(world)
        west, east, south, north = raster_bounds(
            reader.width, reader.height, pixel_x, pixel_y, origin_lon, origin_lat
        )
        center_lat = (north + south) * 0.5
        width_m = abs(east - west) * 111320.0 * math.cos(math.radians(center_lat))
        height_m = abs(north - south) * 110574.0

        heights: list[float] = []
        for output_y in range(reader.height):
            source_y = reader.height - 1 - output_y
            row = reader.read_float_row(source_y)
            for value in row:
                height = float(value)
                if not math.isfinite(height) or height < -500.0 or height > 9000.0:
                    height = 0.0
                heights.append(height)

        dom_colors, dom_size, dom_bounds = load_dom_colors(
            dom,
            dom_world,
            dom_projection,
            reader.width,
            reader.height,
            (west, east, south, north),
        )

        output = Path(args.output)
        if not output.is_absolute():
            output = repo_root() / output
        output.parent.mkdir(parents=True, exist_ok=True)
        half_width = width_m * 0.5
        half_height = height_m * 0.5
        payload = bytearray(MAGIC)
        payload.extend(struct.pack("<IIII", 3, reader.width, reader.height, 0))
        payload.extend(struct.pack("<fffff", -half_width, half_width, -half_height, half_height, min(heights)))
        payload.extend(struct.pack(f"<{len(heights)}f", *heights))
        payload.extend(dom_colors)
        output.write_bytes(payload)

        manifest = output.with_suffix(".txt")
        overlap_width = min(east, dom_bounds[1]) - max(west, dom_bounds[0])
        overlap_height = min(north, dom_bounds[3]) - max(south, dom_bounds[2])
        overlap_ratio = max(0.0, overlap_width) * max(0.0, overlap_height) / ((east - west) * (north - south))
        manifest.write_text(
            "HongTu Huilian WGS84 DEM terrain package\n"
            f"Source DEM: {source}\n"
            f"DEM world file: {world}\n"
            f"Source DOM: {dom}\n"
            f"DOM world file: {dom_world}\n"
            f"Projection: WGS84 geographic longitude/latitude\n"
            f"WGS84 bounds: west={west:.12f} east={east:.12f} south={south:.12f} north={north:.12f}\n"
            f"DOM WGS84 bounds: west={dom_bounds[0]:.12f} east={dom_bounds[1]:.12f} "
            f"south={dom_bounds[2]:.12f} north={dom_bounds[3]:.12f}\n"
            f"DOM/DEM footprint overlap: {overlap_ratio * 100.0:.6f}%; all DEM sample centers covered\n"
            f"Approximate extent: {width_m:.3f}m x {height_m:.3f}m\n"
            f"Grid: {reader.width}x{reader.height}; nominal source resolution: 30m\n"
            f"DOM source grid: {dom_size[0]}x{dom_size[1]}; nominal source resolution: 0.5m\n"
            f"Elevation range: {min(heights):.6f}..{max(heights):.6f}m\n"
            "Color policy: WGS84 world-file affine bicubic resampling onto the DEM runtime grid.\n",
            encoding="utf-8",
        )

        overview_output = Path(args.dom_overview_output)
        detail_output = Path(args.dom_detail_output)
        pyramid_output = Path(args.dom_pyramid_output)
        gpu_manifest_output = Path(args.dom_gpu_manifest)
        if not overview_output.is_absolute():
            overview_output = repo_root() / overview_output
        if not detail_output.is_absolute():
            detail_output = repo_root() / detail_output
        if not pyramid_output.is_absolute():
            pyramid_output = repo_root() / pyramid_output
        if not gpu_manifest_output.is_absolute():
            gpu_manifest_output = repo_root() / gpu_manifest_output
        detail_scene_bounds = tuple(float(value) for value in args.dom_detail_scene_bounds)
        gpu_manifest = build_dom_gpu_textures(
            dom,
            dom_world,
            dom_projection,
            (west, east, south, north),
            (width_m, height_m),
            overview_output,
            detail_output,
            pyramid_output,
            gpu_manifest_output,
            detail_scene_bounds,
        )
        print(
            f"wrote {output} grid={reader.width}x{reader.height} extent={width_m:.1f}x{height_m:.1f}m "
            f"height={min(heights):.2f}..{max(heights):.2f}m dom={dom_size[0]}x{dom_size[1]} "
            f"bytes={output.stat().st_size}"
        )
        overview_grid = gpu_manifest["overview"]["grid"]
        detail_grid = gpu_manifest["detail"]["grid"]
        print(
            f"wrote GPU DOM overview={overview_output} {overview_grid[0]}x{overview_grid[1]} "
            f"detail={detail_output} {detail_grid[0]}x{detail_grid[1]} "
            f"levels={gpu_manifest['tile_pyramid']['level_count']} "
            f"pages={gpu_manifest['tile_pyramid']['total_page_count']} manifest={gpu_manifest_output}"
        )
        return output
    finally:
        reader.close()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dem", default="assets/惠炼/DEM-DOM/hero_DEM_30M.tif")
    parser.add_argument("--world-file", default="")
    parser.add_argument("--projection", default="")
    parser.add_argument("--dom", default="assets/惠炼/DEM-DOM/hero_0.5M.tif")
    parser.add_argument("--dom-world-file", default="")
    parser.add_argument("--dom-projection", default="")
    parser.add_argument("--output", default="assets/showcase/runtime/huilian_dem_30m.bin")
    parser.add_argument("--dom-overview-output", default="assets/showcase/runtime/huilian_dom_overview_2048.png")
    parser.add_argument("--dom-detail-output", default="assets/showcase/runtime/huilian_dom_detail_valve_1024.png")
    parser.add_argument("--dom-pyramid-output", default="assets/showcase/runtime/huilian_dom_pyramid")
    parser.add_argument("--dom-gpu-manifest", default="assets/showcase/runtime/huilian_dom_gpu_manifest.json")
    parser.add_argument(
        "--dom-detail-scene-bounds",
        nargs=4,
        type=float,
        metavar=("MIN_X", "MIN_Y", "MAX_X", "MAX_Y"),
        default=(524.0, 264.0, 1036.0, 776.0),
        help="scene-space detail window; default is a 512 m square around the Huilian valve inspection point",
    )
    return parser.parse_args()


def main() -> int:
    try:
        build(parse_args())
        return 0
    except Exception as exc:
        print(f"[build_huilian_dem_asset] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
