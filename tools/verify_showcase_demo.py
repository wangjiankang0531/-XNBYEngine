#!/usr/bin/env python3
"""Render and validate showcase_demo screenshots and scene stats."""

from __future__ import annotations

import argparse
import json
import math
import os
import sys
from pathlib import Path

import verify_badaling_demo as image_verify
from run_showcase_demo import run_showcase_process, suppress_windows_error_dialogs


PRESETS = (
    "overview",
    "terrain",
    "fusion",
    "gis",
    "models",
    "huilian",
    "materials",
    "process",
    "disassembly",
    "lighting",
    "particles",
    "weather",
    "safety",
    "sprinkler",
    "routes",
    "warehouse",
    "drill",
    "performance",
)


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def descending_log_lod_weight(altitude_m: float, start_altitude_m: float, full_altitude_m: float) -> float:
    altitude = max(altitude_m, 1.0)
    linear = (math.log(start_altitude_m) - math.log(altitude)) / (
        math.log(start_altitude_m) - math.log(full_altitude_m)
    )
    linear = min(max(linear, 0.0), 1.0)
    return linear * linear * (3.0 - 2.0 * linear)


def validate_stats(path: Path, preset: str, args: argparse.Namespace) -> dict:
    if not path.exists():
        raise RuntimeError(f"stats missing: {path}")
    stats = json.loads(path.read_text(encoding="utf-8"))
    failures: list[str] = []
    expected_fountain_mode = args.fountain_mode or "all"
    expected_fountain_counts = {
        "all": {"layers": 4, "jets": 38, "mist": 32, "ripples": 10, "canopies": 2, "wind_vectors": 8},
        "jets": {"layers": 1, "jets": 38, "mist": 0, "ripples": 0, "canopies": 2, "wind_vectors": 0},
        "mist": {"layers": 1, "jets": 0, "mist": 32, "ripples": 0, "canopies": 2, "wind_vectors": 0},
        "ripples": {"layers": 1, "jets": 0, "mist": 0, "ripples": 10, "canopies": 0, "wind_vectors": 0},
        "wind": {"layers": 1, "jets": 38, "mist": 32, "ripples": 0, "canopies": 2, "wind_vectors": 8},
    }[expected_fountain_mode]

    require(stats.get("scene") == "industrial_park", f"scene mismatch: {stats.get('scene')}", failures)
    require(stats.get("preset") == preset, f"preset mismatch: {stats.get('preset')} != {preset}", failures)
    require(stats.get("scene_config_loaded") is True, "scene JSON was not loaded", failures)
    require(int(stats.get("vertices", 0)) >= 55000, f"vertex count too low: {stats.get('vertices')}", failures)
    require(int(stats.get("indices", 0)) >= 140000, f"index count too low: {stats.get('indices')}", failures)
    require(stats.get("full_park_scene_assembly") is True, "full park scene assembly was bypassed", failures)
    require(
        stats.get("scene_assembly_guard_pass")
        == "terrain_roads_facilities_runtime_gltf_effects_no_early_return",
        "full scene early-return guard missing",
        failures,
    )
    require(
        int(stats.get("vertices", 0)) >= int(stats.get("scene_assembly_minimum_vertices", 1_000_000)),
        "full scene vertex budget not reached",
        failures,
    )
    require(
        int(stats.get("indices", 0)) >= int(stats.get("scene_assembly_minimum_indices", 2_000_000)),
        "full scene index budget not reached",
        failures,
    )
    require(
        stats.get("industrial_paving_lighting_pass") == "physical_surface_pbr_sun_point_lights",
        "industrial paving is not using the physical lighting path",
        failures,
    )
    require(stats.get("industrial_paving_unlit_overlay") is False, "industrial paving still uses an unlit overlay", failures)
    require(
        stats.get("industrial_paving_terrain_conformance_pass")
        == "subdivided_dem_sampled_surface_no_hovering_quad",
        "industrial paving is not subdivided against the DEM",
        failures,
    )
    require(
        int(stats.get("industrial_paving_terrain_tiles", 0)) >= 2500,
        "industrial paving DEM tessellation is too sparse",
        failures,
    )
    require(
        int(stats.get("converted_asset_yard_terrain_tiles", 0)) >= 100,
        "converted asset yard DEM tessellation is too sparse",
        failures,
    )
    require(int(stats.get("terrain_grid", [0, 0])[0]) >= 300, "terrain grid width too small", failures)
    require(float(stats.get("terrain_extent_m", [0, 0, 0, 0])[2]) >= 1500.0, "terrain extent too small", failures)
    require(stats.get("terrain_real_asset") is True, "terrain real asset flag not reported", failures)
    require(stats.get("terrain_dom_sampled") is True, "terrain DOM color sampling not reported", failures)
    if preset == "huilian":
        expected_huilian_view = args.huilian_view or "valve"
        approach_active = expected_huilian_view == "approach"
        camera_near_clip = float(stats.get("camera_near_clip_m", 0.0))
        camera_far_clip = float(stats.get("camera_far_clip_m", 0.0))
        camera_scene_distance = float(stats.get("camera_maximum_scene_distance_m", 0.0))
        camera_far_margin = float(stats.get("camera_far_clip_margin_m", 0.0))
        if approach_active:
            require(
                stats.get("camera_clip_policy") == "wgs84_visible_horizon_to_local_terrain_adaptive",
                "Huilian approach camera does not use WGS84 horizon-adaptive clipping",
                failures,
            )
            require(camera_near_clip > 0.0, "Huilian approach near clip is invalid", failures)
            require(
                camera_far_clip > camera_near_clip and camera_far_clip > camera_scene_distance,
                "Huilian approach clip range does not cover the loaded terrain",
                failures,
            )
            require(
                stats.get("camera_global_earth_horizon_inside_far_clip") is True,
                "Huilian approach clips the visible WGS84 horizon",
                failures,
            )
        else:
            require(
                stats.get("camera_clip_policy")
                == "dynamic_loaded_terrain_bounds_eight_corners_quantized_margin",
                "Huilian camera does not use scene-bounds dynamic clipping",
                failures,
            )
            require(
                0.25 <= camera_near_clip <= 4.0,
                "Huilian dynamic near clip is outside the precision-safe range",
                failures,
            )
            require(
                camera_far_clip >= 8000.0 and camera_far_clip > camera_scene_distance,
                "Huilian far clip does not cover the full terrain bounds",
                failures,
            )
            require(camera_far_margin >= 800.0, "Huilian far-clip safety margin is too small", failures)
        require(
            stats.get("camera_terrain_bounds_inside_far_clip") is True,
            "Huilian terrain bounds still intersect the far clip plane",
            failures,
        )
        expected_lod = 2 if expected_huilian_view in ("overview", "dlg", "approach") else 0
        expected_page = {
            "valve": [25, 16],
            "southwest": [2, 3],
            "overview": [5, 3],
            "dlg": [5, 3],
            "approach": None,
        }[expected_huilian_view]
        expected_detail_resolution = 2.0 if expected_lod == 2 else 0.5
        expected_detail_span = 2048.0 if expected_lod == 2 else 512.0
        require(
            stats.get("huilian_view") == expected_huilian_view,
            "Huilian fixed verification view mismatch",
            failures,
        )
        require(
            stats.get("terrain_data_source") == "huilian_wgs84_dem_dom_package",
            "Huilian WGS84 DEM/DOM terrain package not active",
            failures,
        )
        require(stats.get("terrain_asset_grid") == [409, 257], "Huilian runtime terrain grid mismatch", failures)
        require(int(stats.get("terrain_boundary_path_count", -1)) == 0, "Huilian terrain should not claim KML paths", failures)
        require(stats.get("terrain_binhai_dom_asset") is False, "Huilian preset incorrectly reports Binhai terrain", failures)
        require(stats.get("terrain_huilian_dem_dom_asset") is True, "Huilian DEM/DOM asset flag missing", failures)
        require(
            stats.get("terrain_huilian_converter") == "tools/build_huilian_dem_asset.py",
            "Huilian terrain converter not reported",
            failures,
        )
        require(stats.get("terrain_huilian_wgs84") is True, "Huilian WGS84 flag missing", failures)
        require(stats.get("terrain_huilian_dem_grid") == [409, 257], "Huilian DEM source grid mismatch", failures)
        require(stats.get("terrain_huilian_dom_grid") == [21170, 13283], "Huilian DOM source grid mismatch", failures)
        require(
            stats.get("terrain_huilian_resolution_m") == [30.0, 0.5],
            "Huilian DEM/DOM source resolutions mismatch",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_alignment_pass")
            == "shared_upper_left_world_file_affine_bicubic_covers_dem_centers",
            "Huilian DOM world-file alignment pass missing",
            failures,
        )
        require(
            stats.get("terrain_huilian_color_policy") == "wgs84_aligned_source_dom",
            "Huilian runtime terrain is not using source DOM colors",
            failures,
        )
        require(stats.get("terrain_huilian_dom_gpu_active") is True, "Huilian GPU DOM path is inactive", failures)
        require(
            int(stats.get("terrain_huilian_dom_gpu_texture_files", 0)) == 2,
            "Huilian GPU DOM texture set is incomplete",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_gpu_manifest")
            == "assets/showcase/runtime/huilian_dom_gpu_manifest.json",
            "Huilian GPU DOM manifest is not reported",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_gpu_sampling")
            == "overview_plus_async_full_coverage_lod_pyramid_four_slot_priority_prefetch_crossfade",
            "Huilian full-coverage LOD-pyramid GPU DOM sampling pass is missing",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_overview_grid") == [2048, 1386],
            "Huilian GPU DOM overview grid mismatch",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_detail_grid") == [1024, 1024],
            "Huilian GPU DOM detail grid mismatch",
            failures,
        )
        detail_bounds = stats.get("terrain_huilian_dom_detail_scene_bounds_m", [])
        require(
            len(detail_bounds) == 4
            and abs(float(detail_bounds[2]) - float(detail_bounds[0]) - expected_detail_span) <= 0.01
            and abs(float(detail_bounds[3]) - float(detail_bounds[1]) - expected_detail_span) <= 0.01,
            "Huilian GPU DOM resident detail bounds mismatch",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_detail_resolution_m")
            == [expected_detail_resolution, expected_detail_resolution],
            "Huilian GPU DOM detail resolution mismatch",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_detail_shader_blend")
            == "descriptor_page_bounds_wgs84_uv_edge_feather",
            "Huilian GPU DOM shader blend pass is missing",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_pyramid_schema") == "hongtu.huilian_dom_gpu_textures.v7",
            "Huilian DOM tile-pyramid schema mismatch",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_pyramid_full_coverage") is True,
            "Huilian DOM tile pyramid does not report full DEM coverage",
            failures,
        )
        require(
            int(stats.get("terrain_huilian_dom_pyramid_levels", 0)) == 3,
            "Huilian DOM tile-pyramid level count mismatch",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_pyramid_level_grids") == [[46, 31], [23, 16], [12, 8]],
            "Huilian DOM tile-pyramid level grids mismatch",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_pyramid_level_page_counts") == [1426, 368, 96],
            "Huilian DOM tile-pyramid page counts mismatch",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_pyramid_resolutions_m") == [0.5, 1.0, 2.0],
            "Huilian DOM tile-pyramid resolutions mismatch",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_pyramid_page_sizes_m") == [256.0, 512.0, 1024.0],
            "Huilian DOM tile-pyramid world page sizes mismatch",
            failures,
        )
        require(
            int(stats.get("terrain_huilian_dom_pyramid_page_files", 0)) == 1890,
            "Huilian DOM tile-pyramid file set is incomplete",
            failures,
        )
        require(
            int(stats.get("terrain_huilian_dom_pyramid_page_bytes", 0)) == 150_922_432,
            "Huilian DOM tile-pyramid byte budget mismatch",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_page_grid_px") == [512, 512],
            "Huilian DOM page pixel grid mismatch",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_resident_working_set_pages") == [2, 2],
            "Huilian DOM resident working-set grid mismatch",
            failures,
        )
        require(
            int(stats.get("terrain_huilian_dom_resident_page_count", 0)) == 4,
            "Huilian DOM resident page count mismatch",
            failures,
        )
        require(
            int(stats.get("terrain_huilian_dom_resident_slot_count", 0)) == 4,
            "Huilian DOM resident cache slot count mismatch",
            failures,
        )
        require(
            int(stats.get("terrain_huilian_dom_resident_base_level_gpu_bytes_per_slot", 0)) == 4_194_304,
            "Huilian DOM resident base-level bytes per slot mismatch",
            failures,
        )
        require(
            int(stats.get("terrain_huilian_dom_resident_gpu_bytes_per_slot", 0)) == 5_592_404,
            "Huilian DOM resident mip-chain bytes per slot mismatch",
            failures,
        )
        require(
            int(stats.get("terrain_huilian_dom_resident_gpu_budget_bytes", 0)) == 22_369_616,
            "Huilian DOM four-slot GPU byte budget mismatch",
            failures,
        )
        require(
            int(stats.get("terrain_huilian_dom_resident_mip_levels", 0)) == 11,
            "Huilian DOM resident mip-chain level count mismatch",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_sampler_trilinear") is True
            and float(stats.get("terrain_huilian_dom_sampler_max_anisotropy", 0.0)) == 16.0,
            "Huilian DOM trilinear/16x-anisotropic sampler is missing",
            failures,
        )
        require(
            int(stats.get("terrain_huilian_dom_initial_lod", -1)) == expected_lod,
            "Huilian DOM initial camera LOD mismatch",
            failures,
        )
        initial_page = stats.get("terrain_huilian_dom_initial_page")
        if approach_active:
            require(
                isinstance(initial_page, list)
                and len(initial_page) == 2
                and 0 <= int(initial_page[0]) <= 10
                and 0 <= int(initial_page[1]) <= 6,
                "Huilian approach DOM page selection is outside the LOD2 coverage grid",
                failures,
            )
        else:
            require(
                initial_page == expected_page,
                "Huilian DOM initial camera page selection mismatch",
                failures,
            )
        require(
            stats.get("terrain_huilian_dom_lod_height_thresholds_m") == [250.0, 700.0],
            "Huilian DOM camera-height LOD thresholds mismatch",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_lod_hysteresis_m") == [50.0, 75.0],
            "Huilian DOM camera-height LOD hysteresis mismatch",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_page_selection")
            == "camera_xyz_height_lod_hysteresis_nearest_2x2_clamped_to_full_coverage",
            "Huilian DOM camera/height page selection policy mismatch",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_cpu_loading") == "std_async_stb_jpeg_decode_cpu_box_mip_chain",
            "Huilian DOM asynchronous CPU page loading pass is missing",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_request_policy")
            == "foreground_preempts_directional_prefetch_epoch_cancel_stale_result_discard",
            "Huilian DOM foreground-priority request policy is missing",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_request_scheduler")
            == "single_worker_foreground_preempts_directional_prefetch",
            "Huilian DOM priority request scheduler is missing",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_prefetch_policy")
            == "camera_velocity_same_lod_one_atlas_lookahead",
            "Huilian DOM directional prefetch policy is missing",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_cooperative_cancellation")
            == "epoch_token_page_mip_delay_checkpoints",
            "Huilian DOM cooperative cancellation policy is missing",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_cache_policy") == "priority_aware_lru_hash_page_table",
            "Huilian DOM priority-aware LRU cache policy is missing",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_page_table") == "cpu_hash_residency_to_gpu_atlas_slot",
            "Huilian DOM residency page table is missing",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_eviction_policy")
            == "prefetched_first_then_true_lru_excluding_current_and_active_transition",
            "Huilian DOM transition-safe LRU eviction policy is missing",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_stream_telemetry")
            == "resident_upload_cache_frame_time_driver_budget_endurance_accounting",
            "Huilian DOM streaming telemetry contract is missing",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_driver_memory_budget")
            == "optional_vk_ext_memory_budget_device_local_heaps",
            "Huilian DOM optional driver memory-budget path is missing",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_driver_resource_lifetime")
            == "vulkan_texture_raii_vma_image_view_release",
            "Huilian DOM Vulkan texture resource-lifetime contract is missing",
            failures,
        )
        require(
            int(stats.get("terrain_huilian_dom_endurance_probe_seconds", 0)) == 60,
            "Huilian DOM endurance duration mismatch",
            failures,
        )
        require(
            int(stats.get("terrain_huilian_dom_driver_peak_growth_allowance_bytes", 0)) == 89_478_464
            and int(stats.get("terrain_huilian_dom_driver_plateau_growth_allowance_bytes", 0)) == 44_739_232,
            "Huilian DOM driver-memory plateau allowances mismatch",
            failures,
        )
        require(
            int(stats.get("terrain_huilian_dom_cache_probe_station_count", 0)) == 12,
            "Huilian DOM cache traversal station count mismatch",
            failures,
        )
        require(
            abs(float(stats.get("terrain_huilian_dom_crossfade_seconds", 0.0)) - 0.35) <= 1.0e-6,
            "Huilian DOM cross-fade duration mismatch",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_transition_metadata_grid_px") == [3, 1],
            "Huilian DOM transition metadata grid mismatch",
            failures,
        )
        require(
            stats.get("terrain_huilian_dom_gpu_commit") == "main_thread_fence_safe_descriptor_swap",
            "Huilian DOM fence-safe GPU page commit pass is missing",
            failures,
        )
        require(float(stats.get("terrain_height_range_m", 0.0)) >= 430.0, "Huilian DEM relief too low", failures)
        if approach_active:
            expected_progress = 0.0 if args.huilian_approach_progress is None else args.huilian_approach_progress
            request_driven_globe = stats.get("request_driven_globe_active") is True
            require(stats.get("global_local_transition_active") is True, "global/local transition is inactive", failures)
            require(
                stats.get("global_local_transition_pass")
                == (
                    "wgs84_ecef_double_to_enu_rtc_request_driven_globe_dom_dlg_handoff"
                    if request_driven_globe
                    else "wgs84_ecef_double_to_enu_rtc_globe_regional_tiles_dom_dlg_handoff"
                ),
                "global/local coordinate and data handoff pass is missing",
                failures,
            )
            require(stats.get("request_driven_globe_requested") is True, "request-driven globe was not requested", failures)
            require(request_driven_globe, "request-driven GlobeRenderer initialization fell back to the static atlas", failures)
            require(
                stats.get("request_driven_globe_render_path") == "GlobeRenderer_quadtree_routed_offline_xyz"
                and stats.get("request_driven_globe_projection_shared_with_local") is True,
                "request-driven globe does not share the local transition projection",
                failures,
            )
            require(
                stats.get("request_driven_globe_environment_active") is (expected_progress < 1.0),
                "global environment/local sky handoff state is incorrect",
                failures,
            )
            require(
                stats.get("request_driven_globe_provider") == "ShowcaseHuilianOfflineComposite-v1",
                "request-driven globe did not use the deterministic offline routed provider",
                failures,
            )
            require(
                int(stats.get("request_driven_globe_update_frames", 0)) >= 8
                and int(stats.get("request_driven_globe_visible_tiles", 0)) > 0
                and int(stats.get("request_driven_globe_drawing_tiles", 0)) > 0
                and int(stats.get("request_driven_globe_loaded_tiles", 0)) > 0,
                "request-driven globe did not select, stream, and draw imagery tiles",
                failures,
            )
            visible_level_range = stats.get("request_driven_globe_visible_level_range", [-1, -1])
            drawing_level_range = stats.get("request_driven_globe_drawing_level_range", [-1, -1])
            require(
                len(visible_level_range) == 2 and int(visible_level_range[1]) <= 10,
                "request-driven globe selected beyond the offline provider's maximum z10 level",
                failures,
            )
            if expected_progress <= 0.0:
                require(
                    len(drawing_level_range) == 2 and 1 <= int(drawing_level_range[1]) <= 4,
                    "global approach did not draw the offline z1-z4 imagery levels",
                    failures,
                )
            if expected_progress >= 0.65:
                require(
                    len(drawing_level_range) == 2 and int(drawing_level_range[1]) == 10,
                    "near approach did not draw the offline z10 regional imagery",
                    failures,
                )
            require(
                int(stats.get("request_driven_globe_mipped_tiles", 0))
                == int(stats.get("request_driven_globe_loaded_tiles", -1)),
                "request-driven globe contains a resident tile without a complete mip chain",
                failures,
            )
            require(
                abs(float(stats.get("global_local_transition_progress", -1.0)) - expected_progress) <= 1.0e-5,
                "global/local fixed regression progress mismatch",
                failures,
            )
            require(
                abs(float(stats.get("global_local_transition_duration_s", 0.0)) - 18.0) <= 1.0e-5,
                "global/local interactive flight duration mismatch",
                failures,
            )
            require(
                stats.get("global_local_transition_altitude_monotonic") is True
                and stats.get("global_local_transition_local_blend_monotonic") is True,
                "global/local curve is not monotonic",
                failures,
            )
            require(
                int(stats.get("global_local_transition_audit_samples", 0)) >= 257,
                "global/local curve audit is too sparse",
                failures,
            )
            require(
                int(stats.get("global_local_transition_scale_levels_visited", 0)) >= 6,
                "global/local curve does not traverse all required scale levels",
                failures,
            )
            require(
                float(stats.get("global_local_transition_maximum_ecef_enu_roundtrip_error_m", 1.0)) < 1.0e-5,
                "global/local ECEF/ENU roundtrip error exceeds tolerance",
                failures,
            )
            require(
                float(stats.get("global_local_transition_maximum_float_lowering_error_m", 10.0)) < 1.0,
                "global/local float RTC lowering error exceeds one metre",
                failures,
            )
            require(
                float(stats.get("global_local_transition_maximum_view_direction_step_deg", 180.0)) < 5.0,
                "global/local camera path contains an abrupt direction jump",
                failures,
            )
            require(
                stats.get("global_local_transition_terrain_clip_audit_passed") is True,
                "global/local path clips the Huilian terrain at an audited sample",
                failures,
            )
            require(stats.get("global_earth_wgs84_ellipsoid") is True, "WGS84 global Earth is inactive", failures)
            require(stats.get("global_earth_texture_loaded") is True, "global Earth imagery failed to load", failures)
            require(
                stats.get("global_earth_texture_source_encoding") == "dds_bc1_dxt1",
                "global Earth did not use the verified BC1/DXT1 source decoder",
                failures,
            )
            require(
                stats.get("global_earth_texture_source_grid") == [4096, 4096],
                "global Earth source texture grid mismatch",
                failures,
            )
            require(
                stats.get("global_earth_texture_runtime_grid") == [1024, 1024],
                "global Earth runtime texture grid mismatch",
                failures,
            )
            require(int(stats.get("global_earth_hemisphere_textures", 0)) == 2, "global Earth tile count mismatch", failures)
            if request_driven_globe:
                require(
                    stats.get("global_earth_static_mesh_active") is False
                    and int(stats.get("global_earth_mesh_vertices", -1)) == 0
                    and int(stats.get("global_earth_mesh_triangles", -1)) == 0,
                    "legacy static global Earth mesh was not removed from the request-driven path",
                    failures,
                )
            else:
                require(
                    int(stats.get("global_earth_mesh_vertices", 0)) == 16471
                    and int(stats.get("global_earth_mesh_triangles", 0)) == 32400,
                    "global Earth WGS84 mesh topology mismatch",
                    failures,
                )
            require(stats.get("regional_earth_imagery_active") is True, "regional Earth imagery LOD is inactive", failures)
            require(
                stats.get("regional_earth_imagery_pass")
                == (
                    "request_driven_quadtree_z1_z4_z8_z10_to_huilian_dom"
                    if request_driven_globe
                    else "web_mercator_xyz_z3_z4_z8_z10_lod_to_huilian_dom"
                ),
                "regional Earth imagery handoff pass is missing",
                failures,
            )
            require(
                stats.get("regional_earth_static_atlas_active") is (not request_driven_globe),
                "static regional atlas activation does not match the selected globe path",
                failures,
            )
            require(
                stats.get("regional_earth_imagery_source")
                == "Tianditu offline z3/z4 plus Esri World Imagery bounded z8/z10 caches"
                and stats.get("regional_earth_imagery_source_roots")
                == ["assets/tiles/tianditu_img", "assets/tiles/esri_huilian"],
                "regional Earth imagery provenance mismatch",
                failures,
            )
            require(
                stats.get("regional_earth_imagery_attributions")
                == [
                    "assets/tiles/tianditu_img/SOURCE_ATTRIBUTION.md",
                    "assets/tiles/esri_huilian/SOURCE_ATTRIBUTION.md",
                ],
                "regional Earth imagery attribution is missing",
                failures,
            )
            require(
                stats.get("regional_earth_imagery_scheme") == "WebMercatorQuad_XYZ_EPSG3857",
                "regional Earth imagery tile scheme mismatch",
                failures,
            )
            require(
                stats.get("regional_earth_imagery_parent_tile") == [3, 6, 3]
                and stats.get("regional_earth_imagery_detail_tiles")
                == [[4, 12, 6], [4, 13, 6], [4, 12, 7], [4, 13, 7]],
                "regional Earth parent/child tile selection mismatch",
                failures,
            )
            bounds = stats.get("regional_earth_imagery_bounds_lon_lat_deg", [])
            require(
                len(bounds) == 4
                and abs(float(bounds[0]) - 90.0) <= 1.0e-3
                and abs(float(bounds[1])) <= 1.0e-3
                and abs(float(bounds[2]) - 135.0) <= 1.0e-3
                and abs(float(bounds[3]) - 40.979898) <= 1.0e-3,
                "regional Earth imagery geographic bounds mismatch",
                failures,
            )
            context_bounds = stats.get("regional_earth_imagery_context_bounds_lon_lat_deg", [])
            near_bounds = stats.get("regional_earth_imagery_near_bounds_lon_lat_deg", [])
            require(
                stats.get("regional_earth_imagery_context_tile_origin_grid") == [8, 205, 107, 8, 8]
                and len(context_bounds) == 4
                and max(
                    abs(float(value) - expected)
                    for value, expected in zip(
                        context_bounds,
                        [108.28125, 17.97873309555617, 119.53125, 28.304380682962773],
                    )
                )
                <= 1.0e-3,
                "regional Earth z8 context atlas bounds mismatch",
                failures,
            )
            require(
                stats.get("regional_earth_imagery_near_tile_origin_grid") == [10, 833, 441, 8, 8]
                and len(near_bounds) == 4
                and max(
                    abs(float(value) - expected)
                    for value, expected in zip(
                        near_bounds,
                        [112.8515625, 21.616579336740603, 115.6640625, 24.20688962239802],
                    )
                )
                <= 1.0e-3,
                "regional Earth z10 near atlas bounds mismatch",
                failures,
            )
            require(stats.get("regional_earth_imagery_tile_grid_px") == [256, 256], "regional tile grid mismatch", failures)
            require(
                stats.get("regional_earth_imagery_coarse_runtime_grid") == [256, 256]
                and stats.get("regional_earth_imagery_detail_runtime_grid") == [512, 512]
                and stats.get("regional_earth_imagery_context_runtime_grid") == [2048, 2048]
                and stats.get("regional_earth_imagery_near_runtime_grid") == [2048, 2048]
                and stats.get("regional_earth_imagery_loaded_tiles") == [1, 4, 64, 64],
                "regional Earth imagery atlas assembly mismatch",
                failures,
            )
            require(
                stats.get("regional_earth_imagery_source_encoding") == "xyz_tile_atlas_stb_rgba8",
                "regional Earth imagery did not use the shared tile-atlas loader",
                failures,
            )
            require(
                int(stats.get("regional_earth_imagery_lod_levels", 0)) == 4
                and stats.get("regional_earth_imagery_coverage_altitude_range_m") == [5000000.0, 2000000.0]
                and stats.get("regional_earth_imagery_detail_altitude_range_m") == [1600000.0, 400000.0]
                and stats.get("regional_earth_imagery_context_altitude_range_m") == [1000000.0, 250000.0]
                and stats.get("regional_earth_imagery_near_altitude_range_m") == [200000.0, 50000.0],
                "regional Earth imagery LOD policy mismatch",
                failures,
            )
            require(
                stats.get("regional_earth_imagery_manifest")
                == "data/showcase/huilian_regional_imagery_manifest.json"
                and int(stats.get("regional_earth_imagery_manifest_tile_count", 0)) == 128
                and int(stats.get("regional_earth_imagery_manifest_tile_bytes", 0)) == 1_699_537
                and stats.get("regional_earth_imagery_manifest_sha256")
                == "e5e99434ea1e35ac156d34aa7de1e1febbd4bd61fe72dca76c15430431a78a28",
                "regional Earth imagery manifest contract mismatch",
                failures,
            )
            manifest_path = Path(args.cwd) / "data" / "showcase" / "huilian_regional_imagery_manifest.json"
            try:
                imagery_manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            except (OSError, json.JSONDecodeError) as exc:
                failures.append(f"regional Earth imagery manifest cannot be read: {exc}")
                imagery_manifest = {}
            require(
                imagery_manifest.get("total_tile_count") == 128
                and imagery_manifest.get("total_tile_bytes") == 1_699_537
                and imagery_manifest.get("aggregate_sha256")
                == "e5e99434ea1e35ac156d34aa7de1e1febbd4bd61fe72dca76c15430431a78a28",
                "regional Earth imagery on-disk manifest audit mismatch",
                failures,
            )
            regional_altitude = float(stats.get("global_local_transition_view_altitude_m", 0.0))
            expected_coverage_weight = descending_log_lod_weight(regional_altitude, 5_000_000.0, 2_000_000.0)
            expected_detail_weight = descending_log_lod_weight(regional_altitude, 1_600_000.0, 400_000.0)
            expected_context_weight = descending_log_lod_weight(regional_altitude, 1_000_000.0, 250_000.0)
            expected_near_weight = descending_log_lod_weight(regional_altitude, 200_000.0, 50_000.0)
            require(
                abs(float(stats.get("regional_earth_imagery_coverage_weight", -1.0)) - expected_coverage_weight)
                <= 2.0e-3
                and abs(float(stats.get("regional_earth_imagery_detail_weight", -1.0)) - expected_detail_weight)
                <= 2.0e-3
                and abs(float(stats.get("regional_earth_imagery_context_weight", -1.0)) - expected_context_weight)
                <= 2.0e-3
                and abs(float(stats.get("regional_earth_imagery_near_weight", -1.0)) - expected_near_weight)
                <= 2.0e-3,
                "regional Earth imagery LOD weights do not match camera altitude",
                failures,
            )
            if expected_coverage_weight <= 0.0:
                expected_regional_zoom = -1
            elif expected_detail_weight < 0.5:
                expected_regional_zoom = 3
            elif expected_context_weight < 0.5:
                expected_regional_zoom = 4
            elif expected_near_weight < 0.5:
                expected_regional_zoom = 8
            else:
                expected_regional_zoom = 10
            require(
                int(stats.get("regional_earth_imagery_effective_zoom", -99)) == expected_regional_zoom,
                "regional Earth imagery effective zoom mismatch",
                failures,
            )
            require(
                int(stats.get("regional_earth_imagery_lod_audit_samples", 0)) >= 257
                and stats.get("regional_earth_imagery_coverage_weight_monotonic") is True
                and stats.get("regional_earth_imagery_detail_weight_monotonic") is True
                and stats.get("regional_earth_imagery_context_weight_monotonic") is True
                and stats.get("regional_earth_imagery_near_weight_monotonic") is True
                and int(stats.get("regional_earth_imagery_levels_visited", 0)) == 5,
                "regional Earth imagery LOD transition audit failed",
                failures,
            )
            require(
                stats.get("regional_earth_imagery_parent_child_valid") is True
                and stats.get("regional_earth_imagery_anchor_inside_bounds") is True
                and stats.get("regional_earth_imagery_context_anchor_inside_bounds") is True
                and stats.get("regional_earth_imagery_near_anchor_inside_bounds") is True
                and stats.get("regional_earth_imagery_tile_audit_passed") is True,
                "regional Earth imagery tile coverage audit failed",
                failures,
            )
            require(
                stats.get("huilian_dlg_visualization_active") is True,
                "global/local approach does not carry the DLG layer into the local handoff",
                failures,
            )
            altitude = float(stats.get("global_local_transition_view_altitude_m", 0.0))
            local_blend = float(stats.get("global_local_transition_local_blend", -1.0))
            if expected_progress <= 1.0e-6:
                require(altitude >= 11_999_000.0, "global capture does not start at planetary altitude", failures)
                require(local_blend <= 1.0e-6, "global capture prematurely enables local data blend", failures)
                require(stats.get("global_local_transition_scale_level") == "Global", "global scale level mismatch", failures)
            if expected_progress >= 1.0 - 1.0e-6:
                require(abs(altitude - 900.0) <= 0.1, "local capture does not finish at park altitude", failures)
                require(local_blend >= 1.0 - 1.0e-6, "local capture does not complete the handoff", failures)
                require(stats.get("global_local_transition_scale_level") == "Building", "local scale level mismatch", failures)
        if expected_huilian_view in ("dlg", "approach"):
            require(stats.get("huilian_dlg_visualization_active") is True, "Huilian DLG overlay is inactive", failures)
            require(
                stats.get("huilian_dlg_pass")
                == "real_geojson_semantic_layers_wgs84_double_precision_enu_dem_drape",
                "Huilian real DLG render pass missing",
                failures,
            )
            require(stats.get("huilian_dlg_source_valid") is True, "Huilian DLG GeoJSON failed validation", failures)
            require(stats.get("huilian_dlg_enu_valid") is True, "Huilian DLG ENU projection failed", failures)
            require(
                stats.get("huilian_dlg_source_dataset") == "OpenStreetMap via Overpass API",
                "Huilian DLG provenance missing",
                failures,
            )
            require(
                stats.get("huilian_dlg_source_license") == "ODbL-1.0; OpenStreetMap contributors",
                "Huilian DLG license attribution missing",
                failures,
            )
            require(
                int(stats.get("huilian_dlg_source_feature_count", 0)) >= 2000,
                "Huilian DLG source feature count too low",
                failures,
            )
            require(int(stats.get("huilian_dlg_polyline_count", 0)) >= 2000, "Huilian DLG line count too low", failures)
            require(int(stats.get("huilian_dlg_vertex_count", 0)) >= 17000, "Huilian DLG vertex count too low", failures)
            class_counts = stats.get("huilian_dlg_class_counts", {})
            require(int(class_counts.get("road", 0)) >= 900, "Huilian DLG road layer too sparse", failures)
            require(int(class_counts.get("building", 0)) >= 750, "Huilian DLG building layer too sparse", failures)
            require(int(class_counts.get("railway", 0)) >= 60, "Huilian DLG railway layer too sparse", failures)
            require(int(class_counts.get("pipeline", 0)) >= 25, "Huilian DLG pipeline layer too sparse", failures)
            require(int(class_counts.get("power", 0)) >= 75, "Huilian DLG power layer too sparse", failures)
            require(int(class_counts.get("water", 0)) >= 20, "Huilian DLG hydrography layer too sparse", failures)
            require(
                stats.get("huilian_dlg_layer_mode") == (args.dlg_layer or "all"),
                "Huilian DLG layer filter mismatch",
                failures,
            )
            require(
                int(stats.get("huilian_dlg_active_polyline_count", 0)) > 0
                and int(stats.get("huilian_dlg_active_vertex_count", 0)) > 0,
                "Huilian DLG selected layer has no visible geometry",
                failures,
            )
            dlg_bounds = stats.get("huilian_dlg_source_wgs84_bounds", [])
            require(len(dlg_bounds) == 4, "Huilian DLG WGS84 bounds missing", failures)
            if len(dlg_bounds) == 4:
                expected_bounds = [114.542834905631, 22.712074101085, 114.656446016742, 22.783462989974]
                require(
                    max(abs(float(a) - b) for a, b in zip(dlg_bounds, expected_bounds)) <= 1.0e-6,
                    "Huilian DLG does not clip to the DEM/DOM bounds",
                    failures,
                )
            enu_bounds = stats.get("huilian_dlg_enu_bounds_m", [])
            require(len(enu_bounds) == 4, "Huilian DLG ENU bounds missing", failures)
            if len(enu_bounds) == 4:
                require(
                    float(enu_bounds[0]) <= -5800.0
                    and float(enu_bounds[1]) <= -3940.0
                    and float(enu_bounds[2]) >= 5800.0
                    and float(enu_bounds[3]) >= 3940.0,
                    "Huilian DLG ENU footprint does not cover the terrain",
                    failures,
                )
            require(
                float(stats.get("huilian_dlg_maximum_roundtrip_error_m", 1.0)) < 1.0e-5,
                "Huilian DLG WGS84/ENU roundtrip error exceeds tolerance",
                failures,
            )
            require(
                stats.get("huilian_dlg_dem_conformance") == "per_segment_rendered_dem_bilinear_ground_sampling",
                "Huilian DLG is not draped on the rendered DEM",
                failures,
            )
        require(
            stats.get("terrain_material_pass")
            == "huilian_dom_full_coverage_lod_pyramid_gpu_slope_hardstand_grade",
            "Huilian DOM/terrain material pass not reported",
            failures,
        )
    else:
        require(
            stats.get("terrain_data_source") == "binhai_dom_kml_package",
            "Binhai DOM/KML terrain package not active",
            failures,
        )
        require(int(stats.get("terrain_asset_grid", [0, 0])[0]) >= 1024, "terrain DOM grid too small", failures)
        require(int(stats.get("terrain_boundary_path_count", 0)) >= 1, "Binhai KML boundary path missing", failures)
        require(stats.get("terrain_binhai_dom_asset") is True, "Binhai DOM asset flag not reported", failures)
        require(
            stats.get("terrain_binhai_converter") == "tools/build_binhai_terrain_asset.py",
            "Binhai terrain converter not reported",
            failures,
        )
        require(
            stats.get("terrain_binhai_dom_tone_pass") == "coastal_dom_luma_compress_water_urban_grade",
            "Binhai DOM tone mapping pass not reported",
            failures,
        )
        require(
            stats.get("terrain_binhai_highlight_compression") is True,
            "Binhai highlight compression missing",
            failures,
        )
        require(float(stats.get("terrain_height_range_m", 0.0)) >= 18.0, "Binhai coastal relief too low", failures)
        require(
            stats.get("terrain_material_pass") == "dom_slope_rock_strata_grade",
            "terrain slope/rock material pass not reported",
            failures,
        )
    require(
        stats.get("terrain_horizon_edge_blend_pass") == "coastal_apron_uv_feather_no_rectangular_tile_edge",
        "terrain horizon edge blend pass missing",
        failures,
    )
    require(int(stats.get("terrain_horizon_apron_strips", 0)) >= 4, "terrain horizon apron strips missing", failures)
    require(int(stats.get("terrain_horizon_apron_segments", 0)) >= 64, "terrain horizon apron grid too coarse", failures)
    require(
        float(stats.get("terrain_horizon_apron_noise_strength", 0.0)) >= 0.18,
        "terrain horizon apron noise strength too low",
        failures,
    )
    require(
        float(stats.get("terrain_horizon_edge_blend_width_uv", 0.0)) >= 0.12,
        "terrain horizon edge blend width too small",
        failures,
    )
    require(stats.get("terrain_cut_fill_pad") is True, "industrial cut/fill terrain pad not reported", failures)
    require(
        stats.get("terrain_model_fusion_detail_pass")
        == "terrain_cutfill_foundation_contact_shadow_slope_blend_overlay",
        "terrain/model fusion detail pass missing",
        failures,
    )
    require(
        stats.get("cinematic_daylight_industrial_depth_pass") == "overview_oiled_steel_side_depth_no_white_models",
        "daylight industrial depth pass missing",
        failures,
    )
    require(
        stats.get("cinematic_daylight_ambient_contrast_pass") == "lower_day_fill_for_contact_shadows",
        "daylight ambient contrast pass missing",
        failures,
    )
    require(int(stats.get("facility_instances", 0)) >= 50, "facility/model instance count too low", failures)
    require(
        stats.get("model_detail_pass") == "industrial_facade_tank_vehicle_details",
        "industrial model detail pass not reported",
        failures,
    )
    require(
        stats.get("vehicle_detail_pass") == "windows_tail_lights_bumpers_contact_shadows",
        "vehicle detail pass not reported",
        failures,
    )
    require(int(stats.get("vehicle_instances", 0)) >= 18, "vehicle instance count too low", failures)
    require(int(stats.get("vehicle_detail_parts", 0)) >= 160, "vehicle detail part count too low", failures)
    require(
        stats.get("warehouse_street_district_pass") == "tourable_warehouse_street_hazmat_vehicle_proxy_layer",
        "warehouse street district proxy layer not reported",
        failures,
    )
    require(stats.get("warehouse_street_district_active") is True, "warehouse street district layer inactive", failures)
    require(
        int(stats.get("warehouse_street_district_road_segments", 0)) >= 7,
        "warehouse street road network too sparse",
        failures,
    )
    require(
        int(stats.get("warehouse_street_district_guard_bollards", 0)) >= 42,
        "warehouse street guard bollards too sparse",
        failures,
    )
    require(
        int(stats.get("warehouse_street_district_crosswalk_bars", 0)) >= 20,
        "warehouse street crosswalk bars too sparse",
        failures,
    )
    require(
        int(stats.get("warehouse_street_district_direction_arrows", 0)) >= 10,
        "warehouse tour direction arrows too sparse",
        failures,
    )
    require(
        float(stats.get("warehouse_street_tour_route_length_m", 0.0)) >= 400.0,
        "warehouse street tour route too short",
        failures,
    )
    require(
        stats.get("hazmat_vehicle_proxy_pass") == "cylindrical_tanker_convoy_hazard_placards_containment_pads",
        "hazmat vehicle proxy pass not reported",
        failures,
    )
    require(int(stats.get("hazmat_vehicle_convoy_trucks", 0)) >= 6, "hazmat tanker convoy too sparse", failures)
    require(int(stats.get("hazmat_vehicle_placards", 0)) >= 24, "hazmat vehicle placards too sparse", failures)
    require(int(stats.get("hazmat_vehicle_wheel_sets", 0)) >= 18, "hazmat wheel sets too sparse", failures)
    require(
        int(stats.get("hazmat_vehicle_containment_pads", 0)) >= 6,
        "hazmat containment pads too sparse",
        failures,
    )
    require(
        int(stats.get("warehouse_street_hazmat_scene_instances", 0)) >= 140,
        "warehouse street hazmat scene instances too sparse",
        failures,
    )
    require(
        stats.get("xineng_dcc_export_queue_pass") == "max_to_gltf_priority_queue_material_lod_handoff",
        "DCC export priority queue pass not reported",
        failures,
    )
    require(
        stats.get("xineng_dcc_export_queue_runtime_mode") == "performance_preset_dcc_export_readiness_overlay",
        "DCC export priority queue runtime mode missing",
        failures,
    )
    dcc_export_queue_required = preset == "performance"
    require(
        not dcc_export_queue_required or stats.get("xineng_dcc_export_queue_active") is True,
        "DCC export priority queue inactive",
        failures,
    )
    require(
        not dcc_export_queue_required or int(stats.get("xineng_dcc_export_priority_assets", 0)) >= 10,
        "DCC export priority assets too sparse",
        failures,
    )
    require(
        not dcc_export_queue_required or int(stats.get("xineng_dcc_export_heavy_partition_assets", 0)) >= 7,
        "DCC heavy partition asset count too low",
        failures,
    )
    require(
        not dcc_export_queue_required or int(stats.get("xineng_dcc_export_batches", 0)) >= 4,
        "DCC export batches missing",
        failures,
    )
    require(
        not dcc_export_queue_required or int(stats.get("xineng_dcc_export_gate_checks", 0)) >= 6,
        "DCC export gate checks missing",
        failures,
    )
    require(
        int(stats.get("xineng_dcc_export_fbx_ready_assets", 0)) == 7,
        "DCC FBX-ready asset count mismatch",
        failures,
    )
    require(
        int(stats.get("xineng_dcc_export_runtime_converted_assets", 0)) == 7,
        "DCC runtime converted asset count mismatch",
        failures,
    )
    require(
        int(stats.get("xineng_dcc_export_proxy_backed_max_assets", 0)) == 45,
        "DCC proxy-backed MAX asset count mismatch",
        failures,
    )
    require(
        int(stats.get("xineng_dcc_export_material_slot_queue_files", 0)) >= 700,
        "DCC material-slot mapping queue too small",
        failures,
    )
    require(
        int(stats.get("xineng_dcc_export_texture_sidecar_files", 0)) >= 800,
        "DCC texture sidecar count too small",
        failures,
    )
    require(
        stats.get("xineng_dcc_export_exact_mesh_runtime_policy")
        == "max_authoring_sources_require_fbx_glb_export_before_runtime_load",
        "DCC exact mesh runtime policy missing",
        failures,
    )
    require(
        not dcc_export_queue_required or int(stats.get("xineng_dcc_export_scene_instances", 0)) >= 90,
        "DCC export queue scene instances too sparse",
        failures,
    )
    require(
        stats.get("yard_lamp_detail_pass") == "arms_fixtures_small_warm_glow",
        "yard lamp detail pass not reported",
        failures,
    )
    require(int(stats.get("yard_lamp_posts", 0)) >= 16, "yard lamp post count too low", failures)
    require(int(stats.get("yard_lamp_detail_parts", 0)) >= 48, "yard lamp detail count too low", failures)
    require(stats.get("industrial_detail_pass") == "flanges_valves_trusses_bands", "industrial micro detail pass missing", failures)
    require(int(stats.get("hero_flange_collars", 0)) >= 70, "flange collar detail count too low", failures)
    require(int(stats.get("hero_valve_wheels", 0)) >= 20, "valve wheel detail count too low", failures)
    require(int(stats.get("hero_truss_braces", 0)) >= 45, "truss brace detail count too low", failures)
    require(int(stats.get("hero_tower_bands", 0)) >= 20, "tower band detail count too low", failures)
    require(
        stats.get("industrial_weathering_pass") == "tower_stains_rust_labels_pipe_bands",
        "industrial weathering pass missing",
        failures,
    )
    require(int(stats.get("weathered_process_towers", 0)) >= 6, "weathered process tower count too low", failures)
    require(int(stats.get("tower_weathering_streaks", 0)) >= 70, "tower weathering streaks too sparse", failures)
    require(int(stats.get("tower_rust_rings", 0)) >= 45, "tower rust rings too sparse", failures)
    require(int(stats.get("tower_warning_plates", 0)) >= 18, "tower warning plates missing", failures)
    require(
        stats.get("cinematic_tower_panel_finish_pass") == "steel_panel_seams_base_skirts_specular_patches",
        "cinematic tower panel finish pass missing",
        failures,
    )
    require(int(stats.get("cinematic_tower_panel_seams", 0)) >= 48, "cinematic tower panel seams too sparse", failures)
    require(
        int(stats.get("cinematic_tower_specular_panels", 0)) >= 24,
        "cinematic tower specular panels too sparse",
        failures,
    )
    require(int(stats.get("cinematic_tower_base_skirts", 0)) >= 6, "cinematic tower base skirts missing", failures)
    require(
        int(stats.get("cinematic_tower_cylinder_sides", 0)) >= 64,
        "cinematic tower cylinder side count too low",
        failures,
    )
    require(
        stats.get("cinematic_tower_material_breakup_pass") == "cold_stains_oil_panels_warm_rim_catchlights",
        "cinematic tower material breakup pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_tower_material_breakup_patches", 0)) >= 36,
        "cinematic tower material breakup patches too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_tower_rim_catchlights", 0)) >= 24,
        "cinematic tower rim catchlights too sparse",
        failures,
    )
    require(
        stats.get("cinematic_tower_body_tone_grade_pass") == "low_whiteout_oiled_steel_shells",
        "cinematic tower body tone grade pass missing",
        failures,
    )
    require(
        float(stats.get("cinematic_tower_body_tone_scale", 1.0)) <= 0.70,
        "cinematic tower body tone scale too high",
        failures,
    )
    require(
        stats.get("cinematic_tower_body_shader_tone_pass") == "vertical_shell_oiled_steel_luma_clamp",
        "cinematic tower body shader tone pass missing",
        failures,
    )
    require(
        float(stats.get("cinematic_tower_body_shader_tone_strength", 0.0)) >= 0.60,
        "cinematic tower body shader tone strength too weak",
        failures,
    )
    require(
        stats.get("cinematic_tower_deck_cap_tone_pass") == "low_luma_grated_caps_platforms",
        "cinematic tower deck/cap tone pass missing",
        failures,
    )
    require(
        float(stats.get("cinematic_tower_deck_cap_tone_strength", 0.0)) >= 0.70,
        "cinematic tower deck/cap tone strength too weak",
        failures,
    )
    require(
        stats.get("cinematic_neutral_equipment_whiteout_clamp_pass") == "old_steel_tanks_pipes_low_luma",
        "cinematic neutral equipment whiteout clamp pass missing",
        failures,
    )
    require(
        float(stats.get("cinematic_neutral_equipment_tone_strength", 0.0)) >= 0.70,
        "cinematic neutral equipment tone strength too weak",
        failures,
    )
    require(int(stats.get("pipe_service_bands", 0)) >= 90, "pipe service bands too sparse", failures)
    require(int(stats.get("pipe_wear_collars", 0)) >= 90, "pipe wear collars too sparse", failures)
    require(
        stats.get("pipeline_layout_pass") == "orthogonal_rack_risers_manifolds",
        "orthogonal pipeline layout pass missing",
        failures,
    )
    require(
        stats.get("pipeline_alignment_pass") == "orthogonal_xy_runs_vertical_risers_flanged_equipment_tie_ins",
        "orthogonal/flanged pipeline alignment pass missing",
        failures,
    )
    require(
        int(stats.get("pipeline_axis_aligned_route_solver_passes", 0)) >= 2,
        "pipeline axis-aligned route solver not reported",
        failures,
    )
    require(
        stats.get("pipeline_route_solver_scope") == "routed_process_pipes_cables_lamp_arms_overlay_links",
        "pipeline route solver scope missing",
        failures,
    )
    require(
        stats.get("pipeline_attached_weld_band_pass") == "flush_bead_bands_no_floating_sleeves",
        "pipeline attached weld band pass missing",
        failures,
    )
    require(
        int(stats.get("pipeline_attached_weld_bands", 0)) >= 96,
        "pipeline attached weld bands too sparse",
        failures,
    )
    require(
        stats.get("pipeline_pipe_material_uvs") == "circumference_length_brushed_metal_channels",
        "pipeline pipe material UV channels missing",
        failures,
    )
    require(int(stats.get("structured_pipe_rack_levels", 0)) >= 4, "structured pipe rack levels too low", failures)
    require(int(stats.get("structured_pipe_rack_frames", 0)) >= 16, "pipe rack support frames too sparse", failures)
    require(int(stats.get("structured_pipe_rack_runs", 0)) >= 18, "structured pipe rack run count too low", failures)
    require(int(stats.get("pipeline_branch_connections", 0)) >= 10, "pipeline branch connections too sparse", failures)
    require(int(stats.get("pipeline_elbow_markers", 0)) >= 48, "pipeline elbow markers too sparse", failures)
    require(int(stats.get("pipeline_support_saddles", 0)) >= 120, "pipeline support saddles too sparse", failures)
    require(
        int(stats.get("pipeline_equipment_flanged_connections", 0)) >= 22,
        "pipeline equipment flanged connections too sparse",
        failures,
    )
    require(
        int(stats.get("pipeline_equipment_flange_plates", 0)) >= 44,
        "pipeline equipment double-flange plates too sparse",
        failures,
    )
    require(
        int(stats.get("pipeline_equipment_flange_bolt_markers", 0)) >= 264,
        "pipeline equipment flange bolt markers too sparse",
        failures,
    )
    require(
        int(stats.get("pipeline_equipment_tie_in_gaskets", 0)) >= 22,
        "pipeline equipment tie-in gaskets missing",
        failures,
    )
    require(
        int(stats.get("pipeline_equipment_tie_in_short_spools", 0)) >= 22,
        "pipeline equipment tie-in short spools missing",
        failures,
    )
    require(
        stats.get("pipeline_equipment_connection_detail_pass")
        == "double_flange_short_spool_gasket_bolt_ring_at_equipment_crossings",
        "pipeline equipment connection detail pass missing",
        failures,
    )
    require(
        int(stats.get("pipeline_diagonal_process_pipe_segments", -1)) == 0,
        "process pipeline should not report diagonal pipe segments",
        failures,
    )
    require(
        stats.get("pipeline_operability_pass") == "rack_layered_manifolds_valve_platforms_flanged_tie_ins",
        "pipeline operability overlay pass missing",
        failures,
    )
    require(
        int(stats.get("pipeline_operability_manifold_stations", 0)) >= 6,
        "pipeline manifold stations too sparse",
        failures,
    )
    require(
        int(stats.get("pipeline_operability_cross_headers", 0)) >= 12,
        "pipeline orthogonal cross headers too sparse",
        failures,
    )
    require(
        int(stats.get("pipeline_operability_vertical_risers", 0)) >= 24,
        "pipeline vertical risers too sparse",
        failures,
    )
    require(
        int(stats.get("pipeline_operability_valve_stations", 0)) >= 12,
        "pipeline valve stations too sparse",
        failures,
    )
    require(
        int(stats.get("pipeline_operability_inspection_platforms", 0)) >= 6,
        "pipeline inspection platforms too sparse",
        failures,
    )
    require(
        int(stats.get("pipeline_operability_pipe_clamp_sets", 0)) >= 48,
        "pipeline clamp sets too sparse",
        failures,
    )
    require(
        int(stats.get("pipeline_unflanged_equipment_crossings", -1)) == 0,
        "pipeline equipment crossings should all be flanged",
        failures,
    )
    require(
        int(stats.get("pipeline_nonorthogonal_process_pipe_segments", -1)) == 0,
        "process pipeline should only report orthogonal visual segments",
        failures,
    )
    require(
        stats.get("operational_twin_analysis_pass") == "gis_process_safety_disassembly_sprinkler_overlay",
        "operational twin analysis overlay pass missing",
        failures,
    )
    require(
        stats.get("gis_measurement_pass") == "distance_area_elevation_dem_cut_fill_volume_runtime_samples",
        "GIS measurement pass missing",
        failures,
    )
    require(int(stats.get("gis_measurement_overlays", 0)) >= 4, "GIS measurement overlay count too low", failures)
    require(float(stats.get("gis_distance_measure_m", 0.0)) >= 600.0, "GIS distance measurement too short", failures)
    require(float(stats.get("gis_area_measure_sqm", 0.0)) >= 100000.0, "GIS area measurement too small", failures)
    minimum_elevation_delta = 0.1 if preset == "huilian" else 0.5
    require(
        float(stats.get("gis_elevation_delta_m", 0.0)) >= minimum_elevation_delta,
        "GIS elevation delta not reported",
        failures,
    )
    require(
        stats.get("gis_volume_measure_valid") is True,
        "terrain-referenced GIS volume measurement is invalid",
        failures,
    )
    volume_footprint_area = float(stats.get("gis_volume_footprint_area_sqm", 0.0))
    volume_sampled_area = float(stats.get("gis_volume_sampled_area_sqm", 0.0))
    volume_cut = float(stats.get("gis_volume_cut_m3", 0.0))
    volume_fill = float(stats.get("gis_volume_fill_m3", 0.0))
    volume_net = float(stats.get("gis_volume_net_m3", 0.0))
    require(volume_footprint_area >= 70000.0, "GIS volume footprint is too small", failures)
    require(
        abs(volume_sampled_area - volume_footprint_area) <= max(0.01, volume_footprint_area * 1.0e-6),
        "GIS volume integration did not preserve footprint area",
        failures,
    )
    require(volume_cut + volume_fill >= 1000.0, "GIS DEM cut/fill volume is too small", failures)
    require(
        abs(volume_net - (volume_cut - volume_fill)) <= max(0.01, (volume_cut + volume_fill) * 1.0e-6),
        "GIS net volume does not equal cut minus fill",
        failures,
    )
    require(
        stats.get("gis_volume_algorithm")
        == "simple_polygon_ear_clipping_cell_clipping_centroid_quadrature",
        "GIS volume integration algorithm is not reported",
        failures,
    )
    require(
        stats.get("gis_volume_terrain_surface") == "rendered_dem_surface_bilinear_with_visible_grade_blend",
        "GIS volume does not report the rendered DEM sampling surface",
        failures,
    )
    require(float(stats.get("gis_volume_sample_spacing_m", 99.0)) <= 30.0, "GIS volume sample spacing too coarse", failures)
    require(int(stats.get("gis_volume_grid_cells", 0)) >= 100, "GIS volume grid too sparse", failures)
    require(int(stats.get("gis_volume_terrain_samples", 0)) >= 100, "GIS volume terrain samples too sparse", failures)
    require(
        int(stats.get("gis_volume_visualization_samples", 0)) >= 20,
        "GIS cut/fill visualization samples too sparse",
        failures,
    )
    require(
        int(stats.get("gis_volume_reference_grid_lines", 0)) >= 11,
        "GIS volume reference grid too sparse",
        failures,
    )
    require(
        stats.get("digital_earth_gis_pass") == "globe_graticule_tile_footprints_measurement_volume_projection",
        "digital earth GIS pass missing",
        failures,
    )
    require(
        stats.get("digital_earth_gis_measurement_units")
        == "meters_square_meters_cubic_meters_height_delta",
        "digital earth GIS measurement units not reported",
        failures,
    )
    require(
        stats.get("digital_earth_gis_terrain_source")
        == ("huilian_wgs84_dem_dom_package" if preset == "huilian" else "binhai_dom_kml_package"),
        "digital earth GIS terrain source does not match the active terrain package",
        failures,
    )
    require(
        stats.get("digital_earth_gis_georef_pass") == "epsg3857_bounds_wgs84_webmercator_tile_audit",
        "digital earth georef audit pass missing",
        failures,
    )
    require(stats.get("digital_earth_gis_source_crs") == "EPSG:3857", "digital earth source CRS mismatch", failures)
    require(stats.get("digital_earth_gis_target_crs") == "EPSG:4326", "digital earth target CRS mismatch", failures)
    require(
        stats.get("digital_earth_gis_tile_matrix") == "WebMercatorQuad",
        "digital earth tile matrix not reported",
        failures,
    )
    expected_process_mode = args.process_mode or "all"
    expected_process_counts = {
        "all": {
            "layers": 4,
            "edges": 6,
            "arrows": 12,
            "upstream_nodes": 3,
            "downstream_nodes": 4,
            "upstream": True,
            "downstream": True,
            "isolation": True,
            "shutdown": True,
            "pulses": 18,
        },
        "upstream": {
            "layers": 1,
            "edges": 3,
            "arrows": 6,
            "upstream_nodes": 3,
            "downstream_nodes": 0,
            "upstream": True,
            "downstream": False,
            "isolation": False,
            "shutdown": False,
            "pulses": 9,
        },
        "downstream": {
            "layers": 1,
            "edges": 3,
            "arrows": 6,
            "upstream_nodes": 0,
            "downstream_nodes": 4,
            "upstream": False,
            "downstream": True,
            "isolation": False,
            "shutdown": False,
            "pulses": 9,
        },
        "isolation": {
            "layers": 1,
            "edges": 0,
            "arrows": 0,
            "upstream_nodes": 0,
            "downstream_nodes": 0,
            "upstream": False,
            "downstream": False,
            "isolation": True,
            "shutdown": False,
            "pulses": 0,
        },
        "shutdown": {
            "layers": 1,
            "edges": 0,
            "arrows": 0,
            "upstream_nodes": 0,
            "downstream_nodes": 0,
            "upstream": False,
            "downstream": False,
            "isolation": False,
            "shutdown": True,
            "pulses": 0,
        },
    }[expected_process_mode]
    expected_process_flanged_connections = expected_process_counts["edges"] * 2
    if preset in ("process", "drill") and expected_process_counts["isolation"]:
        expected_process_flanged_connections += 3 * 2
    require(
        stats.get("process_flow_analysis_pass") == "directed_pipe_rack_upstream_downstream_overlay",
        "process flow analysis pass missing",
        failures,
    )
    require(
        stats.get("process_flow_alignment_pass") == "orthogonal_folded_process_lines_with_flanged_tie_ins",
        "process flow orthogonal/flanged alignment pass missing",
        failures,
    )
    require(int(stats.get("process_flow_nodes", 0)) >= 7, "process flow node count too low", failures)
    require(int(stats.get("process_flow_edges", 0)) >= 6, "process flow edge count too low", failures)
    require(
        int(stats.get("process_flow_active_edges", -1)) == expected_process_counts["edges"],
        "process active flow edge count did not match selected mode",
        failures,
    )
    require(
        int(stats.get("process_flow_diagonal_pipe_segments", -1)) == 0,
        "process flow should not report diagonal pipe segments",
        failures,
    )
    require(
        int(stats.get("process_flow_flanged_connections", -1)) == expected_process_flanged_connections,
        "process flow flanged connection count did not match selected mode",
        failures,
    )
    require(
        int(stats.get("process_flow_direction_arrows", -1)) == expected_process_counts["arrows"],
        "process flow direction arrows did not match selected mode",
        failures,
    )
    require(
        int(stats.get("process_flow_highlighted_upstream_nodes", -1))
        == expected_process_counts["upstream_nodes"],
        "upstream process highlighting did not match selected mode",
        failures,
    )
    require(
        int(stats.get("process_flow_highlighted_downstream_nodes", -1))
        == expected_process_counts["downstream_nodes"],
        "downstream process highlighting did not match selected mode",
        failures,
    )
    require(float(stats.get("process_flow_route_length_m", 0.0)) >= 550.0, "process flow route too short", failures)
    require(stats.get("process_analysis_mode") == expected_process_mode, "process analysis mode mismatch", failures)
    require(
        stats.get("process_analysis_interactive_controls") is True,
        "process analysis interactive controls missing",
        failures,
    )
    require(int(stats.get("process_analysis_interactive_modes", 0)) >= 5, "process analysis modes missing", failures)
    require(
        stats.get("process_analysis_interactive_hotkeys") == "o_o",
        "process analysis interactive hotkey metadata missing",
        failures,
    )
    require(
        int(stats.get("process_analysis_active_layers", -1)) == expected_process_counts["layers"],
        "process analysis active layer count mismatch",
        failures,
    )
    require(
        stats.get("process_analysis_upstream_active") is expected_process_counts["upstream"],
        "process upstream layer active flag mismatch",
        failures,
    )
    require(
        stats.get("process_analysis_downstream_active") is expected_process_counts["downstream"],
        "process downstream layer active flag mismatch",
        failures,
    )
    require(
        stats.get("process_analysis_isolation_active") is expected_process_counts["isolation"],
        "process isolation layer active flag mismatch",
        failures,
    )
    require(
        stats.get("process_analysis_shutdown_active") is expected_process_counts["shutdown"],
        "process shutdown layer active flag mismatch",
        failures,
    )
    require(
        stats.get("process_management_analysis_pass") == "isolation_bypass_dependency_shutdown_overlay",
        "process management analysis pass missing",
        failures,
    )
    require(
        stats.get("xineng_process_source_impact_pass") == "max_source_process_lineage_impact_analysis_proxy",
        "Xineng process source-impact pass missing",
        failures,
    )
    require(
        stats.get("xineng_process_source_impact_runtime_mode")
        == "procedural_process_unit_proxy_until_dcc_export",
        "Xineng process source-impact runtime mode missing",
        failures,
    )
    require(
        int(stats.get("xineng_process_source_impact_assets", 0)) >= 7,
        "Xineng process source-impact asset count missing",
        failures,
    )
    require(
        int(stats.get("xineng_process_source_impact_source_asset_bytes", 0)) >= 771000000,
        "Xineng process source-impact byte budget missing",
        failures,
    )
    require(
        int(stats.get("xineng_process_source_impact_root_asset_bytes", 0)) >= 608000000,
        "Xineng process source-impact root asset byte budget missing",
        failures,
    )
    require(
        stats.get("process_telemetry_trend_pass") == "dcs_realtime_pressure_temperature_flow_gas_valve_trends",
        "process telemetry trend pass missing",
        failures,
    )
    require(
        stats.get("process_telemetry_runtime_mode") == "process_preset_scada_trend_alarm_interlock_overlay",
        "process telemetry runtime mode missing",
        failures,
    )
    require(
        int(stats.get("process_telemetry_source_assets", 0)) >= 10,
        "process telemetry source asset count missing",
        failures,
    )
    require(
        int(stats.get("process_telemetry_source_asset_bytes", 0)) >= 15000000,
        "process telemetry source asset bytes missing",
        failures,
    )
    require(
        int(stats.get("process_telemetry_diagonal_segments", -1)) == 0,
        "process telemetry should not report diagonal segments",
        failures,
    )
    require(
        stats.get("safety_route_planning_pass") == "fire_rescue_and_personnel_evacuation_ground_routes",
        "safety route planning pass missing",
        failures,
    )
    require(int(stats.get("safety_route_plans", 0)) >= 2, "safety route plan count too low", failures)
    require(int(stats.get("fire_rescue_route_checkpoints", 0)) >= 6, "fire rescue checkpoints too sparse", failures)
    require(float(stats.get("fire_rescue_route_length_m", 0.0)) >= 430.0, "fire rescue route too short", failures)
    require(
        int(stats.get("personnel_evacuation_route_checkpoints", 0)) >= 7,
        "evacuation checkpoints too sparse",
        failures,
    )
    require(
        float(stats.get("personnel_evacuation_route_length_m", 0.0)) >= 520.0,
        "personnel evacuation route too short",
        failures,
    )
    require(int(stats.get("safety_route_chevron_markers", 0)) >= 40, "safety route chevrons too sparse", failures)
    require(
        stats.get("emergency_route_planning_detail_pass") == "graph_reroute_capacity_eta_overlay",
        "emergency route planning detail pass missing",
        failures,
    )
    require(
        stats.get("emergency_route_solver_pass") == "multi_constraint_a_star_hazard_capacity_eta_audit",
        "emergency route solver pass missing",
        failures,
    )
    require(
        stats.get("emergency_route_solver_algorithm") == "a_star_weighted_length_hazard_capacity_clearance",
        "emergency route solver algorithm not reported",
        failures,
    )
    require(
        int(stats.get("emergency_route_solver_candidate_routes", 0)) >= 4,
        "emergency route solver candidate routes too sparse",
        failures,
    )
    require(
        stats.get("emergency_route_wind_gas_reroute_pass")
        == "wind_field_gas_plume_exposure_cost_route_audit",
        "emergency route wind/gas reroute pass missing",
        failures,
    )
    require(
        stats.get("emergency_route_wind_gas_runtime_mode") == "routes_preset_wind_gas_cost_overlay",
        "emergency route wind/gas runtime mode missing",
        failures,
    )
    require(
        stats.get("emergency_route_wind_gas_incident_source") == "route_blocked_zone_gas_release",
        "emergency route wind/gas incident source not reported",
        failures,
    )
    require(
        float(stats.get("emergency_route_wind_gas_downwind_m", 0.0)) >= 200.0,
        "emergency route wind/gas downwind envelope too short",
        failures,
    )
    require(
        float(stats.get("emergency_route_wind_gas_crosswind_half_width_m", 0.0)) >= 40.0,
        "emergency route wind/gas crosswind half-width missing",
        failures,
    )
    require(
        float(stats.get("emergency_route_wind_gas_penalty_weight", 0.0)) >= 2.0,
        "emergency route wind/gas penalty weight too low",
        failures,
    )
    require(
        float(stats.get("emergency_route_wind_gas_safe_exposure_limit", 0.0)) >= 0.55,
        "emergency route wind/gas safe exposure limit missing",
        failures,
    )
    require(
        stats.get("xineng_route_source_dispatch_pass")
        == "max_access_fire_security_asset_route_dispatch_proxy",
        "Xineng route source dispatch pass missing",
        failures,
    )
    require(
        stats.get("xineng_route_source_dispatch_runtime_mode")
        == "procedural_route_asset_proxy_until_dcc_export",
        "Xineng route source dispatch runtime mode not reported",
        failures,
    )
    require(
        int(stats.get("xineng_route_source_dispatch_assets", 0)) >= 14,
        "Xineng route source dispatch asset count missing",
        failures,
    )
    require(
        int(stats.get("xineng_route_source_dispatch_source_asset_bytes", 0)) >= 155000000,
        "Xineng route source dispatch byte budget missing",
        failures,
    )
    expected_sprinkler_mode = args.sprinkler_mode or "all"
    expected_sprinkler_counts = {
        "all": {
            "layers": 4,
            "heads": 16,
            "jets": 64,
            "mist": 48,
            "monitors": 2,
            "zones": 4,
            "coverage": 16,
            "valves": 4,
            "gauges": 8,
            "pumps": 2,
            "deluge": 4,
            "runoff": 8,
            "wind_vectors": 10,
            "wet": 18,
            "alarms": 4,
            "canopies": 4,
            "heads_active": True,
            "monitors_active": True,
            "deluge_active": True,
            "runoff_active": True,
        },
        "heads": {
            "layers": 1,
            "heads": 16,
            "jets": 64,
            "mist": 48,
            "monitors": 0,
            "zones": 0,
            "coverage": 16,
            "valves": 0,
            "gauges": 0,
            "pumps": 0,
            "deluge": 0,
            "runoff": 0,
            "wind_vectors": 0,
            "wet": 0,
            "alarms": 0,
            "canopies": 0,
            "heads_active": True,
            "monitors_active": False,
            "deluge_active": False,
            "runoff_active": False,
        },
        "monitors": {
            "layers": 1,
            "heads": 0,
            "jets": 0,
            "mist": 0,
            "monitors": 2,
            "zones": 0,
            "coverage": 0,
            "valves": 0,
            "gauges": 0,
            "pumps": 0,
            "deluge": 0,
            "runoff": 0,
            "wind_vectors": 0,
            "wet": 0,
            "alarms": 0,
            "canopies": 0,
            "heads_active": False,
            "monitors_active": True,
            "deluge_active": False,
            "runoff_active": False,
        },
        "deluge": {
            "layers": 1,
            "heads": 0,
            "jets": 0,
            "mist": 0,
            "monitors": 0,
            "zones": 4,
            "coverage": 0,
            "valves": 0,
            "gauges": 0,
            "pumps": 0,
            "deluge": 4,
            "runoff": 0,
            "wind_vectors": 0,
            "wet": 0,
            "alarms": 4,
            "canopies": 4,
            "heads_active": False,
            "monitors_active": False,
            "deluge_active": True,
            "runoff_active": False,
        },
        "runoff": {
            "layers": 1,
            "heads": 0,
            "jets": 0,
            "mist": 0,
            "monitors": 0,
            "zones": 0,
            "coverage": 0,
            "valves": 4,
            "gauges": 8,
            "pumps": 2,
            "deluge": 0,
            "runoff": 8,
            "wind_vectors": 10,
            "wet": 18,
            "alarms": 0,
            "canopies": 0,
            "heads_active": False,
            "monitors_active": False,
            "deluge_active": False,
            "runoff_active": True,
        },
    }[expected_sprinkler_mode]
    require(
        stats.get("sprinkler_effect_pass") == "wind_drifted_deluge_heads_water_fans_mist",
        "sprinkler effect pass missing",
        failures,
    )
    require(stats.get("sprinkler_mode") == expected_sprinkler_mode, "sprinkler mode mismatch", failures)
    require(stats.get("sprinkler_interactive_controls") is True, "sprinkler interactive controls missing", failures)
    require(int(stats.get("sprinkler_interactive_modes", 0)) >= 5, "sprinkler interactive modes missing", failures)
    require(
        stats.get("sprinkler_interactive_hotkeys") == "i_i",
        "sprinkler interactive hotkey metadata missing",
        failures,
    )
    require(
        int(stats.get("sprinkler_active_layers", -1)) == expected_sprinkler_counts["layers"],
        "sprinkler active layer count mismatch",
        failures,
    )
    require(
        stats.get("sprinkler_heads_active") is expected_sprinkler_counts["heads_active"],
        "sprinkler head active flag mismatch",
        failures,
    )
    require(
        stats.get("sprinkler_monitors_active") is expected_sprinkler_counts["monitors_active"],
        "sprinkler monitor active flag mismatch",
        failures,
    )
    require(
        stats.get("sprinkler_deluge_active") is expected_sprinkler_counts["deluge_active"],
        "sprinkler deluge active flag mismatch",
        failures,
    )
    require(
        stats.get("sprinkler_runoff_active") is expected_sprinkler_counts["runoff_active"],
        "sprinkler runoff active flag mismatch",
        failures,
    )
    require(
        int(stats.get("sprinkler_heads", -1)) == expected_sprinkler_counts["heads"],
        "sprinkler head count mismatch",
        failures,
    )
    require(
        int(stats.get("sprinkler_water_jets", -1)) == expected_sprinkler_counts["jets"],
        "sprinkler water jet count mismatch",
        failures,
    )
    require(
        int(stats.get("sprinkler_mist_puffs", -1)) == expected_sprinkler_counts["mist"],
        "sprinkler mist count mismatch",
        failures,
    )
    require(
        int(stats.get("fire_water_monitor_count", -1)) == expected_sprinkler_counts["monitors"],
        "fire water monitor count mismatch",
        failures,
    )
    require(
        stats.get("sprinkler_system_detail_pass") == "zone_valve_pressure_deluge_wind_wetfloor_overlay",
        "sprinkler system detail pass missing",
        failures,
    )
    require(
        stats.get("xineng_max_emergency_proxy_pass") == "max_only_fire_assets_procedural_runtime_proxies",
        "Xineng MAX emergency proxy pass missing",
        failures,
    )
    require(
        stats.get("xineng_max_emergency_runtime_mode") == "procedural_proxy_until_dcc_export",
        "Xineng MAX emergency proxy runtime mode not reported",
        failures,
    )
    require(
        int(stats.get("xineng_max_emergency_proxy_assets", 0)) >= 4,
        "Xineng MAX emergency proxy asset coverage too small",
        failures,
    )
    require(
        int(stats.get("xineng_max_emergency_hydrant_stations", 0)) >= 6,
        "Xineng MAX hydrant proxies too sparse",
        failures,
    )
    require(
        int(stats.get("xineng_max_emergency_fire_water_monitors", 0)) >= 2,
        "Xineng MAX fire-water monitor proxies missing",
        failures,
    )
    require(
        int(stats.get("xineng_max_emergency_fire_towers", 0)) >= 2,
        "Xineng MAX fire tower proxies missing",
        failures,
    )
    require(
        int(stats.get("xineng_max_emergency_power_cabinets", 0)) >= 3,
        "Xineng MAX power-monitor cabinet proxies too sparse",
        failures,
    )
    require(
        int(stats.get("xineng_max_emergency_coverage_rings", 0)) >= 8,
        "Xineng MAX emergency coverage rings too sparse",
        failures,
    )
    require(
        int(stats.get("xineng_max_emergency_route_links", 0)) >= 6,
        "Xineng MAX emergency route links too sparse",
        failures,
    )
    require(
        int(stats.get("xineng_max_emergency_proxy_beacons", 0)) >= 13,
        "Xineng MAX emergency proxy beacons too sparse",
        failures,
    )
    require(
        float(stats.get("xineng_max_emergency_water_network_length_m", 0.0)) >= 250.0,
        "Xineng MAX emergency water-network length too short",
        failures,
    )
    require(
        stats.get("fountain_effect_pass") == "wind_affected_cooling_water_fountain_basin",
        "fountain effect pass missing",
        failures,
    )
    require(
        stats.get("fountain_mode") == expected_fountain_mode,
        f"fountain mode mismatch: {stats.get('fountain_mode')} != {expected_fountain_mode}",
        failures,
    )
    require(
        stats.get("fountain_interactive_controls") is True,
        "fountain interactive controls not reported",
        failures,
    )
    require(int(stats.get("fountain_interactive_modes", 0)) >= 5, "fountain mode count too low", failures)
    require(
        stats.get("fountain_interactive_hotkeys") == "6_f6",
        "fountain hotkeys not reported",
        failures,
    )
    require(int(stats.get("fountain_basin_count", 0)) >= 2, "fountain basins too sparse", failures)
    require(
        int(stats.get("fountain_total_water_jets", 0)) >= 38,
        "fountain total water jet count missing",
        failures,
    )
    require(
        int(stats.get("fountain_active_layers", -1)) == expected_fountain_counts["layers"],
        "fountain active layer count mismatch",
        failures,
    )
    require(
        int(stats.get("fountain_water_jets", -1)) == expected_fountain_counts["jets"],
        "fountain active water jet count mismatch",
        failures,
    )
    require(
        int(stats.get("fountain_mist_puffs", -1)) == expected_fountain_counts["mist"],
        "fountain active mist count mismatch",
        failures,
    )
    require(
        int(stats.get("fountain_ripple_discs", -1)) == expected_fountain_counts["ripples"],
        "fountain active ripple count mismatch",
        failures,
    )
    require(
        int(stats.get("fountain_spray_canopies", -1)) == expected_fountain_counts["canopies"],
        "fountain active canopy count mismatch",
        failures,
    )
    require(
        int(stats.get("fountain_wind_vectors", -1)) == expected_fountain_counts["wind_vectors"],
        "fountain active wind-vector count mismatch",
        failures,
    )
    require(stats.get("fountain_jets_active") is (expected_fountain_counts["jets"] > 0), "fountain jets active flag mismatch", failures)
    require(stats.get("fountain_mist_active") is (expected_fountain_counts["mist"] > 0), "fountain mist active flag mismatch", failures)
    require(
        stats.get("fountain_ripples_active") is (expected_fountain_counts["ripples"] > 0),
        "fountain ripples active flag mismatch",
        failures,
    )
    require(
        stats.get("fountain_wind_vectors_active") is (expected_fountain_counts["wind_vectors"] > 0),
        "fountain wind-vector active flag mismatch",
        failures,
    )
    require(float(stats.get("fountain_wind_drift_m", 0.0)) >= 1.0, "fountain wind drift not reported", failures)
    require(
        stats.get("equipment_disassembly_pass") == "pump_skid_exploded_parts_with_leader_lines",
        "equipment disassembly pass missing",
        failures,
    )
    require(int(stats.get("equipment_disassembly_parts", 0)) >= 8, "equipment disassembly part count too low", failures)
    require(
        int(stats.get("equipment_disassembly_exploded_links", 0)) >= 7,
        "equipment disassembly leader links too sparse",
        failures,
    )
    require(
        stats.get("equipment_disassembly_detail_pass") == "maintenance_sequence_fasteners_material_callouts",
        "equipment disassembly detail pass missing",
        failures,
    )
    require(
        stats.get("xineng_equipment_source_disassembly_pass")
        == "max_fire_monitor_cabinet_detector_tower_camera_exploded_proxy",
        "Xineng equipment source-disassembly pass missing",
        failures,
    )
    require(
        stats.get("xineng_equipment_source_disassembly_runtime_mode")
        == "procedural_equipment_part_proxy_until_dcc_export",
        "Xineng equipment source-disassembly runtime mode missing",
        failures,
    )
    require(
        int(stats.get("xineng_equipment_source_disassembly_assets", 0)) >= 5,
        "Xineng equipment source-disassembly asset count missing",
        failures,
    )
    require(
        int(stats.get("xineng_equipment_source_disassembly_source_asset_bytes", 0)) >= 58000000,
        "Xineng equipment source-disassembly byte budget missing",
        failures,
    )
    require(
        int(stats.get("xineng_equipment_source_disassembly_fire_monitor_bytes", 0)) >= 48600000,
        "Xineng equipment source-disassembly fire monitor byte count missing",
        failures,
    )
    require(
        stats.get("cinematic_lighting_pass") == "warm_halos_wet_reflections_low_steam",
        "cinematic lighting pass missing",
        failures,
    )
    require(int(stats.get("cinematic_warm_light_halos", 0)) >= 70, "cinematic warm light halos too sparse", failures)
    require(
        stats.get("cinematic_lamp_bloom_boost_pass")
        == "pipe_canyon_warm_halation_and_wet_reflection_streaks",
        "cinematic lamp bloom boost pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_lamp_bloom_boost_halos", 0)) >= 32,
        "cinematic lamp bloom boost halos too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_lamp_reflection_streaks", 0)) >= 28,
        "cinematic lamp reflection streaks too sparse",
        failures,
    )
    require(
        0.64 <= float(stats.get("cinematic_lamp_halo_alpha_max", 0.0)) <= 0.70,
        "cinematic lamp halo alpha boost missing",
        failures,
    )
    require(
        stats.get("transparent_far_halo_sky_guard_pass") == "distance_faded_light_halos",
        "transparent far-halo sky guard pass missing",
        failures,
    )
    require(
        940.0 <= float(stats.get("transparent_halo_far_fade_start_m", 0.0)) <= 1020.0,
        "transparent halo far fade start outside balanced range",
        failures,
    )
    require(
        1680.0 <= float(stats.get("transparent_halo_far_fade_end_m", 0.0)) <= 1840.0,
        "transparent halo far fade end outside balanced range",
        failures,
    )
    require(
        stats.get("transparent_halo_overlap_limiter_pass") == "foreground_warm_luma_alpha_taming",
        "transparent halo overlap limiter pass missing",
        failures,
    )
    require(
        float(stats.get("transparent_halo_overlap_limiter_alpha_scale", 1.0)) <= 0.45,
        "transparent halo overlap limiter alpha scale too high",
        failures,
    )
    require(
        int(stats.get("cinematic_wet_reflection_patches", 0)) >= 24,
        "cinematic wet reflection patches too sparse",
        failures,
    )
    require(
        stats.get("cinematic_wet_reflection_whiteout_clamp_pass") == "foreground_oily_reflection_low_alpha",
        "cinematic wet reflection whiteout clamp pass missing",
        failures,
    )
    require(
        float(stats.get("cinematic_wet_reflection_foreground_alpha_scale", 1.0)) <= 0.34,
        "cinematic wet reflection foreground alpha too high",
        failures,
    )
    require(int(stats.get("cinematic_low_steam_wisps", 0)) >= 36, "cinematic low steam wisps too sparse", failures)
    require(
        stats.get("cinematic_process_vapor_pass") == "tower_pipe_vent_skid_steam_layers",
        "cinematic process vapor pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_process_vapor_plumes", 0)) >= 18,
        "cinematic process vapor plumes too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_pipe_vent_wisps", 0)) >= 30,
        "cinematic pipe vent wisps too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_skid_steam_puffs", 0)) >= 20,
        "cinematic skid steam puffs too sparse",
        failures,
    )
    require(
        stats.get("cinematic_low_steam_whiteout_clamp_pass") == "condensed_low_alpha_no_pipe_rack_white_band",
        "cinematic low steam whiteout clamp pass missing",
        failures,
    )
    require(float(stats.get("cinematic_low_steam_alpha_scale", 1.0)) <= 0.42, "cinematic low steam alpha too high", failures)
    require(
        int(stats.get("cinematic_specular_pipe_glints", 0)) >= 32,
        "cinematic pipe glints too sparse",
        failures,
    )
    require(
        stats.get("cinematic_tower_service_pass") == "ladders_nozzles_downcomers_platform_braces",
        "cinematic tower service detail pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_tower_nozzle_details", 0)) >= 36,
        "cinematic tower nozzle details too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_tower_ladder_rungs", 0)) >= 72,
        "cinematic tower ladder rungs too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_tower_service_pipes", 0)) >= 18,
        "cinematic tower service pipes too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_tower_platform_braces", 0)) >= 60,
        "cinematic tower platform braces too sparse",
        failures,
    )
    require(
        stats.get("cinematic_ground_process_skid_pass") == "pumps_exchangers_skids_utility_tieins",
        "cinematic ground process skid pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_ground_process_skids", 0)) >= 10,
        "cinematic ground process skids too sparse",
        failures,
    )
    require(int(stats.get("cinematic_pump_units", 0)) >= 16, "cinematic pump units too sparse", failures)
    require(
        int(stats.get("cinematic_heat_exchanger_bundles", 0)) >= 8,
        "cinematic heat exchanger bundles too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_skid_utility_runs", 0)) >= 24,
        "cinematic skid utility tie-ins too sparse",
        failures,
    )
    require(
        stats.get("cinematic_ground_micro_pass") == "drains_drip_stains_anchor_pads_wet_edges",
        "cinematic ground micro pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_ground_drain_channels", 0)) >= 18,
        "cinematic ground drain channels too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_pipe_drip_stains", 0)) >= 36,
        "cinematic pipe drip stains too sparse",
        failures,
    )
    require(int(stats.get("cinematic_anchor_pads", 0)) >= 40, "cinematic anchor pads too sparse", failures)
    require(
        int(stats.get("cinematic_wet_edge_highlights", 0)) >= 28,
        "cinematic wet edge highlights too sparse",
        failures,
    )
    require(
        stats.get("cinematic_pipe_rack_density_pass") == "truss_frames_cable_trays_valve_manifolds_handrails",
        "cinematic pipe-rack density pass missing",
        failures,
    )
    require(
        stats.get("cinematic_pipe_rack_visual_readability_pass")
        == "box_portal_columns_low_luma_supports_no_white_rod_field",
        "cinematic pipe-rack visual readability pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_pipe_rack_box_portal_columns", 0)) >= 56,
        "cinematic pipe-rack box portal columns too sparse",
        failures,
    )
    require(
        float(stats.get("cinematic_pipe_rack_support_luma_scale", 1.0)) <= 0.70,
        "cinematic pipe-rack support luma scale too bright",
        failures,
    )
    require(
        stats.get("cinematic_pipe_rack_vertical_clutter_reduced") is True,
        "cinematic pipe-rack vertical clutter reduction missing",
        failures,
    )
    require(
        int(stats.get("cinematic_pipe_rack_truss_frames", 0)) >= 22,
        "cinematic pipe-rack truss frames too sparse",
        failures,
    )
    require(int(stats.get("cinematic_cable_tray_runs", 0)) >= 12, "cinematic cable tray runs too sparse", failures)
    require(
        int(stats.get("cinematic_valve_manifold_clusters", 0)) >= 12,
        "cinematic valve manifold clusters too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_dense_handrail_segments", 0)) >= 48,
        "cinematic dense handrail segments too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_secondary_pipe_drops", 0)) >= 16,
        "cinematic secondary pipe drops too sparse",
        failures,
    )
    require(
        stats.get("cinematic_pipe_rack_organization_pass") == "undertrays_shadow_bands_portal_bents",
        "cinematic pipe-rack organization pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_pipe_rack_undertray_panels", 0)) >= 18,
        "cinematic pipe-rack undertray panels too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_pipe_rack_ground_shadow_bands", 0)) >= 8,
        "cinematic pipe-rack ground shadow bands too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_pipe_rack_organizing_bents", 0)) >= 18,
        "cinematic pipe-rack organizing bents too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_pipe_rack_layer_separators", 0)) >= 36,
        "cinematic pipe-rack layer separators too sparse",
        failures,
    )
    require(
        stats.get("cinematic_pipe_rack_occlusion_mass_pass") == "baffles_shadow_pockets_pipe_bank_backing",
        "cinematic pipe-rack occlusion mass pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_pipe_rack_occlusion_baffles", 0)) >= 24,
        "cinematic pipe-rack occlusion baffles too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_pipe_rack_shadow_pockets", 0)) >= 12,
        "cinematic pipe-rack shadow pockets too sparse",
        failures,
    )
    require(
        stats.get("cinematic_foreground_main_rack_pass") == "portal_frames_crossbeams_decks_shadow_mats",
        "cinematic foreground main rack pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_foreground_main_rack_frames", 0)) >= 14,
        "cinematic foreground main rack frames too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_foreground_main_rack_crossbeams", 0)) >= 84,
        "cinematic foreground main rack crossbeams too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_foreground_main_rack_deck_panels", 0)) >= 10,
        "cinematic foreground main rack deck panels too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_foreground_main_rack_shadow_mats", 0)) >= 12,
        "cinematic foreground main rack shadow mats too sparse",
        failures,
    )
    require(
        stats.get("cinematic_rack_foundation_access_pass") == "pedestals_saddles_braces_stairs",
        "cinematic rack foundation/access pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_rack_foundation_pedestals", 0)) >= 54,
        "cinematic rack foundation pedestals too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_rack_pipe_saddle_shoes", 0)) >= 72,
        "cinematic rack pipe saddle shoes too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_rack_diagonal_braces", 0)) >= 64,
        "cinematic rack diagonal braces too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_rack_access_stair_flights", 0)) >= 6,
        "cinematic rack access stair flights too sparse",
        failures,
    )
    require(
        stats.get("cinematic_pipe_material_grade_pass") == "dark_oiled_steel_low_whiteout",
        "cinematic pipe material grade pass missing",
        failures,
    )
    require(
        stats.get("cinematic_pipe_material_whiteout_reduced") is True,
        "cinematic pipe material whiteout reduction missing",
        failures,
    )
    require(
        stats.get("cinematic_foreground_pipe_whiteout_clamp_pass") == "neutral_steam_pipe_luma_tamed",
        "cinematic foreground pipe whiteout clamp pass missing",
        failures,
    )
    require(
        stats.get("cinematic_foreground_pipe_shader_whiteout_clamp") is True,
        "cinematic foreground pipe shader whiteout clamp missing",
        failures,
    )
    require(
        stats.get("cinematic_foreground_rack_line_tone_pass") == "thin_steel_lines_low_whiteout",
        "cinematic foreground rack line tone pass missing",
        failures,
    )
    require(
        float(stats.get("cinematic_foreground_rack_line_tone_strength", 0.0)) >= 0.70,
        "cinematic foreground rack line tone strength too weak",
        failures,
    )
    require(
        stats.get("cinematic_foreground_rack_overlay_regrade_pass") == "low_alpha_oiled_steel_glints_no_white_band",
        "cinematic foreground rack overlay regrade pass missing",
        failures,
    )
    require(
        float(stats.get("cinematic_foreground_rack_overlay_alpha_max", 1.0)) <= 0.52,
        "cinematic foreground rack overlay alpha too high",
        failures,
    )
    require(
        stats.get("cinematic_north_service_yard_whiteout_clamp_pass") == "back_service_models_luma_tamed",
        "cinematic north service yard whiteout clamp pass missing",
        failures,
    )
    require(
        stats.get("cinematic_north_service_yard_shader_whiteout_clamp") is True,
        "cinematic north service yard shader whiteout clamp missing",
        failures,
    )
    require(
        stats.get("cinematic_platform_integration_pass") == "grated_decks_kickplates_lifted_rails",
        "cinematic platform integration pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_grated_deck_panels", 0)) >= 20,
        "cinematic grated deck panels too sparse",
        failures,
    )
    require(int(stats.get("cinematic_kick_plate_runs", 0)) >= 24, "cinematic kick plate runs too sparse", failures)
    require(stats.get("cinematic_rail_tone_lift") is True, "cinematic rail tone lift not reported", failures)
    require(
        stats.get("cinematic_platform_material_pass") == "low_contrast_wet_grated_deck",
        "cinematic platform material pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_deck_wet_sheen_panels", 0)) >= 16,
        "cinematic deck wet sheen panels too sparse",
        failures,
    )
    require(
        stats.get("cinematic_deck_panel_tone_profile") == "low_contrast_oiled_steel",
        "cinematic deck panel tone profile missing",
        failures,
    )
    require(
        stats.get("cinematic_rack_light_field_pass") == "string_lights_deck_pools_tower_rims",
        "cinematic rack light field pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_rack_string_lights", 0)) >= 56,
        "cinematic rack string lights too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_deck_warm_light_pools", 0)) >= 18,
        "cinematic deck warm light pools too sparse",
        failures,
    )
    require(
        stats.get("cinematic_practical_pin_light_pass")
        == "midground_sodium_pinlights_for_industrial_night_density",
        "cinematic practical pin light pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_practical_pin_lights", 0)) >= 96,
        "cinematic practical pin lights too sparse",
        failures,
    )
    require(
        stats.get("cinematic_practical_pin_light_halo_radius_pass") == "camera_visible_midground_halation",
        "cinematic practical pin light halo radius pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_tower_rim_light_accents", 0)) >= 24,
        "cinematic tower rim light accents too sparse",
        failures,
    )
    require(
        stats.get("cinematic_readability_pass") == "twilight_fill_volumetric_glow",
        "cinematic twilight readability pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_industrial_haze_volumes", 0)) >= 22,
        "cinematic industrial haze volumes too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_shadow_lift_pools", 0)) >= 14,
        "cinematic shadow lift pools too sparse",
        failures,
    )
    require(stats.get("cinematic_twilight_fill_shader") is True, "cinematic twilight fill shader missing", failures)
    require(
        stats.get("cinematic_point_light_volume_boost") is True,
        "cinematic point-light volume boost missing",
        failures,
    )
    require(
        stats.get("cinematic_practical_bloom_pass") == "distant_pinlights_tower_halos_wet_reflections",
        "cinematic practical bloom pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_tower_beacon_halos", 0)) >= 24,
        "cinematic tower beacon halos too sparse",
        failures,
    )
    require(
        int(stats.get("distant_refinery_glow_halos", 0)) >= 64,
        "distant refinery glow halos too sparse",
        failures,
    )
    require(stats.get("light_marker_alpha_profile") == "soft_radial_halo", "soft light marker alpha missing", failures)
    require(
        stats.get("metallic_lighting_pass") == "fresnel_env_point_specular",
        "metallic Fresnel/specular lighting pass missing",
        failures,
    )
    require(
        stats.get("cinematic_metal_material_pass") == "anisotropic_brushed_wet_steel",
        "cinematic brushed wet metal material pass missing",
        failures,
    )
    require(stats.get("metal_anisotropic_brush_shader") is True, "anisotropic metal brush shader missing", failures)
    require(stats.get("metal_wet_edge_reflection") is True, "wet metal edge reflection missing", failures)
    require(
        stats.get("metal_point_light_grazing_highlights") is True,
        "metal point-light grazing highlights missing",
        failures,
    )
    require(
        stats.get("ambient_occlusion_pass") == "analytic_industrial_contact_ao",
        "analytic industrial contact AO pass missing",
        failures,
    )
    require(int(stats.get("ambient_occlusion_occluders", 0)) >= 16, "ambient occlusion occluder count too low", failures)
    require(float(stats.get("ambient_occlusion_max_strength", 0.0)) >= 0.55, "ambient occlusion max strength too low", failures)
    require(
        stats.get("equipment_shadow_pass") == "contact_and_sun_cast_decals",
        "equipment contact/sun-cast shadow pass missing",
        failures,
    )
    require(
        stats.get("cinematic_daylight_shadow_boost_pass") == "stronger_overview_equipment_grounding",
        "daylight equipment shadow boost pass missing",
        failures,
    )
    require(
        stats.get("hero_refinery_analytic_shadow_pass") == "towers_pipe_rack_soft_projected",
        "hero refinery analytic shadow pass missing",
        failures,
    )
    require(
        int(stats.get("hero_refinery_analytic_shadow_casters", 0)) >= 4,
        "hero refinery analytic shadow caster count too low",
        failures,
    )
    require(
        float(stats.get("hero_refinery_shadow_max_strength", 0.0)) >= 0.38,
        "hero refinery analytic shadow strength too low",
        failures,
    )
    require(
        int(stats.get("external_model_shadow_instances", 0)) >= 100,
        "external model shadow instances too low",
        failures,
    )
    require(int(stats.get("cinematic_shadow_casters", 0)) >= 16, "cinematic shadow caster count too low", failures)
    require(int(stats.get("shadow_decal_layers", 0)) >= 2, "shadow decal layers missing", failures)
    require(
        stats.get("point_light_falloff") == "range_inverse_square_hybrid",
        "hybrid point-light falloff not reported",
        failures,
    )
    require(stats.get("industrial_hero_scene") == "refinery_night_closeup", "refinery hero scene not reported", failures)
    require(
        stats.get("cinematic_refinery_pass") == "foreground_pipe_canyon_warm_dusk_lights",
        "cinematic refinery foreground pass not reported",
        failures,
    )
    require(
        stats.get("lighting_camera_style") == "inside_refinery_pipe_canyon",
        "inside-refinery lighting camera style not reported",
        failures,
    )
    require(
        stats.get("lighting_camera_refinement_pass") == "lower_pipe_canyon_refinery_composition",
        "lighting camera refinement pass missing",
        failures,
    )
    require(
        stats.get("lighting_composition_fill_pass") == "refinery_occupancy_tighter_pipe_canyon",
        "lighting composition fill pass missing",
        failures,
    )
    require(
        stats.get("lighting_second_composition_pass") == "left_shifted_pipe_canyon_less_right_hillside",
        "lighting second composition pass missing",
        failures,
    )
    require(
        stats.get("lighting_third_composition_pass") == "left_pipe_canyon_crop_reduced_hillside",
        "lighting third composition pass missing",
        failures,
    )
    require(
        stats.get("lighting_fourth_composition_pass") == "closer_pipe_canyon_less_empty_hillside",
        "lighting fourth composition pass missing",
        failures,
    )
    require(
        stats.get("lighting_fifth_composition_pass") == "pipe_canyon_crop_hides_right_hardstand_slope",
        "lighting fifth composition pass missing",
        failures,
    )
    require(
        stats.get("lighting_sixth_composition_pass")
        == "raised_pipe_canyon_eye_removes_foreground_terrain_occlusion",
        "lighting sixth composition pass missing",
        failures,
    )
    require(
        stats.get("lighting_seventh_composition_pass") == "dense_refinery_crop_physical_hardstand",
        "lighting seventh composition pass missing",
        failures,
    )
    require(
        stats.get("lighting_eighth_composition_pass") == "centered_twin_tower_pipe_canyon_depth",
        "lighting eighth composition pass missing",
        failures,
    )
    if preset == "lighting":
        require(
            80.0 <= float(stats.get("lighting_camera_eye_height_m", 0.0)) <= 84.0,
            "lighting camera eye height should match dense refinery crop",
            failures,
        )
        require(
            35.0 <= float(stats.get("lighting_camera_pipe_canyon_fov_deg", 0.0)) <= 37.0,
            "lighting pipe-canyon FOV should match dense refinery crop",
            failures,
        )
    require(
        stats.get("lighting_practical_color_grade_pass") == "warm_tungsten_diffuse_specular_volume_catch",
        "warm practical-light color grade missing",
        failures,
    )
    require(
        stats.get("lighting_night_metal_regrade_pass")
        == "dark_brushed_steel_retained_practical_highlights",
        "night metal regrade missing",
        failures,
    )
    require(
        stats.get("lighting_blue_hour_exposure_pass")
        == "physical_surfaces_cool_shadow_grade_emissive_protected",
        "blue-hour exposure pass missing",
        failures,
    )
    require(stats.get("lighting_emissive_grade_protection") is True, "emissive grade protection missing", failures)
    require(
        stats.get("lighting_sky_structure_pass") == "blue_hour_backdrop_soft_fbm_storm_ceiling",
        "lighting sky structure pass missing",
        failures,
    )
    require(int(stats.get("cinematic_foreground_pipes", 0)) >= 24, "cinematic foreground pipe count too low", failures)
    require(int(stats.get("cinematic_foreground_catwalks", 0)) >= 5, "cinematic foreground catwalk count too low", failures)
    require(int(stats.get("cinematic_foreground_towers", 0)) >= 3, "cinematic foreground tower count too low", failures)
    require(
        int(stats.get("cinematic_foreground_light_markers", 0)) >= 40,
        "cinematic foreground warm light markers too sparse",
        failures,
    )
    require(int(stats.get("cinematic_tower_lamp_markers", 0)) >= 48, "cinematic tower lamp bands missing", failures)
    require(int(stats.get("cinematic_foreground_yard_pads", 0)) >= 3, "cinematic foreground yard pads missing", failures)
    require(
        stats.get("cinematic_foreground_service_deck_pass")
        == "near_hardstand_replaces_green_foreground_slope",
        "cinematic foreground service deck pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_foreground_service_deck_pads", 0)) >= 4,
        "cinematic foreground service deck pads missing",
        failures,
    )
    require(
        int(stats.get("cinematic_foreground_service_deck_joints", 0)) >= 32,
        "cinematic foreground service deck joints too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_foreground_service_deck_drains", 0)) >= 12,
        "cinematic foreground service deck drains too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_foreground_service_deck_rails", 0)) >= 18,
        "cinematic foreground service deck rails too sparse",
        failures,
    )
    require(
        stats.get("cinematic_hardstand_apron_pass") == "expanded_refinery_pads_retaining_edges",
        "cinematic hardstand apron pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_expansion_pads", 0)) >= 5,
        "cinematic hardstand expansion pads missing",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_retaining_edges", 0)) >= 12,
        "cinematic hardstand retaining edges missing",
        failures,
    )
    require(
        stats.get("cinematic_hardstand_apron_surface_detail_pass") == "sawcuts_scuppers_toe_riprap",
        "cinematic hardstand apron surface detail pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_apron_sawcuts", 0)) >= 24,
        "cinematic hardstand apron sawcuts too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_apron_scuppers", 0)) >= 8,
        "cinematic hardstand apron scuppers too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_toe_riprap", 0)) >= 36,
        "cinematic hardstand toe riprap too sparse",
        failures,
    )
    require(
        stats.get("cinematic_hardstand_apron_fascia_breakup_pass")
        == "bands_panel_breaks_pipe_runs_grates_guardrail",
        "cinematic hardstand apron fascia breakup pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_fascia_bands", 0)) >= 5,
        "cinematic hardstand fascia bands missing",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_fascia_panel_breaks", 0)) >= 24,
        "cinematic hardstand fascia panel breaks too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_fascia_pipe_runs", 0)) >= 10,
        "cinematic hardstand fascia pipe runs too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_fascia_drain_grates", 0)) >= 14,
        "cinematic hardstand fascia drain grates too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_fascia_guard_posts", 0)) >= 24,
        "cinematic hardstand fascia guard posts too sparse",
        failures,
    )
    require(
        stats.get("cinematic_hardstand_retaining_service_access_pass")
        == "panels_switchback_steps_cable_trays_toe_shadows",
        "cinematic hardstand retaining service access pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_retaining_panels", 0)) >= 28,
        "cinematic hardstand retaining panels too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_switchback_steps", 0)) >= 22,
        "cinematic hardstand switchback steps too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_cable_tray_spans", 0)) >= 8,
        "cinematic hardstand cable tray spans too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_toe_shadow_bands", 0)) >= 6,
        "cinematic hardstand toe shadow bands too sparse",
        failures,
    )
    require(
        stats.get("cinematic_hardstand_edge_blend_pass") == "low_contrast_shoulders_thin_retaining_edges",
        "cinematic hardstand edge blend pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_edge_blend_strips", 0)) >= 12,
        "cinematic hardstand edge blend strips missing",
        failures,
    )
    require(
        stats.get("cinematic_hardstand_tonal_regrade_pass")
        == "low_luma_panel_fields_service_lanes_runoff_strata",
        "cinematic hardstand tonal regrade pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_tonal_panels", 0)) >= 22,
        "cinematic hardstand tonal panels too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_runoff_stains", 0)) >= 34,
        "cinematic hardstand runoff stains too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_service_lane_bands", 0)) >= 10,
        "cinematic hardstand service lane bands too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_hardstand_retaining_strata", 0)) >= 28,
        "cinematic hardstand retaining strata too sparse",
        failures,
    )
    require(
        stats.get("cinematic_hardstand_shader_whiteout_clamp") is True,
        "cinematic hardstand shader whiteout clamp missing",
        failures,
    )
    require(
        stats.get("cinematic_foreground_deck_whiteout_clamp_pass") == "low_luma_oil_stained_service_deck_shader",
        "cinematic foreground deck whiteout clamp pass missing",
        failures,
    )
    require(
        stats.get("cinematic_foreground_deck_shader_whiteout_clamp") is True,
        "cinematic foreground deck shader whiteout clamp missing",
        failures,
    )
    require(
        stats.get("cinematic_right_slope_industrialization_pass") == "terraced_service_road_retaining_walls",
        "cinematic right slope industrialization pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_right_slope_terraces", 0)) >= 7,
        "cinematic right slope terraces missing",
        failures,
    )
    require(
        int(stats.get("cinematic_right_slope_retaining_walls", 0)) >= 14,
        "cinematic right slope retaining walls too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_right_slope_service_roads", 0)) >= 4,
        "cinematic right slope service roads missing",
        failures,
    )
    require(
        stats.get("cinematic_right_slope_detail_pass") == "panel_joints_riprap_drains_guardrails",
        "cinematic right slope detail pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_right_slope_panel_joints", 0)) >= 18,
        "cinematic right slope panel joints too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_right_slope_riprap_stones", 0)) >= 42,
        "cinematic right slope riprap stones too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_right_slope_drainage_runs", 0)) >= 6,
        "cinematic right slope drainage runs too sparse",
        failures,
    )
    require(
        int(stats.get("cinematic_right_slope_guardrails", 0)) >= 10,
        "cinematic right slope guardrails too sparse",
        failures,
    )
    require(
        stats.get("vegetation_refinery_clearance_pass") == "hero_hardstand_tree_cull",
        "vegetation refinery clearance pass missing",
        failures,
    )
    require(
        stats.get("cinematic_foreground_slope_fill_pass") == "near_terrain_shadow_lift_far_mountains_preserved",
        "cinematic foreground slope fill pass missing",
        failures,
    )
    require(
        stats.get("cinematic_foreground_slope_fill_shader") is True,
        "cinematic foreground slope fill shader missing",
        failures,
    )
    require(
        stats.get("cinematic_foreground_terrain_regrade_pass")
        == "lighting_near_hardstand_mask_replaces_green_foreground_slope",
        "cinematic foreground terrain regrade pass missing",
        failures,
    )
    require(
        stats.get("cinematic_foreground_terrain_regrade_shader") is True,
        "cinematic foreground terrain regrade shader missing",
        failures,
    )
    require(
        stats.get("cinematic_refinery_footprint_dom_clamp_pass") == "dark_hardstand_replaces_binhai_white_dom_patch",
        "cinematic refinery footprint DOM clamp pass missing",
        failures,
    )
    require(
        stats.get("cinematic_refinery_footprint_dom_shader") is True,
        "cinematic refinery footprint DOM shader missing",
        failures,
    )
    require(
        stats.get("cinematic_far_terrain_skyline_cleanup_pass")
        == "lighting_horizon_aerial_blend_no_diagonal_backdrop",
        "cinematic far-terrain skyline cleanup pass missing",
        failures,
    )
    require(stats.get("far_terrain_skyline_blend_shader") is True, "far terrain skyline blend shader missing", failures)
    require(
        float(stats.get("far_terrain_skyline_blend_start_m", 9999.0)) <= 780.0,
        "far terrain skyline blend starts too late",
        failures,
    )
    require(
        float(stats.get("far_terrain_skyline_blend_end_m", 9999.0)) <= 1450.0,
        "far terrain skyline blend ends too late",
        failures,
    )
    require(int(stats.get("cinematic_storm_cloud_puffs", 0)) >= 30, "cinematic storm cloud layer too sparse", failures)
    require(
        stats.get("distant_refinery_skyline_pass") == "layered_towers_stacks_racks_vapor",
        "distant refinery skyline pass missing",
        failures,
    )
    require(
        int(stats.get("distant_refinery_skyline_towers", 0)) >= 28,
        "distant refinery skyline towers too sparse",
        failures,
    )
    require(int(stats.get("distant_refinery_stacks", 0)) >= 18, "distant refinery stacks too sparse", failures)
    require(
        int(stats.get("distant_refinery_rack_frames", 0)) >= 42,
        "distant refinery rack frames too sparse",
        failures,
    )
    require(
        int(stats.get("distant_refinery_pipe_bridges", 0)) >= 18,
        "distant refinery pipe bridges too sparse",
        failures,
    )
    require(
        int(stats.get("distant_refinery_vapor_plumes", 0)) >= 30,
        "distant refinery vapor plumes too sparse",
        failures,
    )
    require(int(stats.get("distant_refinery_depth_layers", 0)) >= 3, "distant refinery depth layers missing", failures)
    require(
        stats.get("distant_refinery_site_pass") == "graded_pad_bands_service_roads_retaining_edges",
        "distant refinery graded site pass missing",
        failures,
    )
    require(int(stats.get("distant_refinery_pad_bands", 0)) >= 3, "distant refinery pad bands missing", failures)
    require(
        int(stats.get("distant_refinery_service_roads", 0)) >= 6,
        "distant refinery service roads too sparse",
        failures,
    )
    require(
        int(stats.get("distant_refinery_retaining_walls", 0)) >= 24,
        "distant refinery retaining walls too sparse",
        failures,
    )
    require(int(stats.get("distant_refinery_light_count", 0)) >= 90, "distant refinery light field too sparse", failures)
    require(int(stats.get("hero_process_towers", 0)) >= 3, "hero process towers missing", failures)
    require(int(stats.get("hero_catwalk_levels", 0)) >= 12, "hero catwalk levels missing", failures)
    require(int(stats.get("hero_pipe_segments", 0)) >= 35, "hero pipe density too low", failures)
    require(int(stats.get("hero_lamp_markers", 0)) >= 24, "hero lamp markers too sparse", failures)
    require(stats.get("window_material_channel") == "stable_lit_surface", "stable window material channel not reported", failures)
    require(
        stats.get("industrial_ground_detail_pass") == "curbs_yard_decals_tire_wear",
        "industrial ground detail pass not reported",
        failures,
    )
    require(
        stats.get("industrial_ground_tone_regrade_pass") == "muted_coastal_concrete_low_whiteout",
        "industrial ground tone regrade pass not reported",
        failures,
    )
    require(stats.get("yard_surface_pad_luma_reduced") is True, "yard surface pad luma reduction missing", failures)
    require(
        stats.get("road_marking_profile") == "worn_low_luma_thin_paint",
        "worn low-luma road marking profile not reported",
        failures,
    )
    require(float(stats.get("road_marking_max_luma", 1.0)) <= 0.56, "road marking luma too high", failures)
    require(
        stats.get("road_marking_overlay_regrade_pass") == "thin_worn_paint_low_alpha_shader",
        "road marking overlay regrade pass not reported",
        failures,
    )
    require(float(stats.get("road_marking_overlay_alpha_max", 1.0)) <= 0.42, "road marking overlay alpha too high", failures)
    require(int(stats.get("yard_surface_pads", 0)) >= 4, "yard surface pads missing", failures)
    require(int(stats.get("roadside_curb_segments", 0)) >= 100, "roadside curb segment count too low", failures)
    require(int(stats.get("road_wear_decals", 0)) >= 40, "road wear decal count too low", failures)
    require(int(stats.get("yard_oil_stains", 0)) >= 12, "yard oil stain decals missing", failures)
    require(int(stats.get("safety_chevron_markers", 0)) >= 18, "safety chevron markers missing", failures)
    require(
        int(stats.get("yard_expansion_joint_decals", 0)) >= 30,
        "yard expansion joint decals missing",
        failures,
    )
    require(
        stats.get("converted_asset_display_pass") == "hardstand_plinths_bollards_cable_trays_lights",
        "converted asset display yard pass not reported",
        failures,
    )
    require(
        int(stats.get("converted_asset_display_pads", 0)) >= 5,
        "converted asset display hardstand pads missing",
        failures,
    )
    require(
        int(stats.get("converted_asset_display_bollards", 0)) >= 18,
        "converted asset display bollards too sparse",
        failures,
    )
    require(
        int(stats.get("converted_asset_display_rails", 0)) >= 12,
        "converted asset display guard rails too sparse",
        failures,
    )
    require(
        int(stats.get("converted_asset_display_cable_runs", 0)) >= 7,
        "converted asset display cable runs missing",
        failures,
    )
    require(
        int(stats.get("converted_asset_display_light_posts", 0)) >= 4,
        "converted asset display light posts missing",
        failures,
    )
    require(
        int(stats.get("converted_asset_display_screen_widgets", 0)) >= 14,
        "converted asset display screen widgets missing",
        failures,
    )
    require(
        int(stats.get("converted_asset_display_control_cabinets", 0)) >= 3,
        "converted asset display control cabinets missing",
        failures,
    )
    require(
        stats.get("perimeter_fence_style") == "low_contrast_metal_gate_mesh",
        "low-contrast perimeter fence style not reported",
        failures,
    )
    require(int(stats.get("perimeter_fence_posts", 0)) >= 60, "perimeter fence posts missing", failures)
    require(int(stats.get("perimeter_gate_count", 0)) >= 1, "perimeter gate detail missing", failures)
    require(stats.get("perimeter_guardhouse") is True, "perimeter guardhouse not reported", failures)
    require(stats.get("tree_contact_shadows") is True, "tree contact shadows not reported", failures)
    require(
        stats.get("vegetation_style") == "mixed_deciduous_conifer_layered",
        "mixed layered vegetation style not reported",
        failures,
    )
    require(
        stats.get("vegetation_detail_pass") == "species_clusters_understory_lod",
        "vegetation detail pass not reported",
        failures,
    )
    require(
        stats.get("vegetation_species_mix") == "conifer_deciduous_shrub",
        "vegetation species mix not reported",
        failures,
    )
    require(
        stats.get("vegetation_canopy_style") == "offset_multilobe_crowns",
        "offset multilobe tree crowns not reported",
        failures,
    )
    require(stats.get("understory_shrub_layer") is True, "understory shrub layer not reported", failures)
    require(
        int(stats.get("understory_shrub_count", 0)) >= 90,
        "understory shrub count too low",
        failures,
    )
    require(int(stats.get("vegetation_shadow_layers", 0)) >= 2, "vegetation shadow layering not reported", failures)
    require(
        stats.get("tree_shadow_style") == "sun_oriented_contact_ellipses",
        "sun-oriented tree shadows not reported",
        failures,
    )
    require(int(stats.get("external_gltf_instances", 0)) >= 100, "runtime glTF asset set was not loaded densely", failures)
    require(
        stats.get("huilian_asset_intake_pass") == "user_huilian_fbx_runtime_lod_and_legacy_dcc_export_queue",
        "Huilian asset intake pass not reported",
        failures,
    )
    require(int(stats.get("huilian_asset_total_files", 0)) == 1602, "Huilian total file count mismatch", failures)
    require(
        int(stats.get("huilian_asset_total_bytes", 0)) >= 1_963_000_000,
        "Huilian source byte inventory too small",
        failures,
    )
    require(int(stats.get("huilian_model_source_files", 0)) == 460, "Huilian model source count mismatch", failures)
    require(int(stats.get("huilian_max_source_files", 0)) == 177, "Huilian MAX source count mismatch", failures)
    require(int(stats.get("huilian_fbx_source_files", 0)) == 1, "Huilian FBX source count mismatch", failures)
    require(
        int(stats.get("huilian_legacy_package_files", 0)) == 282,
        "Huilian legacy package count mismatch",
        failures,
    )
    require(int(stats.get("huilian_texture_files", 0)) == 1005, "Huilian texture count mismatch", failures)
    require(
        stats.get("huilian_legacy_runtime_policy") == "matching_max_export_or_verified_decoder_then_gltf_lod",
        "Huilian legacy package policy is not explicit",
        failures,
    )
    require(
        stats.get("huilian_valve_catalog_asset_pass")
        == "fbx_assimp_pretransform_wz_check_valve_runtime_manifold",
        "Huilian valve catalog runtime pass not reported",
        failures,
    )
    require(
        stats.get("huilian_equipment_asset_pass")
        == "fbx_source_mesh_filter_valve_pump_runtime_pbr_inspection",
        "Huilian valve/pump runtime equipment pass not reported",
        failures,
    )
    require(
        int(stats.get("huilian_equipment_runtime_instances", 0)) >= 2,
        "converted Huilian valve and pump were not both loaded",
        failures,
    )
    require(
        int(stats.get("huilian_equipment_direct_runtime_assets", 0)) == 2,
        "Huilian direct runtime asset count mismatch",
        failures,
    )
    require(
        int(stats.get("huilian_equipment_runtime_triangles", 0)) == 912,
        "Huilian runtime equipment triangle total mismatch",
        failures,
    )
    require(
        int(stats.get("huilian_equipment_pbr_materials", 0)) >= 2,
        "Huilian explicit PBR equipment materials missing",
        failures,
    )
    require(
        int(stats.get("huilian_valve_catalog_instances", 0)) >= 1,
        "converted Huilian valve catalog was not loaded",
        failures,
    )
    require(
        int(stats.get("huilian_valve_catalog_geometries", 0)) == 1625,
        "Huilian valve geometry count mismatch",
        failures,
    )
    require(
        int(stats.get("huilian_valve_catalog_materials", 0)) >= 380,
        "Huilian valve material coverage too low",
        failures,
    )
    require(
        int(stats.get("huilian_valve_catalog_source_triangles", 0)) == 135621,
        "Huilian valve source triangle count mismatch",
        failures,
    )
    require(
        int(stats.get("huilian_valve_catalog_output_triangles", 0)) == 400,
        "Huilian valve runtime LOD triangle count mismatch",
        failures,
    )
    require(
        stats.get("huilian_valve_catalog_lod")
        == "assimp_pretransform_center_ground_named_wz_mesh_lossless",
        "Huilian valve LOD policy mismatch",
        failures,
    )
    require(int(stats.get("huilian_valve_manifold_pipe_runs", 0)) >= 2, "Huilian manifold pipe runs missing", failures)
    require(int(stats.get("huilian_valve_manifold_supports", 0)) >= 4, "Huilian manifold supports missing", failures)
    require(
        int(stats.get("huilian_production_water_pump_instances", 0)) == 1,
        "converted Huilian production-water pump was not loaded exactly once",
        failures,
    )
    require(
        stats.get("huilian_production_water_pump_runtime_asset")
        == "assets/showcase/runtime/huilian_production_water_pump_p0401_runtime.glb",
        "Huilian production-water pump runtime asset mismatch",
        failures,
    )
    require(
        stats.get("huilian_production_water_pump_source_mesh") == "生产水泵P-0401",
        "Huilian production-water pump source mesh mismatch",
        failures,
    )
    require(
        int(stats.get("huilian_production_water_pump_runtime_meshes", 0)) == 1
        and int(stats.get("huilian_production_water_pump_output_triangles", 0)) == 512,
        "Huilian production-water pump geometry counts mismatch",
        failures,
    )
    require(
        stats.get("huilian_production_water_pump_geometry_source_real") is True,
        "Huilian production-water pump must be reported as real source geometry",
        failures,
    )
    require(
        stats.get("huilian_production_water_pump_source_texture_available") is False,
        "Huilian production-water pump must disclose the missing original source texture",
        failures,
    )
    require(
        stats.get("huilian_production_water_pump_material_policy")
        == "real_geometry_legacy_diffuse_proxy_explicit_metallic_roughness",
        "Huilian production-water pump material disclosure mismatch",
        failures,
    )
    require(
        stats.get("xineng_source_model_asset_pass") == "peilou_fbx_converted_from_user_max_asset",
        "Xineng source model conversion pass not reported",
        failures,
    )
    require(
        int(stats.get("xineng_source_model_instances", 0)) >= 1,
        "converted Xineng source model was not loaded",
        failures,
    )
    require(
        int(stats.get("xineng_source_model_triangle_count", 0)) >= 500000,
        "Xineng peilou source model triangle budget not reported",
        failures,
    )
    require(int(stats.get("xineng_max_source_asset_count", 0)) >= 45, "MAX source asset inventory too small", failures)
    require(int(stats.get("xineng_fbx_source_asset_count", 0)) >= 7, "FBX source asset inventory too small", failures)
    require(
        stats.get("xineng_site_max_catalog_pass") == "site_terrain_factory_power_safety_security_catalog_overlay",
        "Xineng site MAX catalog overlay pass not reported",
        failures,
    )
    require(
        int(stats.get("xineng_site_max_catalog_assets", 0)) >= 19,
        "Xineng site MAX catalog asset count too small",
        failures,
    )
    require(
        int(stats.get("xineng_site_max_heavy_assets", 0)) >= 9,
        "Xineng site MAX heavy asset count too small",
        failures,
    )
    require(
        int(stats.get("xineng_site_max_catalog_bytes", 0)) >= 1000000000,
        "Xineng site MAX selected byte count too small",
        failures,
    )
    require(
        int(stats.get("xineng_site_max_role_groups", 0)) >= 6,
        "Xineng site MAX role grouping missing",
        failures,
    )
    require(
        stats.get("xineng_site_max_direct_load_policy") == "max_requires_dcc_export_or_fbx_intermediate",
        "Xineng site MAX direct-load policy not reported",
        failures,
    )
    require(
        stats.get("xineng_site_max_runtime_use_mode") == "gltf_lod_and_procedural_proxy_staging",
        "Xineng site MAX runtime use mode not reported",
        failures,
    )
    require(
        stats.get("xineng_source_asset_coverage_pass") == "all_max_fbx_sources_mapped_to_runtime_capability_layers",
        "Xineng source asset coverage audit pass not reported",
        failures,
    )
    require(
        int(stats.get("xineng_source_asset_coverage_model_assets", 0)) == 52,
        "Xineng source asset coverage should account for all MAX/FBX model assets",
        failures,
    )
    require(
        int(stats.get("xineng_source_asset_coverage_covered_assets", 0)) == 52,
        "Xineng source asset coverage should report all model assets covered",
        failures,
    )
    require(
        int(stats.get("xineng_source_asset_coverage_max_files", 0)) == 45,
        "Xineng source asset coverage MAX count mismatch",
        failures,
    )
    require(
        int(stats.get("xineng_source_asset_coverage_fbx_files", 0)) == 7,
        "Xineng source asset coverage FBX count mismatch",
        failures,
    )
    require(
        int(stats.get("xineng_source_asset_coverage_model_bytes", 0)) >= 1726000000,
        "Xineng source asset coverage model byte total too small",
        failures,
    )
    require(
        float(stats.get("xineng_source_asset_coverage_percent", 0.0)) >= 100.0,
        "Xineng source asset coverage should be complete",
        failures,
    )
    require(
        int(stats.get("xineng_source_asset_coverage_unreferenced_assets", -1)) == 0,
        "Xineng source asset coverage reports unreferenced model sources",
        failures,
    )
    require(
        stats.get("xineng_warehouse_lod_asset_pass") == "warehouse_rack_fbx_even_triangle_sampling_lod",
        "Xineng warehouse rack LOD pass not reported",
        failures,
    )
    require(
        int(stats.get("xineng_warehouse_lod_instances", 0)) >= 3,
        "Xineng warehouse rack LOD instances too sparse",
        failures,
    )
    require(
        int(stats.get("xineng_warehouse_lod_source_triangle_count", 0)) >= 4000000,
        "Xineng warehouse source triangle count too low",
        failures,
    )
    require(
        int(stats.get("xineng_warehouse_lod_output_triangle_count", 999999)) <= 60000,
        "Xineng warehouse LOD output triangle count too high",
        failures,
    )
    require(
        float(stats.get("xineng_warehouse_lod_reduction_ratio", 1.0)) <= 0.02,
        "Xineng warehouse LOD reduction ratio too high",
        failures,
    )
    require(
        stats.get("xineng_asset_inspection_pass") == "warehouse_lod_closeup_selection_overlay",
        "Xineng asset inspection overlay pass not reported",
        failures,
    )
    require(
        stats.get("xineng_asset_inspection_preset") == "warehouse",
        "Xineng asset inspection preset not reported",
        failures,
    )
    require(
        stats.get("xineng_asset_inspection_lod_strategy") == "source_count_reported_runtime_triangle_cap",
        "Xineng asset inspection LOD strategy not reported",
        failures,
    )
    require(
        stats.get("xineng_asset_impact_analysis_pass") == "warehouse_lod_process_safety_impact_overlay",
        "Xineng asset impact-analysis pass not reported",
        failures,
    )
    require(
        stats.get("xineng_asset_impact_source_role") == "warehouse_logistics_rack_lod",
        "Xineng asset impact-analysis source role not reported",
        failures,
    )
    require(
        stats.get("render_efficiency_pass") == "lod_stress_frame_budget_telemetry_overlay",
        "render efficiency telemetry pass not reported",
        failures,
    )
    require(
        int(stats.get("render_efficiency_target_fps", 0)) >= 60,
        "render efficiency target FPS too low",
        failures,
    )
    require(
        float(stats.get("render_efficiency_frame_budget_ms", 99.0)) <= 16.8,
        "render efficiency frame budget too loose",
        failures,
    )
    require(
        int(stats.get("render_efficiency_lod_source_triangles", 0)) >= 4000000,
        "render efficiency LOD source triangle count missing",
        failures,
    )
    require(
        int(stats.get("render_efficiency_lod_output_triangles", 999999)) <= 60000,
        "render efficiency LOD output triangle count too high",
        failures,
    )
    require(
        float(stats.get("render_efficiency_lod_reduction_ratio", 1.0)) <= 0.02,
        "render efficiency LOD reduction ratio too high",
        failures,
    )
    require(
        int(stats.get("render_efficiency_model_stress_instances", 0)) >= 1000,
        "render efficiency model stress profile missing",
        failures,
    )
    require(
        int(stats.get("render_efficiency_particle_stress_instances", 0)) >= 50000,
        "render efficiency particle stress profile missing",
        failures,
    )
    require(
        stats.get("render_efficiency_draw_call_model") == "single_indexed_scene_batch_with_material_kind_channels",
        "render efficiency draw-call model not reported",
        failures,
    )
    require(stats.get("render_efficiency_single_indexed_draw") is True, "single indexed draw not reported", failures)
    require(
        int(stats.get("render_efficiency_material_kind_channels", 0)) >= 5,
        "render efficiency material-kind channels too sparse",
        failures,
    )
    require(
        int(stats.get("render_efficiency_push_constant_bytes", 999)) <= 128,
        "render efficiency push constants exceed Vulkan minimum budget",
        failures,
    )
    require(
        int(stats.get("render_efficiency_descriptor_sets", 0)) == 1,
        "render efficiency descriptor set count should remain compact",
        failures,
    )
    require(stats.get("render_efficiency_runtime_fps_title") is True, "runtime FPS title telemetry missing", failures)
    require(
        stats.get("render_postfx_stack_pass") == "hdr_ssao_bloom_tonemap_fxaa_colorgrade_budget_stack",
        "render post-FX stack pass not reported",
        failures,
    )
    require(
        int(stats.get("render_postfx_stack_stages", 0)) >= 9,
        "render post-FX stack stages too sparse",
        failures,
    )
    require(int(stats.get("render_postfx_hdr_targets", 0)) >= 2, "render post-FX HDR targets missing", failures)
    require(stats.get("render_postfx_auto_exposure") is True, "render post-FX auto exposure missing", failures)
    require(
        int(stats.get("render_postfx_ssao_samples", 0)) >= 32,
        "render post-FX SSAO sample budget too low",
        failures,
    )
    require(
        int(stats.get("render_postfx_bloom_mips", 0)) >= 5,
        "render post-FX bloom mip chain too short",
        failures,
    )
    require(
        int(stats.get("render_postfx_tonemap_operators", 0)) >= 5,
        "render post-FX tone-map operator set too small",
        failures,
    )
    require(
        stats.get("render_postfx_tonemap_modes") == "none_aces_agx_filmic_reinhard",
        "render post-FX tone-map modes not reported",
        failures,
    )
    require(int(stats.get("render_postfx_fxaa_taps", 0)) >= 5, "render post-FX FXAA taps missing", failures)
    require(
        int(stats.get("render_postfx_grade_controls", 0)) >= 8,
        "render post-FX color grading controls too sparse",
        failures,
    )
    require(
        stats.get("render_fullscreen_antialiasing_pass") == "2x_supersample_box_resolve_for_showcase_screenshots",
        "fullscreen antialiasing resolve pass missing",
        failures,
    )
    require(
        stats.get("render_fullscreen_antialiasing_active") is True,
        "fullscreen antialiasing not active",
        failures,
    )
    require(
        int(stats.get("render_fullscreen_antialiasing_samples", 0)) >= 4,
        "fullscreen antialiasing sample count too low",
        failures,
    )
    require(
        stats.get("render_resource_budget_pass") == "gpu_memory_residency_streaming_upload_heap_audit",
        "render resource budget pass not reported",
        failures,
    )
    require(
        stats.get("render_resource_budget_runtime_mode") == "performance_preset_gpu_resource_budget_overlay",
        "render resource budget runtime mode not reported",
        failures,
    )
    require(
        int(stats.get("render_resource_budget_gpu_budget_mb", 0)) >= 4096,
        "render resource GPU budget too low",
        failures,
    )
    require(
        int(stats.get("render_resource_budget_resident_mb", 99999))
        <= int(stats.get("render_resource_budget_gpu_budget_mb", 0)),
        "render resource resident memory exceeds GPU budget",
        failures,
    )
    require(
        float(stats.get("render_resource_budget_headroom_percent", 0.0)) >= 60.0,
        "render resource budget headroom too low",
        failures,
    )
    require(
        int(stats.get("render_resource_budget_source_folder_bytes", 0)) >= 2200000000,
        "render resource source folder byte coverage missing",
        failures,
    )
    require(
        int(stats.get("render_resource_budget_source_model_bytes", 0)) >= 1720000000,
        "render resource source model byte coverage missing",
        failures,
    )
    require(
        int(stats.get("render_resource_budget_source_texture_bytes", 0)) >= 482000000,
        "render resource source texture byte coverage missing",
        failures,
    )
    require(
        stats.get("render_resource_budget_tracks_whole_plant_streaming") is True,
        "render resource budget should track whole-plant streaming",
        failures,
    )
    require(
        stats.get("xineng_whole_plant_max_streaming_pass")
        == "whole_plant_max_partition_lod_streaming_budget_proxy",
        "Xineng whole-plant MAX streaming pass not reported",
        failures,
    )
    require(
        stats.get("xineng_whole_plant_max_streaming_runtime_mode")
        == "procedural_streaming_proxy_until_dcc_export_partition",
        "Xineng whole-plant MAX streaming runtime mode not reported",
        failures,
    )
    require(
        int(stats.get("xineng_whole_plant_max_streaming_source_assets", 0)) >= 6,
        "Xineng whole-plant MAX streaming source asset count missing",
        failures,
    )
    require(
        int(stats.get("xineng_whole_plant_max_streaming_source_asset_bytes", 0)) >= 843000000,
        "Xineng whole-plant MAX streaming source byte budget missing",
        failures,
    )
    require(
        int(stats.get("xineng_whole_plant_max_streaming_root_asset_bytes", 0)) >= 608000000,
        "Xineng whole-plant MAX streaming root asset byte budget missing",
        failures,
    )
    require(
        int(stats.get("xineng_whole_plant_max_streaming_resident_budget_mb", 0)) >= 256,
        "Xineng whole-plant MAX streaming resident budget missing",
        failures,
    )
    require(
        stats.get("emergency_drill_pass") == "leak_to_fire_integrated_response_timeline_overlay",
        "emergency drill integrated pass not reported",
        failures,
    )
    require(
        stats.get("emergency_drill_scenario_runtime") == "data/scenarios/leak_to_fire.json",
        "emergency drill runtime scenario path not reported",
        failures,
    )
    require(
        stats.get("emergency_drill_local_scenario") == "data/scenarios/leak_to_fire_local.json",
        "emergency drill local scenario path not reported",
        failures,
    )
    require(
        int(stats.get("emergency_drill_duration_seconds", 0)) == 45,
        "emergency drill duration does not match scenario",
        failures,
    )
    require(
        int(stats.get("emergency_drill_scenario_annotations", 0)) >= 4,
        "emergency drill scenario annotations missing",
        failures,
    )
    require(
        int(stats.get("emergency_drill_scenario_tracks", 0)) >= 7,
        "emergency drill scenario tracks missing",
        failures,
    )
    require(
        stats.get("emergency_drill_decision_audit_pass") == "sensor_alarm_command_ack_closed_loop",
        "emergency drill decision audit pass not reported",
        failures,
    )
    require(int(stats.get("cloud_clusters", 0)) >= 2, "cloud cluster telemetry missing", failures)
    require(stats.get("cloud_style") == "layered_horizon_banks", "layered horizon cloud banks not reported", failures)
    require(int(stats.get("cloud_puffs", 0)) >= 30, "cloud bank telemetry too low", failures)
    require(stats.get("cloud_dark_undersides") is True, "dark cloud undersides not reported", failures)
    require(stats.get("sky_procedural_cloud_shader") is True, "procedural sky cloud shader not reported", failures)
    require(
        stats.get("cinematic_sky_grade_pass") == "cold_storm_zenith_warm_horizon",
        "cinematic sky grade pass missing",
        failures,
    )
    require(
        stats.get("sky_cloud_contrast_profile") == "layered_storm_bands",
        "layered storm-band sky contrast missing",
        failures,
    )
    require(stats.get("sky_horizon_warmth") is True, "warm horizon sky grade missing", failures)
    require(
        stats.get("cinematic_sky_cleanup_pass") == "fbm_smooth_shelf_no_blocky_cloud_mesh",
        "cinematic sky cleanup pass missing",
        failures,
    )
    require(
        stats.get("sky_cloud_geometry_profile") == "procedural_only_no_mesh_clouds",
        "procedural-only no-mesh cloud profile not reported",
        failures,
    )
    require(
        float(stats.get("sky_mesh_cloud_alpha_max", 1.0)) <= 0.08,
        "sky mesh cloud alpha too high for lighting preset cleanup",
        failures,
    )
    require(int(stats.get("sky_procedural_cloud_fbm_octaves", 0)) >= 4, "sky FBM octave count too low", failures)
    require(stats.get("sky_storm_zenith_regrade") is True, "storm zenith sky regrade missing", failures)
    require(int(stats.get("cinematic_sky_shelf_bands", 0)) >= 5, "cinematic sky shelf bands too sparse", failures)
    require(
        stats.get("cinematic_sky_depth_pass") == "layered_storm_shelf_silver_edges",
        "cinematic sky depth pass missing",
        failures,
    )
    require(
        int(stats.get("cinematic_sky_horizon_scud_bands", 0)) >= 7,
        "cinematic sky horizon scud bands too sparse",
        failures,
    )
    require(
        float(stats.get("sky_procedural_cloud_layer_strength", 0.0)) >= 0.42,
        "procedural cloud layer strength too low",
        failures,
    )
    require(stats.get("cinematic_sky_silver_edges") is True, "cinematic sky silver edges missing", failures)
    require(
        stats.get("cinematic_sky_contrast_pass") == "stronger_shelf_anvil_silver_edges_no_screen_space",
        "cinematic sky contrast pass missing",
        failures,
    )
    require(
        stats.get("sky_procedural_cloud_no_screen_space_coords") is True,
        "procedural sky clouds should avoid screen-space coordinate artifacts",
        failures,
    )
    require(
        stats.get("cinematic_sky_mood_pass") == "low_frequency_shelf_warm_break_no_screen_space",
        "cinematic sky mood pass missing",
        failures,
    )
    require(
        stats.get("cinematic_sky_streak_cleanup_pass") == "noise_broken_shelf_no_linear_azimuth_bands",
        "cinematic sky streak cleanup pass missing",
        failures,
    )
    require(
        stats.get("cinematic_sky_directional_cloud_bank_pass")
        == "viewdir_xy_broken_horizon_banks_no_azimuth_streaks",
        "directional sky cloud-bank pass missing",
        failures,
    )
    require(
        stats.get("cinematic_overview_sky_volume_pass") == "raised_cloud_air_depth_for_overview",
        "overview sky volume pass missing",
        failures,
    )
    require(
        float(stats.get("overview_sky_min_cloud_amount", 1.0)) <= 0.02,
        "overview should start with cloud haze disabled",
        failures,
    )
    require(
        stats.get("cinematic_overview_sky_tone_pass") == "cool_blue_gray_daylight_no_pink_haze",
        "overview sky tone pass missing",
        failures,
    )
    require(
        stats.get("sky_directional_cloud_bank_shader") is True,
        "directional sky cloud-bank shader missing",
        failures,
    )
    require(stats.get("sky_linear_azimuth_streaks_removed") is True, "linear azimuth sky streaks not removed", failures)
    require(stats.get("sky_lighting_cloud_artifact_guard") is True, "lighting cloud artifact guard missing", failures)
    guard_strength = float(stats.get("sky_lighting_cloud_artifact_guard_strength", 0.0))
    require(
        0.45 <= guard_strength <= 0.72,
        "lighting cloud artifact guard outside balanced cloud-detail range",
        failures,
    )
    require(
        stats.get("cinematic_lighting_sky_cloud_restoration_pass") == "balanced_procedural_clouds_no_flat_brown_sky",
        "lighting sky cloud restoration pass missing",
        failures,
    )
    require(
        stats.get("cinematic_lighting_dark_ceiling_pass") == "viewdir_fbm_cloud_belly_no_geometry",
        "lighting dark cloud ceiling pass missing",
        failures,
    )
    require(
        stats.get("sky_low_horizon_whiteout_clamp_pass") == "industrial_haze_ceiling_no_pipe_canyon_white_band",
        "sky low horizon whiteout clamp pass missing",
        failures,
    )
    require(
        float(stats.get("sky_low_horizon_whiteout_guard_strength", 0.0)) >= 0.60,
        "sky low horizon whiteout guard too weak",
        failures,
    )
    require(stats.get("sky_low_frequency_shelf_belly") is True, "low-frequency sky shelf belly missing", failures)
    require(stats.get("sky_warm_cloud_break") is True, "warm cloud break missing", failures)
    require(stats.get("sky_cloud_break_rays") is True, "cloud break rays missing", failures)
    require(
        stats.get("postprocess_color_grade") == "filmic_aces_depth_aerial_tint",
        "filmic depth color grade not reported",
        failures,
    )
    require(
        stats.get("cinematic_exposure_grade_pass") == "s_curve_cool_shadows_warm_practicals",
        "cinematic exposure grade pass missing",
        failures,
    )
    require(stats.get("cinematic_exposure_s_curve") is True, "cinematic exposure S-curve missing", failures)
    require(stats.get("cinematic_cool_shadow_tint") is True, "cinematic cool shadow tint missing", failures)
    require(stats.get("cinematic_warm_highlight_bias") is True, "cinematic warm highlight bias missing", failures)
    require(
        stats.get("lighting_pipe_canyon_screen_whiteout_guard_pass") == "post_aces_lower_left_highlight_clamp",
        "lighting pipe canyon screen whiteout guard pass missing",
        failures,
    )
    require(
        float(stats.get("lighting_pipe_canyon_screen_whiteout_guard_strength", 0.0)) >= 0.80,
        "lighting pipe canyon screen whiteout guard too weak",
        failures,
    )
    require(
        stats.get("lighting_lower_left_reflection_clamp_pass") == "low_threshold_gray_white_band_taming",
        "lighting lower-left reflection clamp pass missing",
        failures,
    )
    require(stats.get("aerial_perspective_grade") is True, "aerial perspective grade not reported", failures)
    require(stats.get("tonemap_operator") == "aces_narkowicz", "ACES tone map operator not reported", failures)
    require(int(stats.get("particle_instances", 0)) >= 400, "particle systems too small", failures)
    require(
        stats.get("shadow_mode") in ("analytic_sun_projected", "realtime_sun_projected_analytic"),
        "sun shadow mode missing",
        failures,
    )
    require(float(stats.get("visible_shadow_strength", 0.0)) >= 0.70, "visible shadow strength too low", failures)
    require(int(stats.get("declared_point_lights", 0)) >= 16, "scene point lights were not loaded", failures)
    require(stats.get("gui_controls") is True, "GUI controls not reported", failures)
    require(int(stats.get("gui_slider_count", 0)) >= 10, "GUI sliders not reported", failures)
    require(stats.get("localized_ui") == "zh_CN", "Chinese localized UI not reported", failures)
    require(stats.get("mouse_wheel_mode") == "dolly_forward_back", "mouse wheel dolly mode not reported", failures)
    require(stats.get("skybox") is True, "skybox missing", failures)
    require(stats.get("skybox_mode") == "gradient_sky_dome", "skybox dome mode missing", failures)
    require(stats.get("skybox_edge_free") is True, "edge-free sky sphere not reported", failures)
    require(stats.get("skybox_camera_relative") is True, "camera-relative skybox not reported", failures)
    require(stats.get("skybox_far_clip_guard") is True, "skybox far-clip guard not reported", failures)
    require(
        stats.get("skybox_fragment_direction_pass") == "per_pixel_dome_gradient_no_triangle_interpolation",
        "skybox per-pixel dome gradient pass missing",
        failures,
    )
    require(
        float(stats.get("skybox_vertex_color_weight", 1.0)) <= 0.05,
        "skybox still relies too much on triangle-interpolated vertex color",
        failures,
    )
    require(
        stats.get("sky_sun_disc_geometry") == "procedural_shader_glow_only",
        "low-poly sky sun disc geometry should stay disabled",
        failures,
    )
    require(
        stats.get("skybox_tessellation_pass") == "high_density_procedural_clouds_no_facets",
        "skybox high-density tessellation pass missing",
        failures,
    )
    require(int(stats.get("skybox_rings", 0)) >= 128, "skybox ring count too low for procedural clouds", failures)
    require(int(stats.get("skybox_slices", 0)) >= 384, "skybox slice count too low for procedural clouds", failures)
    require(stats.get("skybox_sun_glow") is True, "skybox sun glow not reported", failures)
    require(stats.get("interactive_markers") is True, "interactive markers missing", failures)
    require(int(stats.get("interactive_marker_count", 0)) >= 6, "interactive marker count too low", failures)
    require(
        stats.get("interactive_marker_style") == "low_ground_beacons",
        "interactive markers should remain low ground beacons",
        failures,
    )
    require(
        stats.get("interactive_marker_geometry") == "flat_ground_vector_strips",
        "interactive markers should use flat ground vector strips",
        failures,
    )
    require(float(stats.get("interactive_marker_max_height_m", 99.0)) <= 1.25, "interactive markers too tall", failures)
    require(stats.get("sky_sun_arrow_removed") is True, "sky sun arrow removal not reported", failures)
    require(stats.get("material_mode") == "procedural_multilayer", "material mode missing", failures)
    require(int(stats.get("material_layers", 0)) >= 5, "material layer count too low", failures)
    require(stats.get("material_texture_atlas") is True, "material texture atlas missing", failures)
    require(int(stats.get("material_texture_tiles", 0)) >= 6, "material texture tile count too low", failures)
    require(
        stats.get("material_texture_source") == "polyhaven_plus_huilian_legacy_diffuse",
        "Poly Haven and Huilian runtime textures are not both active",
        failures,
    )
    require(int(stats.get("material_texture_runtime_files", 0)) >= 6, "runtime material texture files missing", failures)
    require(
        stats.get("huilian_equipment_material_manifest")
        == "data/showcase/huilian_equipment_material_manifest.json",
        "Huilian equipment material manifest not reported",
        failures,
    )
    require(
        int(stats.get("huilian_equipment_legacy_base_color_textures", 0)) == 2,
        "Huilian legacy diffuse runtime texture count mismatch",
        failures,
    )
    require(
        stats.get("huilian_equipment_complete_pbr_texture_set") is False,
        "Huilian legacy diffuse textures must not be reported as a complete PBR texture set",
        failures,
    )
    require(
        stats.get("huilian_equipment_explicit_metallic_roughness_factors") is True
        and stats.get("gltf_explicit_metallic_roughness_vertex_channel") is True,
        "Huilian explicit metallic/roughness material channel missing",
        failures,
    )
    require(
        stats.get("gltf_equipment_brdf") == "ggx_smith_schlick_with_dual_practical_lights",
        "Huilian equipment GGX/practical-light material pass missing",
        failures,
    )
    expected_material_mode = args.material_mode or "all"
    expected_material_counts = {
        "all": {
            "layers": 5,
            "layer_swatches": 8,
            "channel_strips": 6,
            "sample_panels": 12,
            "uv_grid_lines": 16,
            "roughness_bands": 8,
            "normal_glyphs": 24,
            "wetness_patches": 18,
            "corrosion_patches": 22,
            "emission_markers": 6,
            "atlas_tiles": 4,
            "layers_active": True,
            "uv_active": True,
            "surface_active": True,
            "degradation_active": True,
            "emissive_active": True,
        },
        "layers": {
            "layers": 1,
            "layer_swatches": 8,
            "channel_strips": 6,
            "sample_panels": 12,
            "uv_grid_lines": 0,
            "roughness_bands": 0,
            "normal_glyphs": 0,
            "wetness_patches": 0,
            "corrosion_patches": 0,
            "emission_markers": 0,
            "atlas_tiles": 0,
            "layers_active": True,
            "uv_active": False,
            "surface_active": False,
            "degradation_active": False,
            "emissive_active": False,
        },
        "uv": {
            "layers": 1,
            "layer_swatches": 0,
            "channel_strips": 0,
            "sample_panels": 0,
            "uv_grid_lines": 16,
            "roughness_bands": 0,
            "normal_glyphs": 0,
            "wetness_patches": 0,
            "corrosion_patches": 0,
            "emission_markers": 0,
            "atlas_tiles": 4,
            "layers_active": False,
            "uv_active": True,
            "surface_active": False,
            "degradation_active": False,
            "emissive_active": False,
        },
        "surface": {
            "layers": 1,
            "layer_swatches": 0,
            "channel_strips": 0,
            "sample_panels": 0,
            "uv_grid_lines": 0,
            "roughness_bands": 8,
            "normal_glyphs": 24,
            "wetness_patches": 0,
            "corrosion_patches": 0,
            "emission_markers": 0,
            "atlas_tiles": 0,
            "layers_active": False,
            "uv_active": False,
            "surface_active": True,
            "degradation_active": False,
            "emissive_active": False,
        },
        "degradation": {
            "layers": 1,
            "layer_swatches": 0,
            "channel_strips": 0,
            "sample_panels": 0,
            "uv_grid_lines": 0,
            "roughness_bands": 0,
            "normal_glyphs": 0,
            "wetness_patches": 18,
            "corrosion_patches": 22,
            "emission_markers": 0,
            "atlas_tiles": 0,
            "layers_active": False,
            "uv_active": False,
            "surface_active": False,
            "degradation_active": True,
            "emissive_active": False,
        },
        "emissive": {
            "layers": 1,
            "layer_swatches": 0,
            "channel_strips": 0,
            "sample_panels": 0,
            "uv_grid_lines": 0,
            "roughness_bands": 0,
            "normal_glyphs": 0,
            "wetness_patches": 0,
            "corrosion_patches": 0,
            "emission_markers": 6,
            "atlas_tiles": 0,
            "layers_active": False,
            "uv_active": False,
            "surface_active": False,
            "degradation_active": False,
            "emissive_active": True,
        },
    }[expected_material_mode]
    require(
        stats.get("material_inspection_detail_pass") == "atlas_layers_uv_roughness_normal_wet_corrosion_overlay",
        "material inspection pass missing",
        failures,
    )
    require(
        stats.get("cinematic_material_regrade_pass") == "low_whiteout_steel_concrete_split",
        "cinematic material regrade pass missing",
        failures,
    )
    require(stats.get("cinematic_low_luma_steel_mask") is True, "low-luma steel material mask missing", failures)
    require(stats.get("cinematic_near_steel_edge_regrade") is True, "near steel edge regrade missing", failures)
    require(
        stats.get("cinematic_concrete_whiteout_reduced") is True,
        "concrete whiteout reduction missing",
        failures,
    )
    require(stats.get("animated_particle_shader") is True, "animated particle shader not reported", failures)
    require(stats.get("wind_driven_particle_animation") is True, "wind-driven particle animation not reported", failures)
    require(stats.get("fire_flicker_shader") is True, "fire flicker shader not reported", failures)
    require(
        stats.get("fire_particle_style") == "layered_pool_jet_tongues_smoke",
        "layered fire/smoke particle style not reported",
        failures,
    )
    require(int(stats.get("pool_fire_tongues", 0)) >= 150, "pool fire tongue count too low", failures)
    require(int(stats.get("pool_fire_glow_puffs", 0)) >= 40, "pool fire glow layer missing", failures)
    require(int(stats.get("smoke_plume_puffs", 0)) >= 280, "smoke plume puff count too low", failures)
    require(int(stats.get("jet_fire_segments", 0)) >= 90, "jet fire segment count too low", failures)
    require(int(stats.get("jet_smoke_puffs", 0)) >= 70, "jet smoke trail missing", failures)
    require(
        stats.get("fire_shape_profile") == "low_tapered_pool_with_smoke_cap",
        "low tapered fire shape profile missing",
        failures,
    )
    require(
        stats.get("fire_alpha_profile") == "view_soft_edge_noise_tongues",
        "fire soft-edge alpha profile missing",
        failures,
    )
    require(
        stats.get("smoke_plume_model") == "wind_aligned_layered_puffs",
        "wind-aligned smoke plume model missing",
        failures,
    )
    require(
        stats.get("explosion_effect_pass") == "tank_fireball_shockwave_debris_smoke_crown",
        "tank explosion particle pass not reported",
        failures,
    )
    require(int(stats.get("explosion_fireball_puffs", 0)) >= 80, "explosion fireball puffs too sparse", failures)
    require(
        int(stats.get("explosion_smoke_crown_puffs", 0)) >= 60,
        "explosion smoke crown puffs too sparse",
        failures,
    )
    require(int(stats.get("explosion_debris_sparks", 0)) >= 70, "explosion debris sparks too sparse", failures)
    require(int(stats.get("explosion_shockwave_rings", 0)) >= 5, "explosion shockwave rings missing", failures)
    require(int(stats.get("explosion_pressure_bands", 0)) >= 4, "explosion pressure bands missing", failures)
    require(int(stats.get("explosion_pressure_vectors", 0)) >= 8, "explosion pressure vectors missing", failures)
    require(
        stats.get("explosion_source_role") == "tank_farm_overpressure_event",
        "explosion source role missing",
        failures,
    )
    require(stats.get("explosion_wind_coupled_smoke") is True, "explosion smoke wind coupling missing", failures)
    require(
        stats.get("gas_diffusion_analysis_pass") == "wind_driven_ground_footprint_and_evacuation_guidance",
        "gas diffusion analysis pass not reported",
        failures,
    )
    require(
        stats.get("gas_diffusion_realtime_wind_response") is True,
        "gas diffusion realtime wind response missing",
        failures,
    )
    require(stats.get("weather_particles_separate_channels") is True, "weather particle channel separation not reported", failures)
    require(stats.get("transparent_effect_blending") is True, "transparent effect blending not reported", failures)
    require(stats.get("volumetric_particle_alpha") is True, "volumetric particle alpha not reported", failures)
    require(
        stats.get("weather_particle_material_channels") == "rain_streak_snow_flake",
        "separate rain/snow material channels not reported",
        failures,
    )
    require(
        stats.get("precipitation_style") == "fine_wind_slanted_distance_faded",
        "fine distance-faded precipitation style not reported",
        failures,
    )
    require(
        stats.get("rain_streak_profile") == "thin_low_alpha_slanted_lines",
        "thin low-alpha rain streak profile not reported",
        failures,
    )
    require(
        stats.get("snow_flake_profile") == "small_soft_drift_crosses",
        "soft drifting snow flake profile not reported",
        failures,
    )
    require(stats.get("wet_surface_shader") is True, "wet surface shader not reported", failures)
    require(stats.get("wet_material_channels") == "road_roof_metal", "wet material channels not reported", failures)
    require(stats.get("rain_ground_feedback") is True, "rain ground feedback capability not reported", failures)

    if preset == "overview":
        require(float(stats.get("fog_density", 9.0)) <= 0.12, "overview preset should not start too foggy", failures)
    elif preset == "terrain":
        require(int(stats.get("vegetation_seed_count", 0)) >= 500, "terrain preset should show dense vegetation", failures)
        require(float(stats.get("fog_density", 9.0)) <= 0.2, "terrain preset should keep fog low", failures)
    elif preset == "fusion":
        require(
            stats.get("terrain_model_fusion_focus_active") is True,
            "fusion preset did not activate terrain/model fusion overlay",
            failures,
        )
        require(
            int(stats.get("terrain_model_fusion_blend_collars", 0)) >= 10,
            "terrain/model blend collars too sparse",
            failures,
        )
        require(
            int(stats.get("terrain_model_fusion_cut_fill_bands", 0)) >= 6,
            "terrain/model cut-fill bands too sparse",
            failures,
        )
        require(
            int(stats.get("terrain_model_fusion_foundation_pads", 0)) >= 14,
            "terrain/model foundation pads too sparse",
            failures,
        )
        require(
            int(stats.get("terrain_model_fusion_contact_shadow_checks", 0)) >= 14,
            "terrain/model contact-shadow checks too sparse",
            failures,
        )
        require(
            int(stats.get("terrain_model_fusion_grade_sample_posts", 0)) >= 18,
            "terrain/model grade sample posts too sparse",
            failures,
        )
        require(
            int(stats.get("terrain_model_fusion_slope_feather_strips", 0)) >= 24,
            "terrain/model slope feather strips too sparse",
            failures,
        )
        require(
            int(stats.get("terrain_model_fusion_drain_tie_ins", 0)) >= 12,
            "terrain/model drain tie-ins too sparse",
            failures,
        )
        require(
            int(stats.get("terrain_model_fusion_road_shoulder_stitches", 0)) >= 16,
            "terrain/model road shoulder stitches too sparse",
            failures,
        )
        require(
            int(stats.get("terrain_model_fusion_vegetation_masks", 0)) >= 8,
            "terrain/model vegetation masks too sparse",
            failures,
        )
        require(
            int(stats.get("terrain_model_fusion_imported_asset_anchors", 0)) >= 4,
            "terrain/model imported asset anchors missing",
            failures,
        )
        require(
            float(stats.get("terrain_model_fusion_area_sqm", 0.0)) >= 90000.0,
            "terrain/model fusion coverage too small",
            failures,
        )
        require(
            float(stats.get("terrain_model_fusion_max_grade_delta_m", 99.0)) <= 12.0,
            "terrain/model local grade delta too large",
            failures,
        )
        require(stats.get("terrain_model_fusion_real_dem_dom") is True, "fusion preset should use real DEM/DOM", failures)
        require(stats.get("terrain_model_fusion_cut_fill_pad") is True, "fusion preset should use cut/fill pad", failures)
        require(stats.get("terrain_model_fusion_contact_ao") is True, "fusion preset should keep contact AO", failures)
        require(stats.get("cloud_geometry_visible") is False, "fusion preset should hide cloud geometry over seams", failures)
        require(int(stats.get("point_lights", 0)) >= 6, "fusion preset should keep model bases readable", failures)
        require(float(stats.get("fog_density", 1.0)) <= 0.12, "fusion preset should keep seam overlays readable", failures)
        require(
            stats.get("xineng_terrain_road_max_fusion_proxy_pass")
            == "terrain_road_landscape_max_source_runtime_fusion_proxies",
            "terrain/road MAX fusion proxy pass missing",
            failures,
        )
        require(
            stats.get("xineng_terrain_road_max_fusion_runtime_mode")
            == "procedural_proxy_until_dcc_export_with_dem_dom_alignment",
            "terrain/road MAX fusion runtime mode missing",
            failures,
        )
        require(
            stats.get("xineng_terrain_road_max_fusion_focus_active") is True,
            "terrain/road MAX fusion proxy layer inactive",
            failures,
        )
        require(
            int(stats.get("xineng_terrain_road_max_fusion_proxy_assets", 0)) >= 5,
            "terrain/road MAX fusion source asset count too low",
            failures,
        )
        require(
            int(stats.get("xineng_terrain_road_max_fusion_source_asset_bytes", 0)) >= 12000000,
            "terrain/road MAX fusion source byte count too low",
            failures,
        )
        require(
            int(stats.get("xineng_terrain_road_max_fusion_gravel_shoulders", 0)) >= 18,
            "terrain/road MAX fusion gravel shoulders too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_terrain_road_max_fusion_paddy_tiles", 0)) >= 12,
            "terrain/road MAX fusion paddy tiles too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_terrain_road_max_fusion_lane_strips", 0)) >= 22,
            "terrain/road MAX fusion lane strips too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_terrain_road_max_fusion_parking_stalls", 0)) >= 12,
            "terrain/road MAX fusion parking stalls too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_terrain_road_max_fusion_seam_feather_bands", 0)) >= 20,
            "terrain/road MAX fusion seam feather bands too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_terrain_road_max_fusion_drainage_swales", 0)) >= 8,
            "terrain/road MAX fusion drainage swales too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_terrain_road_max_fusion_grade_tie_samples", 0)) >= 10,
            "terrain/road MAX fusion grade tie samples too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_terrain_road_max_fusion_scene_instances", 0)) >= 150,
            "terrain/road MAX fusion scene instances too sparse",
            failures,
        )
    elif preset == "gis":
        require(stats.get("digital_earth_gis_active") is True, "gis preset did not activate digital earth overlay", failures)
        expected_gis_measure = args.gis_measure or "all"
        require(
            stats.get("gis_measurement_mode") == expected_gis_measure,
            f"GIS measurement mode mismatch: {stats.get('gis_measurement_mode')} != {expected_gis_measure}",
            failures,
        )
        require(
            stats.get("gis_measurement_interactive_controls") is True,
            "GIS interactive measurement controls not reported",
            failures,
        )
        require(
            int(stats.get("gis_measurement_interactive_modes", 0)) >= 5,
            "GIS interactive measurement modes missing",
            failures,
        )
        require(
            stats.get("gis_measurement_interactive_hotkeys") == "9_g",
            "GIS interactive measurement hotkeys not reported",
            failures,
        )
        expected_layers = 4 if expected_gis_measure == "all" else 1
        require(
            int(stats.get("gis_measurement_active_layers", 0)) == expected_layers,
            "GIS active measurement layer count mismatch",
            failures,
        )
        require(
            stats.get("gis_distance_measure_active") is (expected_gis_measure in ("all", "distance")),
            "GIS distance layer active flag mismatch",
            failures,
        )
        require(
            stats.get("gis_area_measure_active") is (expected_gis_measure in ("all", "area")),
            "GIS area layer active flag mismatch",
            failures,
        )
        require(
            stats.get("gis_elevation_measure_active") is (expected_gis_measure in ("all", "elevation")),
            "GIS elevation layer active flag mismatch",
            failures,
        )
        require(
            stats.get("gis_volume_measure_active") is (expected_gis_measure in ("all", "volume")),
            "GIS volume layer active flag mismatch",
            failures,
        )
        require(
            int(stats.get("digital_earth_gis_globe_meridians", 0)) >= 12,
            "digital earth globe meridians too sparse",
            failures,
        )
        require(
            int(stats.get("digital_earth_gis_globe_parallels", 0)) >= 7,
            "digital earth globe parallels too sparse",
            failures,
        )
        require(
            int(stats.get("digital_earth_gis_tile_footprints", 0)) >= 16,
            "GIS tile footprint overlay too sparse",
            failures,
        )
        expected_projection_links = {
            "all": 6,
            "distance": 2,
            "area": 1,
            "elevation": 2,
            "volume": 1,
        }[expected_gis_measure]
        require(
            int(stats.get("digital_earth_gis_projection_links", 0)) == expected_projection_links,
            "GIS active measurement projection-link count mismatch",
            failures,
        )
        require(
            int(stats.get("digital_earth_gis_available_projection_links", 0)) >= 6,
            "GIS available measurement projection links too sparse",
            failures,
        )
        require(
            int(stats.get("digital_earth_gis_coordinate_grid_lines", 0)) >= 12,
            "GIS coordinate grid lines too sparse",
            failures,
        )
        require(
            int(stats.get("digital_earth_gis_sample_markers", 0)) >= 9,
            "GIS terrain sample markers too sparse",
            failures,
        )
        require(stats.get("digital_earth_gis_georef_active") is True, "GIS georef audit should be active", failures)
        require(
            int(stats.get("digital_earth_gis_georef_anchors", 0)) >= 4,
            "GIS georef anchors too sparse",
            failures,
        )
        require(
            int(stats.get("digital_earth_gis_transform_pipelines", 0)) >= 3,
            "GIS transform pipelines too sparse",
            failures,
        )
        require(
            abs(float(stats.get("digital_earth_gis_center_lat_deg", 0.0)) - 38.945098) <= 0.01,
            "GIS georef center latitude mismatch",
            failures,
        )
        require(
            abs(float(stats.get("digital_earth_gis_center_lon_deg", 0.0)) - 117.593455) <= 0.01,
            "GIS georef center longitude mismatch",
            failures,
        )
        require(
            float(stats.get("digital_earth_gis_mercator_width_m", 0.0)) >= 103000.0,
            "GIS Mercator width too small",
            failures,
        )
        require(
            float(stats.get("digital_earth_gis_mercator_height_m", 0.0)) >= 111000.0,
            "GIS Mercator height too small",
            failures,
        )
        require(
            float(stats.get("digital_earth_gis_dom_meters_per_sample", 0.0)) >= 100.0,
            "GIS DOM meters-per-sample not reported",
            failures,
        )
        require(int(stats.get("digital_earth_gis_tile_zoom", 0)) == 15, "GIS tile zoom mismatch", failures)
        require(int(stats.get("digital_earth_gis_tile_x_min", 0)) == 27045, "GIS tile x min mismatch", failures)
        require(int(stats.get("digital_earth_gis_tile_x_max", 0)) == 27130, "GIS tile x max mismatch", failures)
        require(int(stats.get("digital_earth_gis_tile_y_min", 0)) == 12484, "GIS tile y min mismatch", failures)
        require(int(stats.get("digital_earth_gis_tile_y_max", 0)) == 12575, "GIS tile y max mismatch", failures)
        require(
            int(stats.get("digital_earth_gis_tile_columns", 0)) >= 86,
            "GIS tile column span too small",
            failures,
        )
        require(
            int(stats.get("digital_earth_gis_tile_rows", 0)) >= 92,
            "GIS tile row span too small",
            failures,
        )
        require(
            float(stats.get("digital_earth_gis_roundtrip_error_cm", 99.0)) <= 5.0,
            "GIS georef round-trip error budget too loose",
            failures,
        )
        expected_source_distance = expected_gis_measure in ("all", "distance")
        expected_source_area = expected_gis_measure in ("all", "area")
        expected_source_elevation = expected_gis_measure in ("all", "elevation")
        expected_source_volume = expected_gis_measure in ("all", "volume")
        expected_gis_source_instances = (
            8
            + 9
            + 16
            + 3
            + 8
            + (2 if expected_source_distance else 0)
            + (4 if expected_source_area else 0)
            + (2 if expected_source_elevation else 0)
            + (4 if expected_source_volume else 0)
        )
        require(
            stats.get("xineng_gis_source_survey_pass") == "max_terrain_road_landmark_survey_control_proxy",
            "GIS source survey control pass missing",
            failures,
        )
        require(
            stats.get("xineng_gis_source_survey_runtime_mode")
            == "procedural_gis_control_proxy_until_dcc_export",
            "GIS source survey runtime mode mismatch",
            failures,
        )
        require(
            stats.get("xineng_gis_source_survey_focus_active") is True,
            "GIS source survey control network should be active for GIS preset",
            failures,
        )
        require(
            int(stats.get("xineng_gis_source_survey_assets", 0)) >= 8,
            "GIS source survey asset coverage too low",
            failures,
        )
        require(
            int(stats.get("xineng_gis_source_survey_source_asset_bytes", 0)) >= 16700000,
            "GIS source survey represented source bytes too low",
            failures,
        )
        require(
            int(stats.get("xineng_gis_source_survey_control_points", 0)) >= 9,
            "GIS source survey control points too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_gis_source_survey_tile_anchors", 0)) >= 16,
            "GIS source survey tile anchors too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_gis_source_survey_datum_posts", 0)) >= 3,
            "GIS source survey datum posts too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_gis_source_survey_source_beacons", 0)) >= 8,
            "GIS source survey source beacons too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_gis_source_survey_volume_tie_lines", 0))
            == (4 if expected_source_volume else 0),
            "GIS source survey volume tie-line count mismatch",
            failures,
        )
        require(
            (float(stats.get("xineng_gis_source_survey_volume_total_m3", 0.0)) > 0.0)
            is expected_source_volume,
            "GIS source survey volume result active state mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_gis_source_survey_scene_instances", 0)) >= expected_gis_source_instances,
            "GIS source survey scene instance count too low for selected measurement mode",
            failures,
        )
        if expected_source_distance:
            require(
                int(stats.get("xineng_gis_source_survey_distance_tie_lines", 0)) >= 2,
                "GIS source survey distance tie lines too sparse",
                failures,
            )
            require(
                float(stats.get("xineng_gis_source_survey_distance_m", 0.0)) >= 600.0,
                "GIS source survey distance measurement too short",
                failures,
            )
        else:
            require(
                int(stats.get("xineng_gis_source_survey_distance_tie_lines", -1)) == 0,
                "GIS source survey distance tie lines should be hidden",
                failures,
            )
            require(
                float(stats.get("xineng_gis_source_survey_distance_m", -1.0)) == 0.0,
                "GIS source survey distance measurement should be hidden",
                failures,
            )
        if expected_source_area:
            require(
                int(stats.get("xineng_gis_source_survey_area_tie_lines", 0)) >= 4,
                "GIS source survey area tie lines too sparse",
                failures,
            )
            require(
                float(stats.get("xineng_gis_source_survey_area_sqm", 0.0)) >= 100000.0,
                "GIS source survey area measurement too small",
                failures,
            )
        else:
            require(
                int(stats.get("xineng_gis_source_survey_area_tie_lines", -1)) == 0,
                "GIS source survey area tie lines should be hidden",
                failures,
            )
            require(
                float(stats.get("xineng_gis_source_survey_area_sqm", -1.0)) == 0.0,
                "GIS source survey area measurement should be hidden",
                failures,
            )
        if expected_source_elevation:
            require(
                int(stats.get("xineng_gis_source_survey_elevation_tie_lines", 0)) >= 2,
                "GIS source survey elevation tie lines too sparse",
                failures,
            )
            require(
                float(stats.get("xineng_gis_source_survey_elevation_delta_m", 0.0)) >= 0.5,
                "GIS source survey elevation delta not reported",
                failures,
            )
        else:
            require(
                int(stats.get("xineng_gis_source_survey_elevation_tie_lines", -1)) == 0,
                "GIS source survey elevation tie lines should be hidden",
                failures,
            )
            require(
                float(stats.get("xineng_gis_source_survey_elevation_delta_m", -1.0)) == 0.0,
                "GIS source survey elevation measurement should be hidden",
                failures,
            )
        require(float(stats.get("fog_density", 9.0)) <= 0.12, "gis preset should keep GIS overlays readable", failures)
        require(int(stats.get("point_lights", 99)) <= 4, "gis preset should keep point lights low for map readability", failures)
    elif preset == "models":
        expected_max_catalog_mode = args.max_catalog_mode or "all"
        expected_max_catalog_counts = {
            "all": {"groups": 6, "heavy": 9, "footprints": 8, "stages": 4, "risk": 6, "anchors": 19},
            "site": {"groups": 2, "heavy": 5, "footprints": 4, "stages": 4, "risk": 2, "anchors": 6},
            "warehouse": {"groups": 1, "heavy": 1, "footprints": 2, "stages": 4, "risk": 1, "anchors": 3},
            "safety": {"groups": 1, "heavy": 2, "footprints": 1, "stages": 4, "risk": 3, "anchors": 4},
            "power": {"groups": 1, "heavy": 1, "footprints": 1, "stages": 4, "risk": 1, "anchors": 3},
            "security": {"groups": 1, "heavy": 0, "footprints": 1, "stages": 4, "risk": 2, "anchors": 3},
        }[expected_max_catalog_mode]
        require(int(stats.get("facility_instances", 0)) >= 50, "models preset should expose the industrial model set", failures)
        require(stats.get("xineng_site_max_catalog_active") is True, "models preset did not activate site MAX catalog overlay", failures)
        require(
            stats.get("xineng_site_max_catalog_mode") == expected_max_catalog_mode,
            f"MAX catalog mode mismatch: {stats.get('xineng_site_max_catalog_mode')} != {expected_max_catalog_mode}",
            failures,
        )
        require(
            stats.get("xineng_site_max_interactive_controls") is True,
            "models preset MAX catalog interactive controls missing",
            failures,
        )
        require(int(stats.get("xineng_site_max_interactive_modes", 0)) >= 6, "MAX catalog mode count too low", failures)
        require(
            stats.get("xineng_site_max_interactive_hotkeys") == "3_f3",
            "MAX catalog hotkeys not reported",
            failures,
        )
        require(
            int(stats.get("xineng_site_max_active_role_groups", -1)) == expected_max_catalog_counts["groups"],
            "models preset site MAX active role group count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_site_max_active_heavy_assets", -1)) == expected_max_catalog_counts["heavy"],
            "models preset site MAX active heavy asset count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_site_max_coverage_footprints", -1)) == expected_max_catalog_counts["footprints"],
            "models preset site MAX coverage footprint count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_site_max_conversion_stages", -1)) == expected_max_catalog_counts["stages"],
            "models preset site MAX conversion stage count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_site_max_risk_links", -1)) == expected_max_catalog_counts["risk"],
            "models preset site MAX risk link count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_site_max_anchor_beacons", -1)) == expected_max_catalog_counts["anchors"],
            "models preset site MAX anchor beacon count mismatch",
            failures,
        )
        require(
            stats.get("xineng_source_asset_coverage_active") is True,
            "models preset did not activate source asset coverage audit",
            failures,
        )
        require(
            int(stats.get("xineng_source_asset_coverage_groups", -1)) == 8,
            "source asset coverage group count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_source_asset_coverage_runtime_layers", -1)) == 18,
            "source asset coverage runtime layer count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_source_asset_coverage_grid_cells", -1)) == 52,
            "source asset coverage grid cell count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_source_asset_coverage_dcc_stages", -1)) == 4,
            "source asset coverage DCC stage count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_source_asset_coverage_evidence_links", -1)) == 12,
            "source asset coverage evidence link count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_source_asset_coverage_source_beacons", -1)) == 8,
            "source asset coverage source beacon count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_source_asset_coverage_scene_instances", -1)) == 103,
            "source asset coverage scene instance count mismatch",
            failures,
        )
        require(
            stats.get("xineng_max_site_ops_proxy_pass")
            == "max_only_site_operations_assets_procedural_runtime_proxies",
            "models preset site operations MAX proxy pass not reported",
            failures,
        )
        require(
            stats.get("xineng_max_site_ops_runtime_mode") == "procedural_proxy_until_dcc_export",
            "models preset site operations MAX proxy runtime mode not reported",
            failures,
        )
        require(
            stats.get("xineng_max_site_ops_proxy_focus_active") is True,
            "models preset did not activate site operations MAX proxy layer",
            failures,
        )
        site_ops_roles = str(stats.get("xineng_max_site_ops_source_roles", ""))
        require(
            "guardhouse" in site_ops_roles
            and "camera" in site_ops_roles
            and "brand_signage" in site_ops_roles
            and "weighbridge" in site_ops_roles
            and "lane_markings" in site_ops_roles,
            "models preset site operations source roles incomplete",
            failures,
        )
        require(
            int(stats.get("xineng_max_site_ops_proxy_assets", 0)) >= 18,
            "models preset site operations MAX proxy source asset count too low",
            failures,
        )
        require(
            int(stats.get("xineng_max_site_ops_source_asset_bytes", 0)) >= 14700000,
            "models preset site operations MAX source byte count too low",
            failures,
        )
        require(
            int(stats.get("xineng_max_site_ops_scene_instances", 0)) >= 60,
            "models preset site operations proxy scene instances too sparse",
            failures,
        )
        require(int(stats.get("xineng_max_site_ops_guardhouses", 0)) >= 3, "guardhouse proxies too sparse", failures)
        require(
            int(stats.get("xineng_max_site_ops_barrier_gates", 0)) >= 2,
            "barrier gate proxies too sparse",
            failures,
        )
        require(int(stats.get("xineng_max_site_ops_camera_poles", 0)) >= 6, "camera pole proxies too sparse", failures)
        require(int(stats.get("xineng_max_site_ops_camera_heads", 0)) >= 6, "camera head proxies too sparse", failures)
        require(
            int(stats.get("xineng_max_site_ops_signage_screens", 0)) >= 4,
            "site operations screen proxies too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_max_site_ops_brand_signage_panels", 0)) >= 1,
            "site operations brand signage proxy missing",
            failures,
        )
        require(
            int(stats.get("xineng_max_site_ops_street_lamps", 0)) >= 8,
            "site operations street lamps too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_max_site_ops_heat_station_units", 0)) >= 1,
            "heat station proxy missing",
            failures,
        )
        require(
            int(stats.get("xineng_max_site_ops_weighbridge_pads", 0)) >= 1,
            "weighbridge proxy missing",
            failures,
        )
        require(
            int(stats.get("xineng_max_site_ops_parking_slots", 0)) >= 12,
            "parking slot proxies too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_max_site_ops_lane_markings", 0)) >= 16,
            "lane marking proxies too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_max_site_ops_route_links", 0)) >= 8,
            "site operations route links too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_max_site_ops_proxy_beacons", 0)) >= 18,
            "site operations proxy beacons too sparse",
            failures,
        )
        require(
            float(stats.get("xineng_max_site_ops_route_length_m", 0.0)) >= 250.0,
            "site operations route length too short",
            failures,
        )
        expected_security_source_active = expected_max_catalog_mode in ("all", "security")
        require(
            stats.get("xineng_security_source_surveillance_pass")
            == "max_camera_guardhouse_access_surveillance_source_proxy",
            "security source surveillance pass not reported",
            failures,
        )
        require(
            stats.get("xineng_security_source_runtime_mode")
            == "procedural_security_surveillance_proxy_until_dcc_export",
            "security source surveillance runtime mode mismatch",
            failures,
        )
        require(
            stats.get("xineng_security_source_focus_active") is expected_security_source_active,
            "security source surveillance active flag mismatch",
            failures,
        )
        require(
            stats.get("xineng_security_source_catalog_modes") == "all_security",
            "security source catalog mode binding missing",
            failures,
        )
        require(
            int(stats.get("xineng_security_source_assets", 0)) >= 12,
            "security source asset count too low",
            failures,
        )
        require(
            int(stats.get("xineng_security_source_asset_bytes", 0)) >= 11500000,
            "security source byte count too low",
            failures,
        )
        expected_security_counts = {
            "nodes": 6 if expected_security_source_active else 0,
            "cones": 6 if expected_security_source_active else 0,
            "handoff": 7 if expected_security_source_active else 0,
            "interlocks": 5 if expected_security_source_active else 0,
            "plates": 2 if expected_security_source_active else 0,
            "tripwires": 8 if expected_security_source_active else 0,
            "blind": 4 if expected_security_source_active else 0,
            "displays": 4 if expected_security_source_active else 0,
            "brand": 1 if expected_security_source_active else 0,
            "beacons": 12 if expected_security_source_active else 0,
            "instances": 67 if expected_security_source_active else 0,
        }
        require(
            int(stats.get("xineng_security_surveillance_nodes", -1)) == expected_security_counts["nodes"],
            "security surveillance node count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_security_coverage_cones", -1)) == expected_security_counts["cones"],
            "security coverage cone count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_security_handoff_links", -1)) == expected_security_counts["handoff"],
            "security handoff link count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_security_access_interlocks", -1)) == expected_security_counts["interlocks"],
            "security access interlock count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_security_plate_readers", -1)) == expected_security_counts["plates"],
            "security plate reader count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_security_tripwire_segments", -1)) == expected_security_counts["tripwires"],
            "security tripwire segment count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_security_blind_zone_patches", -1)) == expected_security_counts["blind"],
            "security blind-zone patch count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_security_command_displays", -1)) == expected_security_counts["displays"],
            "security command display count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_security_brand_signage_panels", -1)) == expected_security_counts["brand"],
            "security brand signage panel count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_security_source_beacons", -1)) == expected_security_counts["beacons"],
            "security source beacon count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_security_scene_instances", -1)) == expected_security_counts["instances"],
            "security source scene instance count mismatch",
            failures,
        )
        expected_power_proxy_active = expected_max_catalog_mode in ("all", "power")
        require(
            stats.get("xineng_power_max_proxy_pass")
            == "max_only_high_voltage_power_corridor_procedural_runtime_proxies",
            "power MAX proxy pass not reported",
            failures,
        )
        require(
            stats.get("xineng_power_max_runtime_mode") == "procedural_proxy_until_dcc_export_and_lod_partition",
            "power MAX proxy runtime mode not reported",
            failures,
        )
        require(
            stats.get("xineng_power_max_proxy_focus_active") is expected_power_proxy_active,
            "power MAX proxy active flag mismatch",
            failures,
        )
        if expected_power_proxy_active:
            if expected_max_catalog_mode == "power":
                require(
                    stats.get("xineng_power_max_camera_focus") == "side_profile_corridor_when_power_mode",
                    "power MAX focused camera not reported",
                    failures,
                )
            require(
                int(stats.get("xineng_power_max_proxy_assets", 0)) >= 4,
                "power MAX proxy source asset count too low",
                failures,
            )
            require(
                int(stats.get("xineng_power_max_source_asset_bytes", 0)) >= 190000000,
                "power MAX proxy source byte count too low",
                failures,
            )
            require(
                int(stats.get("xineng_power_max_transmission_towers", 0)) >= 4,
                "power MAX transmission tower proxies too sparse",
                failures,
            )
            require(
                int(stats.get("xineng_power_max_tower_crossarms", 0)) >= 12,
                "power MAX tower crossarms too sparse",
                failures,
            )
            require(
                int(stats.get("xineng_power_max_insulator_strings", 0)) >= 24,
                "power MAX insulator strings too sparse",
                failures,
            )
            require(
                int(stats.get("xineng_power_max_conductor_spans", 0)) >= 18,
                "power MAX conductor spans too sparse",
                failures,
            )
            require(
                int(stats.get("xineng_power_max_substation_transformers", 0)) >= 3,
                "power MAX substation transformers too sparse",
                failures,
            )
            require(
                int(stats.get("xineng_power_max_switchgear_bays", 0)) >= 6,
                "power MAX switchgear bays too sparse",
                failures,
            )
            require(
                int(stats.get("xineng_power_max_monitor_cabinets", 0)) >= 3,
                "power MAX monitor cabinets too sparse",
                failures,
            )
            require(
                int(stats.get("xineng_power_max_scene_instances", 0)) >= 100,
                "power MAX scene instances too sparse",
                failures,
            )
            require(
                float(stats.get("xineng_power_max_corridor_length_m", 0.0)) >= 180.0,
                "power MAX corridor length too short",
                failures,
            )
        require(
            stats.get("xineng_site_max_site_active") is (expected_max_catalog_mode == "site"),
            "site MAX focus active flag mismatch",
            failures,
        )
        require(
            stats.get("xineng_site_max_warehouse_active") is (expected_max_catalog_mode == "warehouse"),
            "warehouse MAX focus active flag mismatch",
            failures,
        )
        require(
            stats.get("xineng_site_max_safety_active") is (expected_max_catalog_mode == "safety"),
            "safety MAX focus active flag mismatch",
            failures,
        )
        require(
            stats.get("xineng_site_max_power_active") is (expected_max_catalog_mode == "power"),
            "power MAX focus active flag mismatch",
            failures,
        )
        require(
            stats.get("xineng_site_max_security_active") is (expected_max_catalog_mode == "security"),
            "security MAX focus active flag mismatch",
            failures,
        )
    elif preset == "huilian":
        require(stats.get("huilian_asset_focus_active") is True, "Huilian inspection focus is inactive", failures)
        require(
            int(stats.get("huilian_valve_catalog_instances", 0)) == 1,
            "Huilian inspection preset did not load exactly one valve catalog instance",
            failures,
        )
        require(
            int(stats.get("huilian_equipment_runtime_instances", 0)) == 2,
            "Huilian inspection preset did not load exactly one valve and one pump",
            failures,
        )
        require(
            stats.get("huilian_valve_catalog_runtime_asset")
            == "assets/showcase/runtime/huilian_wz_check_valve_runtime.glb",
            "Huilian inspection preset reports the wrong runtime asset",
            failures,
        )
        require(
            stats.get("huilian_production_water_pump_runtime_asset")
            == "assets/showcase/runtime/huilian_production_water_pump_p0401_runtime.glb",
            "Huilian inspection preset reports the wrong pump runtime asset",
            failures,
        )
        expected_huilian_volume_active = (args.huilian_view or "valve") == "southwest"
        require(
            stats.get("gis_volume_measure_active") is expected_huilian_volume_active,
            "Huilian southwest DEM volume overlay active state mismatch",
            failures,
        )
        require(
            114.5428 <= float(stats.get("gis_volume_center_lon_deg", 0.0)) <= 114.6565
            and 22.7120 <= float(stats.get("gis_volume_center_lat_deg", 0.0)) <= 22.7835,
            "Huilian volume footprint WGS84 center is outside source bounds",
            failures,
        )
    elif preset == "materials":
        require(
            stats.get("material_inspection_focus_active") is True,
            "materials preset did not activate material inspection overlay",
            failures,
        )
        require(
            stats.get("material_inspection_mode") == expected_material_mode,
            f"material inspection mode mismatch: {stats.get('material_inspection_mode')} != {expected_material_mode}",
            failures,
        )
        require(
            stats.get("material_inspection_interactive_controls") is True,
            "material inspection interactive controls missing",
            failures,
        )
        require(
            int(stats.get("material_inspection_interactive_modes", 0)) >= 6,
            "material inspection interactive modes missing",
            failures,
        )
        require(
            stats.get("material_inspection_interactive_hotkeys") == "m_m",
            "material inspection interactive hotkey metadata missing",
            failures,
        )
        require(
            int(stats.get("material_inspection_active_layers", -1)) == expected_material_counts["layers"],
            "material inspection active layer count mismatch",
            failures,
        )
        require(
            stats.get("material_inspection_layers_active") is expected_material_counts["layers_active"],
            "material layers active flag mismatch",
            failures,
        )
        require(
            stats.get("material_inspection_uv_active") is expected_material_counts["uv_active"],
            "material UV active flag mismatch",
            failures,
        )
        require(
            stats.get("material_inspection_surface_active") is expected_material_counts["surface_active"],
            "material surface active flag mismatch",
            failures,
        )
        require(
            stats.get("material_inspection_degradation_active") is expected_material_counts["degradation_active"],
            "material degradation active flag mismatch",
            failures,
        )
        require(
            stats.get("material_inspection_emissive_active") is expected_material_counts["emissive_active"],
            "material emissive active flag mismatch",
            failures,
        )
        require(
            int(stats.get("material_inspection_layer_swatches", -1)) == expected_material_counts["layer_swatches"],
            "material layer swatch count mismatch",
            failures,
        )
        require(
            int(stats.get("material_inspection_channel_strips", -1)) == expected_material_counts["channel_strips"],
            "material channel strip count mismatch",
            failures,
        )
        require(
            int(stats.get("material_inspection_sample_panels", -1)) == expected_material_counts["sample_panels"],
            "material sample panel count mismatch",
            failures,
        )
        require(
            int(stats.get("material_inspection_uv_grid_lines", -1)) == expected_material_counts["uv_grid_lines"],
            "material UV grid count mismatch",
            failures,
        )
        require(
            int(stats.get("material_inspection_roughness_bands", -1)) == expected_material_counts["roughness_bands"],
            "material roughness band count mismatch",
            failures,
        )
        require(
            int(stats.get("material_inspection_normal_glyphs", -1)) == expected_material_counts["normal_glyphs"],
            "material normal glyph count mismatch",
            failures,
        )
        require(
            int(stats.get("material_inspection_wetness_patches", -1)) == expected_material_counts["wetness_patches"],
            "material wetness patch count mismatch",
            failures,
        )
        require(
            int(stats.get("material_inspection_corrosion_patches", -1)) == expected_material_counts["corrosion_patches"],
            "material corrosion patch count mismatch",
            failures,
        )
        require(
            int(stats.get("material_inspection_emission_markers", -1)) == expected_material_counts["emission_markers"],
            "material emission marker count mismatch",
            failures,
        )
        require(
            int(stats.get("material_inspection_atlas_tiles", -1)) == expected_material_counts["atlas_tiles"],
            "material atlas tile count mismatch",
            failures,
        )
        require(
            float(stats.get("material_inspection_area_sqm", 0.0)) >= 8000.0,
            "material inspection area too small",
            failures,
        )
        require(
            stats.get("material_inspection_runtime_source") == "polyhaven_runtime",
            "material inspection runtime texture source not active",
            failures,
        )
        require(
            stats.get("material_inspection_multilayer_channels")
            == "base_metal_roughness_normal_wet_corrosion_emissive",
            "material multilayer channel report missing",
            failures,
        )
        require(
            stats.get("xineng_texture_sidecar_material_pass")
            == "source_texture_sidecar_inventory_material_audit_layer",
            "Xineng source texture sidecar material pass missing",
            failures,
        )
        require(
            stats.get("xineng_texture_sidecar_material_runtime_mode")
            == "inventory_proxy_until_channel_renaming_and_runtime_atlas_pack",
            "Xineng source texture sidecar runtime mode missing",
            failures,
        )
        require(
            stats.get("xineng_texture_sidecar_material_focus_active") is True,
            "Xineng source texture sidecar material layer inactive",
            failures,
        )
        require(
            int(stats.get("xineng_texture_sidecar_directories", 0)) >= 7,
            "Xineng source texture sidecar directory count too low",
            failures,
        )
        require(
            int(stats.get("xineng_texture_sidecar_files", 0)) >= 840,
            "Xineng source texture sidecar file count too low",
            failures,
        )
        require(
            int(stats.get("xineng_texture_sidecar_bytes", 0)) >= 480000000,
            "Xineng source texture sidecar byte count too low",
            failures,
        )
        require(
            int(stats.get("xineng_texture_sidecar_fbm_files", 0)) >= 273,
            "Xineng FBM texture sidecar count too low",
            failures,
        )
        require(
            int(stats.get("xineng_texture_sidecar_loose_and_map_files", 0)) >= 560,
            "Xineng loose/map texture sidecar count too low",
            failures,
        )
        require(
            int(stats.get("xineng_texture_sidecar_jpg_files", 0)) >= 675,
            "Xineng JPG texture sidecar count too low",
            failures,
        )
        require(
            int(stats.get("xineng_texture_sidecar_png_files", 0)) >= 44,
            "Xineng PNG texture sidecar count too low",
            failures,
        )
        require(
            int(stats.get("xineng_texture_sidecar_tga_files", 0)) >= 121,
            "Xineng TGA texture sidecar count too low",
            failures,
        )
        require(
            int(stats.get("xineng_texture_sidecar_preview_swatches", 0)) >= 64,
            "Xineng texture sidecar preview swatches too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_texture_sidecar_scene_instances", 0)) >= 99,
            "Xineng texture sidecar scene instances too sparse",
            failures,
        )
        require(
            stats.get("xineng_pbr_channel_resolver_pass")
            == "filename_semantic_pbr_channel_binding_and_atlas_queue",
            "Xineng PBR channel resolver pass missing",
            failures,
        )
        require(
            stats.get("xineng_pbr_channel_resolver_runtime_mode")
            == "materials_preset_source_texture_channel_resolver_until_dcc_slots_export",
            "Xineng PBR channel resolver runtime mode mismatch",
            failures,
        )
        require(
            stats.get("xineng_pbr_channel_resolver_focus_active") is True,
            "Xineng PBR channel resolver inactive",
            failures,
        )
        require(int(stats.get("xineng_pbr_channel_groups", 0)) >= 6, "PBR channel groups missing", failures)
        require(
            int(stats.get("xineng_pbr_semantic_texture_files", 0)) >= 69,
            "PBR semantic texture file count too low",
            failures,
        )
        require(
            int(stats.get("xineng_pbr_semantic_texture_bytes", 0)) >= 14700000,
            "PBR semantic texture byte count too low",
            failures,
        )
        require(
            int(stats.get("xineng_pbr_needs_material_slot_mapping_files", 0)) >= 771,
            "PBR material-slot mapping queue should report unmapped textures",
            failures,
        )
        require(
            int(stats.get("xineng_pbr_needs_material_slot_mapping_bytes", 0)) >= 467000000,
            "PBR material-slot mapping byte queue too low",
            failures,
        )
        require(
            int(stats.get("xineng_pbr_base_color_candidates", 0)) >= 61,
            "PBR base-color candidates too low",
            failures,
        )
        require(
            int(stats.get("xineng_pbr_normal_bump_candidates", 0)) >= 4,
            "PBR normal/bump candidates too low",
            failures,
        )
        require(
            int(stats.get("xineng_pbr_rough_spec_candidates", 0)) >= 2,
            "PBR rough/spec candidates too low",
            failures,
        )
        require(int(stats.get("xineng_pbr_metal_candidates", 0)) >= 4, "PBR metal candidates too low", failures)
        require(
            int(stats.get("xineng_pbr_emissive_light_candidates", 0)) >= 6,
            "PBR emissive/light candidates too low",
            failures,
        )
        require(
            int(stats.get("xineng_pbr_opacity_alpha_candidates", 0)) >= 1,
            "PBR opacity/alpha candidates missing",
            failures,
        )
        require(
            int(stats.get("xineng_pbr_channel_package_links", 0)) >= 7,
            "PBR package links missing",
            failures,
        )
        require(
            int(stats.get("xineng_pbr_atlas_packing_slots", 0)) >= 6,
            "PBR atlas packing slots missing",
            failures,
        )
        require(
            int(stats.get("xineng_pbr_uv_set_remap_rows", 0)) >= 4,
            "PBR UV remap rows missing",
            failures,
        )
        require(
            int(stats.get("xineng_pbr_unmapped_queue_bars", 0)) >= 7,
            "PBR unmapped queue bars missing",
            failures,
        )
        require(
            int(stats.get("xineng_pbr_channel_resolver_scene_instances", 0)) >= 99,
            "PBR channel resolver scene instances too sparse",
            failures,
        )
        require(
            stats.get("cloud_geometry_visible") is False,
            "materials preset should hide cloud geometry over material inspection",
            failures,
        )
        require(float(stats.get("rain", 0.0)) >= 0.15, "materials preset should expose wet material response", failures)
        require(int(stats.get("point_lights", 0)) >= 8, "materials preset should keep material samples readable", failures)
        require(float(stats.get("fog_density", 1.0)) <= 0.10, "materials preset should keep material detail readable", failures)
    elif preset == "process":
        require(
            stats.get("process_management_focus_active") is True,
            "process preset did not activate process-management overlay",
            failures,
        )
        expected_isolation_count = 4 if expected_process_counts["isolation"] else 0
        expected_bypass_count = 3 if expected_process_counts["isolation"] else 0
        expected_dependency_count = 6 if expected_process_counts["isolation"] else 0
        expected_impact_count = 5 if expected_process_counts["isolation"] else 0
        expected_topology_count = 7 if expected_process_counts["isolation"] else 0
        expected_shutdown_count = 2 if expected_process_counts["shutdown"] else 0
        expected_source_lineage_edges = expected_process_counts["edges"]
        expected_source_upstream_cards = 3 if expected_process_counts["upstream"] else 0
        expected_source_downstream_cards = 4 if expected_process_counts["downstream"] else 0
        expected_source_isolation_links = 6 if expected_process_counts["isolation"] else 0
        expected_source_shutdown_locks = 2 if expected_process_counts["shutdown"] else 0
        expected_control_command_links = expected_process_counts["edges"]
        expected_control_ack_cards = (3 if expected_process_counts["upstream"] else 0) + (
            4 if expected_process_counts["downstream"] else 0
        )
        expected_control_interlock_relays = 6 if expected_process_counts["isolation"] else 0
        expected_control_alarm_buses = 4 if expected_process_counts["isolation"] or expected_process_counts["shutdown"] else 0
        expected_control_shutdown_locks = 2 if expected_process_counts["shutdown"] else 0
        expected_control_scene_instances = (
            10
            + 1
            + 6
            + 5
            + expected_control_command_links
            + expected_control_ack_cards
            + expected_control_interlock_relays
            + expected_control_alarm_buses
            + expected_control_shutdown_locks
            + 10
        )
        expected_telemetry_scene_instances = 6 + 18 + 4 + 6 + 5 + 6 + 4 + 6 + 6
        expected_source_scene_instances = (
            7
            + 7
            + expected_source_lineage_edges
            + expected_source_upstream_cards
            + expected_source_downstream_cards
            + expected_source_isolation_links
            + expected_source_shutdown_locks
            + 7
            + 7
            + 7
        )
        require(
            int(stats.get("process_management_isolation_valves", -1)) == expected_isolation_count,
            "process preset isolation valve count mismatch",
            failures,
        )
        require(
            int(stats.get("process_management_bypass_links", -1)) == expected_bypass_count,
            "process preset bypass link count mismatch",
            failures,
        )
        require(
            int(stats.get("process_management_dependency_beams", -1)) == expected_dependency_count,
            "process preset dependency beam count mismatch",
            failures,
        )
        require(
            int(stats.get("process_management_impact_zones", -1)) == expected_impact_count,
            "process preset impact zone count mismatch",
            failures,
        )
        require(
            int(stats.get("process_management_shutdown_stages", -1)) == expected_shutdown_count,
            "process preset shutdown stage count mismatch",
            failures,
        )
        require(
            int(stats.get("process_management_topology_panel_nodes", -1)) == expected_topology_count,
            "process preset topology panel node count mismatch",
            failures,
        )
        require(
            int(stats.get("process_management_flow_pulse_markers", -1)) == expected_process_counts["pulses"],
            "process preset flow pulse marker count mismatch",
            failures,
        )
        require(
            stats.get("xineng_process_source_impact_focus_active") is True,
            "process preset did not activate Xineng source-impact proxy",
            failures,
        )
        require(
            int(stats.get("xineng_process_source_impact_unit_anchors", -1)) == 7,
            "process source-impact unit anchor count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_source_impact_source_bindings", -1)) == 7,
            "process source-impact binding count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_source_impact_lineage_edges", -1)) == expected_source_lineage_edges,
            "process source-impact lineage edge count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_source_impact_upstream_cards", -1)) == expected_source_upstream_cards,
            "process source-impact upstream card count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_source_impact_downstream_cards", -1)) == expected_source_downstream_cards,
            "process source-impact downstream card count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_source_impact_isolation_links", -1)) == expected_source_isolation_links,
            "process source-impact isolation link count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_source_impact_shutdown_locks", -1)) == expected_source_shutdown_locks,
            "process source-impact shutdown lock count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_source_impact_scada_readouts", -1)) == 7,
            "process source-impact SCADA readout count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_source_impact_budget_columns", -1)) == 7,
            "process source-impact budget column count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_source_impact_source_beacons", -1)) == 7,
            "process source-impact beacon count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_source_impact_scene_instances", -1)) == expected_source_scene_instances,
            "process source-impact scene instance count mismatch",
            failures,
        )
        require(
            stats.get("xineng_process_control_interlock_pass")
            == "max_screen_detector_firepower_scada_interlock_proxy",
            "process control interlock source pass missing",
            failures,
        )
        require(
            stats.get("xineng_process_control_interlock_runtime_mode")
            == "procedural_scada_interlock_proxy_until_dcc_export",
            "process control interlock runtime mode mismatch",
            failures,
        )
        require(
            stats.get("xineng_process_control_interlock_focus_active") is True,
            "process preset did not activate control interlock source proxy",
            failures,
        )
        require(
            int(stats.get("xineng_process_control_interlock_assets", 0)) >= 10,
            "process control interlock source asset count too low",
            failures,
        )
        require(
            int(stats.get("xineng_process_control_interlock_source_asset_bytes", 0)) >= 15000000,
            "process control interlock source bytes too low",
            failures,
        )
        require(
            int(stats.get("xineng_process_control_rooms", -1)) == 1,
            "process control room count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_control_detector_nodes", -1)) == 6,
            "process detector node count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_control_operator_screens", -1)) == 5,
            "process operator screen count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_control_command_links", -1)) == expected_control_command_links,
            "process control command link count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_control_ack_cards", -1)) == expected_control_ack_cards,
            "process control ack-card count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_control_interlock_relays", -1)) == expected_control_interlock_relays,
            "process control interlock relay count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_control_alarm_buses", -1)) == expected_control_alarm_buses,
            "process control alarm bus count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_control_shutdown_locks", -1)) == expected_control_shutdown_locks,
            "process control shutdown lock count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_control_source_beacons", -1)) == 10,
            "process control source beacon count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_process_control_scene_instances", -1)) == expected_control_scene_instances,
            "process control scene instance count mismatch",
            failures,
        )
        require(
            stats.get("process_telemetry_trend_active") is True,
            "process preset did not activate process telemetry trend overlay",
            failures,
        )
        require(
            int(stats.get("process_telemetry_channels", -1)) == 6,
            "process telemetry channel count mismatch",
            failures,
        )
        require(
            int(stats.get("process_telemetry_trend_samples", -1)) == 18,
            "process telemetry trend sample count mismatch",
            failures,
        )
        require(
            int(stats.get("process_telemetry_threshold_bands", -1)) == 4,
            "process telemetry threshold band count mismatch",
            failures,
        )
        require(
            int(stats.get("process_telemetry_alarm_beacons", -1)) == 6,
            "process telemetry alarm beacon count mismatch",
            failures,
        )
        require(
            int(stats.get("process_telemetry_interlock_cards", -1)) == 5,
            "process telemetry interlock card count mismatch",
            failures,
        )
        require(
            int(stats.get("process_telemetry_sensor_vote_inputs", -1)) == 6,
            "process telemetry sensor vote input count mismatch",
            failures,
        )
        require(
            int(stats.get("process_telemetry_control_loops", -1)) == 4,
            "process telemetry control loop count mismatch",
            failures,
        )
        require(
            int(stats.get("process_telemetry_orthogonal_links", -1)) == 6,
            "process telemetry orthogonal link count mismatch",
            failures,
        )
        require(
            int(stats.get("process_telemetry_flanged_sensor_tie_ins", -1)) == 6,
            "process telemetry flanged sensor tie-in count mismatch",
            failures,
        )
        require(
            float(stats.get("process_telemetry_pressure_mpa", 0.0)) >= 2.3,
            "process telemetry pressure reading missing",
            failures,
        )
        require(
            float(stats.get("process_telemetry_temperature_c", 0.0)) >= 80.0,
            "process telemetry temperature reading missing",
            failures,
        )
        require(
            float(stats.get("process_telemetry_flow_tph", 0.0)) >= 120.0,
            "process telemetry flow reading missing",
            failures,
        )
        require(
            float(stats.get("process_telemetry_gas_alarm_ppm", 0.0)) >= 25.0,
            "process telemetry gas alarm threshold missing",
            failures,
        )
        require(
            float(stats.get("process_telemetry_control_latency_ms", 9999.0)) <= 100.0,
            "process telemetry control latency too high",
            failures,
        )
        require(
            float(stats.get("process_telemetry_health_percent", 0.0)) >= 90.0,
            "process telemetry health score too low",
            failures,
        )
        require(
            stats.get("process_telemetry_tracks_process_modes") is True,
            "process telemetry does not report process-mode tracking",
            failures,
        )
        require(
            int(stats.get("process_telemetry_scene_instances", -1)) == expected_telemetry_scene_instances,
            "process telemetry scene instance count mismatch",
            failures,
        )
        require(float(stats.get("process_flow_route_length_m", 0.0)) >= 600.0, "process preset route too short", failures)
        require(int(stats.get("point_lights", 0)) >= 6, "process preset should light the process overlay", failures)
        require(float(stats.get("fog_density", 1.0)) <= 0.10, "process preset should keep overlays readable", failures)
    elif preset == "disassembly":
        expected_step = args.disassembly_step or "all"
        expected_disassembly_counts = {
            "all": {
                "parts": 8,
                "links": 7,
                "fasteners": 32,
                "callouts": 4,
            },
            "base": {
                "parts": 1,
                "links": 0,
                "fasteners": 0,
                "callouts": 0,
            },
            "housing": {
                "parts": 2,
                "links": 2,
                "fasteners": 0,
                "callouts": 2,
            },
            "rotor": {
                "parts": 2,
                "links": 2,
                "fasteners": 8,
                "callouts": 1,
            },
            "seal": {
                "parts": 2,
                "links": 2,
                "fasteners": 16,
                "callouts": 0,
            },
            "motor": {
                "parts": 1,
                "links": 1,
                "fasteners": 8,
                "callouts": 1,
            },
            "fasteners": {
                "parts": 1,
                "links": 1,
                "fasteners": 32,
                "callouts": 0,
            },
        }[expected_step]
        require(
            stats.get("equipment_disassembly_focus_active") is True,
            "disassembly preset did not activate focused inspection overlay",
            failures,
        )
        require(
            stats.get("equipment_disassembly_visual_clearance_pass")
            == "suppressed_cloud_fire_gas_geometry_for_part_inspection",
            "disassembly visual-clearance pass missing",
            failures,
        )
        require(
            stats.get("cloud_geometry_visible") is False,
            "disassembly preset should hide cloud geometry over the inspection bench",
            failures,
        )
        require(
            stats.get("equipment_disassembly_step_mode") == expected_step,
            f"disassembly step mismatch: {stats.get('equipment_disassembly_step_mode')} != {expected_step}",
            failures,
        )
        require(
            stats.get("equipment_disassembly_interactive_controls") is True,
            "disassembly interactive controls not reported",
            failures,
        )
        require(int(stats.get("equipment_disassembly_interactive_modes", 0)) >= 7, "disassembly step count too low", failures)
        require(
            stats.get("equipment_disassembly_interactive_hotkeys") == "0_tab",
            "disassembly hotkeys not reported",
            failures,
        )
        require(
            int(stats.get("equipment_disassembly_active_parts", -1)) == expected_disassembly_counts["parts"],
            "disassembly active part count mismatch",
            failures,
        )
        require(
            int(stats.get("equipment_disassembly_active_leader_links", -1)) == expected_disassembly_counts["links"],
            "disassembly active leader link count mismatch",
            failures,
        )
        require(
            int(stats.get("equipment_disassembly_workstations", -1)) == expected_disassembly_counts["parts"],
            "disassembly workstation count mismatch",
            failures,
        )
        require(
            int(stats.get("equipment_disassembly_sequence_arrows", -1)) == expected_disassembly_counts["links"],
            "disassembly sequence arrow count mismatch",
            failures,
        )
        require(
            int(stats.get("equipment_disassembly_fastener_markers", -1)) == expected_disassembly_counts["fasteners"],
            "disassembly fastener marker count mismatch",
            failures,
        )
        require(
            int(stats.get("equipment_disassembly_material_callouts", -1)) == expected_disassembly_counts["callouts"],
            "disassembly material callout count mismatch",
            failures,
        )
        require(
            int(stats.get("equipment_disassembly_ghost_parts", -1)) == expected_disassembly_counts["parts"],
            "disassembly ghost reference part count mismatch",
            failures,
        )
        require(
            int(stats.get("equipment_disassembly_alignment_axes", 0)) >= 3,
            "disassembly alignment axes missing",
            failures,
        )
        require(
            stats.get("xineng_equipment_source_disassembly_focus_active") is True,
            "disassembly preset did not activate Xineng source equipment disassembly",
            failures,
        )
        require(
            int(stats.get("xineng_equipment_source_disassembly_bays", 0)) >= 5,
            "Xineng source equipment disassembly bays missing",
            failures,
        )
        require(
            int(stats.get("xineng_equipment_source_disassembly_exploded_components", 0)) >= 28,
            "Xineng source equipment exploded components too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_equipment_source_disassembly_fastener_clusters", 0)) >= 22,
            "Xineng source equipment fastener clusters too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_equipment_source_disassembly_wiring_harnesses", 0)) >= 8,
            "Xineng source equipment wiring harnesses missing",
            failures,
        )
        require(
            int(stats.get("xineng_equipment_source_disassembly_material_tags", 0)) >= 5,
            "Xineng source equipment material tags missing",
            failures,
        )
        require(
            int(stats.get("xineng_equipment_source_disassembly_inspection_axes", 0)) >= 6,
            "Xineng source equipment inspection axes missing",
            failures,
        )
        require(
            int(stats.get("xineng_equipment_source_disassembly_source_beacons", 0)) >= 5,
            "Xineng source equipment beacons missing",
            failures,
        )
        require(
            int(stats.get("xineng_equipment_source_disassembly_scene_instances", 0)) >= 84,
            "Xineng source equipment disassembly scene instances too sparse",
            failures,
        )
        require(int(stats.get("point_lights", 0)) >= 5, "disassembly preset should light the inspection bench", failures)
        require(float(stats.get("fog_density", 1.0)) <= 0.08, "disassembly preset should keep part details readable", failures)
        require(
            float(stats.get("particle_intensity", 1.0)) <= 0.30,
            "disassembly preset should suppress particles around part inspection",
            failures,
        )
    elif preset == "lighting":
        require(int(stats.get("point_lights", 0)) >= 12, "lighting preset should enable dense point lights", failures)
        require(float(stats.get("sun_intensity", 1.0)) <= 0.55, "lighting preset should dim the sun", failures)
        require(float(stats.get("fog_density", 1.0)) <= 0.02, "lighting preset should start with visibility fog disabled", failures)
        require(float(stats.get("cloud_amount", 0.0)) >= 1.0, "lighting preset should use dramatic cloud cover", failures)
        require(stats.get("cloud_geometry_visible") is False, "lighting preset should avoid visible ellipsoid cloud geometry", failures)
    elif preset == "particles":
        expected_gas_mode = args.gas_mode or "all"
        expected_gas_plume = expected_gas_mode in ("all", "plume")
        expected_gas_footprint = expected_gas_mode in ("all", "footprint")
        expected_gas_evacuation = expected_gas_mode in ("all", "evacuation")
        expected_gas_sensors = expected_gas_mode in ("all", "sensors")
        expected_gas_layers = sum(
            int(value)
            for value in (
                expected_gas_plume,
                expected_gas_footprint,
                expected_gas_evacuation,
                expected_gas_sensors,
            )
        )
        require(float(stats.get("particle_intensity", 0.0)) >= 1.25, "particles preset should boost particles", failures)
        require(float(stats.get("wind_speed", 0.0)) >= 5.5, "particles preset should show wind-driven plume", failures)
        require(stats.get("explosion_effect_active") is True, "particles preset did not activate explosion effects", failures)
        require(
            int(stats.get("particle_instances", 0)) >= (1100 if expected_gas_plume else 880),
            "particles preset particle budget too low for the selected gas mode",
            failures,
        )
        require(
            stats.get("gas_diffusion_analysis_active") is True,
            "particles preset did not activate gas diffusion analysis",
            failures,
        )
        require(
            stats.get("gas_diffusion_mode") == expected_gas_mode,
            f"gas diffusion mode mismatch: {stats.get('gas_diffusion_mode')} != {expected_gas_mode}",
            failures,
        )
        require(
            stats.get("gas_diffusion_interactive_controls") is True,
            "gas diffusion interactive controls not reported",
            failures,
        )
        require(int(stats.get("gas_diffusion_interactive_modes", 0)) >= 5, "gas diffusion mode count too low", failures)
        require(
            stats.get("gas_diffusion_interactive_hotkeys") == "5_h",
            "gas diffusion hotkeys not reported",
            failures,
        )
        require(
            int(stats.get("gas_diffusion_active_layers", -1)) == expected_gas_layers,
            "gas diffusion active layer count mismatch",
            failures,
        )
        require(stats.get("gas_diffusion_plume_active") is expected_gas_plume, "gas plume active flag mismatch", failures)
        require(
            stats.get("gas_diffusion_footprint_active") is expected_gas_footprint,
            "gas footprint active flag mismatch",
            failures,
        )
        require(
            stats.get("gas_diffusion_evacuation_active") is expected_gas_evacuation,
            "gas evacuation active flag mismatch",
            failures,
        )
        require(
            stats.get("gas_diffusion_sensors_active") is expected_gas_sensors,
            "gas sensor active flag mismatch",
            failures,
        )
        if expected_gas_footprint:
            require(
                int(stats.get("gas_diffusion_concentration_bands", 0)) >= 4,
                "gas diffusion concentration bands too sparse",
                failures,
            )
        else:
            require(
                int(stats.get("gas_diffusion_concentration_bands", -1)) == 0,
                "gas concentration bands should be hidden",
                failures,
            )
        if expected_gas_footprint or expected_gas_sensors:
            require(
                int(stats.get("gas_diffusion_crosswind_samples", 0)) >= 10,
                "gas diffusion crosswind samples too sparse",
                failures,
            )
        else:
            require(
                int(stats.get("gas_diffusion_crosswind_samples", -1)) == 0,
                "gas crosswind samples should be hidden",
                failures,
            )
        if expected_gas_evacuation:
            require(
                int(stats.get("gas_diffusion_evacuation_vectors", 0)) >= 3,
                "gas diffusion evacuation vectors too sparse",
                failures,
            )
        else:
            require(
                int(stats.get("gas_diffusion_evacuation_vectors", -1)) == 0,
                "gas evacuation vectors should be hidden",
                failures,
            )
        if expected_gas_sensors:
            require(int(stats.get("gas_diffusion_sensor_posts", 0)) >= 6, "gas sensor posts too sparse", failures)
            require(
                int(stats.get("gas_diffusion_wind_probe_vectors", 0)) >= 8,
                "gas wind probe vectors too sparse",
                failures,
            )
        else:
            require(int(stats.get("gas_diffusion_sensor_posts", -1)) == 0, "gas sensor posts should be hidden", failures)
            require(
                int(stats.get("gas_diffusion_wind_probe_vectors", -1)) == 0,
                "gas wind probe vectors should be hidden",
                failures,
            )
        require(
            float(stats.get("gas_diffusion_downwind_distance_m", 0.0)) >= 390.0,
            "gas diffusion downwind distance too short for particles preset wind",
            failures,
        )
        require(
            float(stats.get("gas_diffusion_exclusion_area_sqm", 0.0)) >= 30000.0,
            "gas diffusion exclusion area too small",
            failures,
        )
        require(
            abs(float(stats.get("gas_diffusion_wind_speed_mps", -1.0)) - float(stats.get("wind_speed", -2.0))) <= 0.05,
            "gas diffusion wind-speed stat not tied to active wind setting",
            failures,
        )
        active_wind_speed = float(stats.get("wind_speed", 0.0))
        active_wind_dir = float(stats.get("wind_dir_degrees", 0.0))
        expected_downwind_m = 210.0 + min(max(active_wind_speed, 0.0), 18.0) * 33.0
        expected_crosswind_m = 32.0 + min(max(active_wind_speed, 0.0), 18.0) * 3.5
        expected_response = min(max(active_wind_speed / 12.0, 0.0), 1.5)
        expected_shear = 0.35 + expected_response * 0.45
        expected_source_plume_links = expected_gas_plume or expected_gas_footprint or expected_gas_sensors
        expected_source_alarm_links = expected_gas_sensors or expected_gas_evacuation
        expected_gas_source_instances = (
            7
            + 3
            + 8
            + 6
            + 7
            + (6 if expected_gas_sensors else 0)
            + (10 if expected_source_plume_links else 0)
            + (4 if expected_source_alarm_links else 0)
            + (3 if expected_gas_evacuation else 0)
        )
        expected_fire_explosion_source_instances = 57
        angle = math.radians(active_wind_dir)
        expected_downwind_vector = [math.cos(angle) * expected_downwind_m, math.sin(angle) * expected_downwind_m]
        expected_crosswind_vector = [-math.sin(angle) * expected_crosswind_m, math.cos(angle) * expected_crosswind_m]
        require(
            stats.get("gas_diffusion_realtime_wind_field_pass")
            == "live_wind_vector_streamlines_crosswind_envelope",
            "gas realtime wind-field pass missing",
            failures,
        )
        require(
            stats.get("gas_diffusion_wind_field_active") is True,
            "gas realtime wind-field overlay not active",
            failures,
        )
        require(
            int(stats.get("gas_diffusion_wind_field_streamlines", 0)) >= 12,
            "gas wind-field streamlines too sparse",
            failures,
        )
        require(
            int(stats.get("gas_diffusion_wind_field_envelope_ribs", 0)) >= 8,
            "gas wind-field envelope ribs too sparse",
            failures,
        )
        require(
            int(stats.get("gas_diffusion_wind_speed_samples", 0)) >= 6,
            "gas wind-field speed samples too sparse",
            failures,
        )
        require(
            int(stats.get("gas_diffusion_wind_profile_cards", 0)) >= 4,
            "gas wind-field profile cards too sparse",
            failures,
        )
        require(
            abs(float(stats.get("gas_diffusion_wind_response_index", -9.0)) - expected_response) <= 0.01,
            "gas wind response index does not track active wind speed",
            failures,
        )
        require(
            abs(float(stats.get("gas_diffusion_wind_shear_factor", -9.0)) - expected_shear) <= 0.01,
            "gas wind shear factor does not track active wind speed",
            failures,
        )
        actual_downwind_vector = stats.get("gas_diffusion_downwind_vector_m", [])
        actual_crosswind_vector = stats.get("gas_diffusion_crosswind_vector_m", [])
        require(len(actual_downwind_vector) == 2, "gas downwind vector should have two components", failures)
        require(len(actual_crosswind_vector) == 2, "gas crosswind vector should have two components", failures)
        if len(actual_downwind_vector) == 2:
            require(
                all(abs(float(actual_downwind_vector[i]) - expected_downwind_vector[i]) <= 0.5 for i in range(2)),
                "gas downwind vector does not follow active wind direction/speed",
                failures,
            )
        if len(actual_crosswind_vector) == 2:
            require(
                all(abs(float(actual_crosswind_vector[i]) - expected_crosswind_vector[i]) <= 0.5 for i in range(2)),
                "gas crosswind vector does not follow active wind direction/speed",
                failures,
            )
        require(
            stats.get("xineng_gas_wind_source_sensor_pass")
            == "max_detector_tower_camera_power_wind_profile_gas_sensor_proxy",
            "gas wind source sensor network pass missing",
            failures,
        )
        require(
            stats.get("xineng_gas_wind_source_sensor_runtime_mode")
            == "procedural_wind_gas_sensor_proxy_until_dcc_export",
            "gas wind source sensor runtime mode mismatch",
            failures,
        )
        require(
            stats.get("xineng_gas_wind_source_sensor_focus_active") is True,
            "gas wind source sensor network should be active for particles preset",
            failures,
        )
        require(
            int(stats.get("xineng_gas_wind_source_sensor_assets", 0)) >= 7,
            "gas wind source sensor asset coverage too low",
            failures,
        )
        require(
            int(stats.get("xineng_gas_wind_source_sensor_source_asset_bytes", 0)) >= 198000000,
            "gas wind source sensor represented source bytes too low",
            failures,
        )
        require(
            int(stats.get("xineng_gas_wind_source_profile_towers", 0)) >= 3,
            "gas wind source profile towers too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_gas_wind_source_wind_samples", 0)) >= 8,
            "gas wind source wind samples too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_gas_wind_source_profile_cards", 0)) >= 6,
            "gas wind source profile cards too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_gas_wind_source_beacons", 0)) >= 7,
            "gas wind source beacons too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_gas_wind_source_scene_instances", 0)) >= expected_gas_source_instances,
            "gas wind source scene instance count too low for selected gas mode",
            failures,
        )
        if expected_gas_sensors:
            require(
                int(stats.get("xineng_gas_wind_source_sensor_nodes", 0)) >= 6,
                "gas wind source sensor nodes too sparse",
                failures,
            )
        else:
            require(
                int(stats.get("xineng_gas_wind_source_sensor_nodes", -1)) == 0,
                "gas wind source sensor nodes should be hidden",
                failures,
            )
        if expected_source_plume_links:
            require(
                int(stats.get("xineng_gas_wind_source_plume_links", 0)) >= 10,
                "gas wind source plume sample links too sparse",
                failures,
            )
        else:
            require(
                int(stats.get("xineng_gas_wind_source_plume_links", -1)) == 0,
                "gas wind source plume sample links should be hidden",
                failures,
            )
        if expected_source_alarm_links:
            require(
                int(stats.get("xineng_gas_wind_source_alarm_links", 0)) >= 4,
                "gas wind source alarm links too sparse",
                failures,
            )
        else:
            require(
                int(stats.get("xineng_gas_wind_source_alarm_links", -1)) == 0,
                "gas wind source alarm links should be hidden",
                failures,
            )
        if expected_gas_evacuation:
            require(
                int(stats.get("xineng_gas_wind_source_evacuation_gates", 0)) >= 3,
                "gas wind source evacuation gates too sparse",
                failures,
            )
        else:
            require(
                int(stats.get("xineng_gas_wind_source_evacuation_gates", -1)) == 0,
                "gas wind source evacuation gates should be hidden",
                failures,
            )
        require(
            abs(float(stats.get("xineng_gas_wind_source_response_index", -9.0)) - expected_response) <= 0.01,
            "gas wind source response index does not track active wind speed",
            failures,
        )
        require(
            abs(float(stats.get("xineng_gas_wind_source_downwind_probe_m", -9.0)) - expected_downwind_m * 0.76)
            <= 1.0,
            "gas wind source downwind probe does not track active wind speed",
            failures,
        )
        require(
            abs(float(stats.get("xineng_gas_wind_source_crosswind_probe_m", -9.0)) - expected_crosswind_m) <= 0.5,
            "gas wind source crosswind probe does not track active wind speed",
            failures,
        )
        require(
            stats.get("xineng_fire_explosion_source_response_pass")
            == "max_detector_firewater_camera_alarm_source_response_proxy",
            "fire/explosion source response pass missing",
            failures,
        )
        require(
            stats.get("xineng_fire_explosion_source_runtime_mode")
            == "procedural_fire_explosion_response_proxy_until_dcc_export",
            "fire/explosion source response runtime mode mismatch",
            failures,
        )
        require(
            stats.get("xineng_fire_explosion_source_focus_active") is True,
            "fire/explosion source response network should be active for particles preset",
            failures,
        )
        require(
            int(stats.get("xineng_fire_explosion_source_assets", 0)) >= 9,
            "fire/explosion source asset coverage too low",
            failures,
        )
        require(
            int(stats.get("xineng_fire_explosion_source_asset_bytes", 0)) >= 153000000,
            "fire/explosion represented source bytes too low",
            failures,
        )
        require(
            int(stats.get("xineng_fire_explosion_detector_triggers", 0)) >= 6,
            "fire/explosion detector triggers too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_fire_explosion_ignition_links", 0)) >= 6,
            "fire/explosion ignition links too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_fire_explosion_shockwave_source_rings", 0)) >= 5,
            "fire/explosion shockwave source rings too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_fire_explosion_overpressure_panels", 0)) >= 6,
            "fire/explosion overpressure panels too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_fire_explosion_firewater_response_links", 0)) >= 8,
            "fire/explosion firewater response links too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_fire_explosion_camera_tracks", 0)) >= 4,
            "fire/explosion camera tracks too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_fire_explosion_alarm_displays", 0)) >= 4,
            "fire/explosion alarm displays too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_fire_explosion_source_beacons", 0)) >= 9,
            "fire/explosion source beacons too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_fire_explosion_source_scene_instances", 0))
            >= expected_fire_explosion_source_instances,
            "fire/explosion source scene instance count too low",
            failures,
        )
    elif preset == "weather":
        expected_weather_fountain_mode = args.fountain_mode or "all"
        expected_weather_wind_links = expected_weather_fountain_mode in ("all", "wind")
        expected_weather_source_instances = (
            12 + 5 + 4 + 8 + 6 + 5 + 6 + 12 + (8 if expected_weather_wind_links else 0)
        )
        require(float(stats.get("rain", 0.0)) >= 0.7, "weather preset rain too low", failures)
        require(float(stats.get("snow", 0.0)) >= 0.18, "weather preset snow too low", failures)
        require(float(stats.get("fog_density", 0.0)) >= 0.35, "weather preset fog too low", failures)
        require(int(stats.get("rain_streaks", 0)) >= 700, "rain streak geometry too sparse", failures)
        require(int(stats.get("snow_flakes", 0)) >= 340, "snow flake geometry too sparse", failures)
        require(int(stats.get("puddle_instances", 0)) >= 40, "rain puddle feedback missing", failures)
        require(int(stats.get("rain_splash_marks", 0)) >= 80, "rain splash feedback missing", failures)
        require(float(stats.get("weather_wetness", 0.0)) >= 0.70, "wet weather surface response too weak", failures)
        require(
            stats.get("xineng_weather_source_telemetry_pass") == "max_display_lamp_firetower_weather_warning_proxy",
            "weather source telemetry pass missing",
            failures,
        )
        require(
            stats.get("xineng_weather_source_telemetry_runtime_mode")
            == "procedural_weather_telemetry_proxy_until_dcc_export",
            "weather source telemetry runtime mode mismatch",
            failures,
        )
        require(
            stats.get("xineng_weather_source_telemetry_focus_active") is True,
            "weather source telemetry should be active for weather preset",
            failures,
        )
        require(
            int(stats.get("xineng_weather_source_telemetry_assets", 0)) >= 12,
            "weather source telemetry asset coverage too low",
            failures,
        )
        require(
            int(stats.get("xineng_weather_source_telemetry_source_asset_bytes", 0)) >= 17300000,
            "weather source telemetry represented source bytes too low",
            failures,
        )
        require(
            int(stats.get("xineng_weather_source_telemetry_masts", 0)) >= 5,
            "weather telemetry masts too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_weather_source_alert_displays", 0)) >= 4,
            "weather alert displays too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_weather_source_lamp_wet_beacons", 0)) >= 8,
            "weather lamp wet beacons too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_weather_source_rain_gauges", 0)) >= 6,
            "weather rain gauges too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_weather_source_fog_visibility_posts", 0)) >= 5,
            "weather fog visibility posts too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_weather_source_snow_depth_pins", 0)) >= 6,
            "weather snow depth pins too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_weather_source_beacons", 0)) >= 12,
            "weather source beacons too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_weather_source_scene_instances", 0)) >= expected_weather_source_instances,
            "weather source telemetry scene instances too low for selected fountain mode",
            failures,
        )
        require(
            float(stats.get("xineng_weather_source_wetness_index", 0.0)) >= 0.70,
            "weather source telemetry wetness index too low",
            failures,
        )
        if expected_weather_wind_links:
            require(
                int(stats.get("xineng_weather_source_fountain_wind_links", 0)) >= 8,
                "weather source fountain wind links too sparse",
                failures,
            )
            require(
                float(stats.get("xineng_weather_source_fountain_wind_drift_m", 0.0)) >= 1.0,
                "weather source fountain wind drift not reported",
                failures,
            )
        else:
            require(
                int(stats.get("xineng_weather_source_fountain_wind_links", -1)) == 0,
                "weather source fountain wind links should be hidden",
                failures,
            )
            require(
                float(stats.get("xineng_weather_source_fountain_wind_drift_m", -1.0)) == 0.0,
                "weather source fountain wind drift should be hidden",
                failures,
            )
    elif preset == "safety":
        require(
            stats.get("xineng_max_emergency_proxy_focus_active") is True,
            "safety preset did not activate MAX emergency proxy focus",
            failures,
        )
        require(
            int(stats.get("point_lights", 0)) >= 8,
            "safety preset should keep firefighting equipment lights readable",
            failures,
        )
        require(
            float(stats.get("rain", 0.0)) >= 0.12,
            "safety preset should show wet deluge/weather interaction",
            failures,
        )
        require(
            float(stats.get("wind_speed", 0.0)) >= 5.0,
            "safety preset should expose wind drift on water monitors",
            failures,
        )
        require(
            int(stats.get("xineng_max_emergency_hydrant_stations", 0)) >= 6,
            "safety preset hydrant proxies too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_max_emergency_proxy_beacons", 0)) >= 13,
            "safety preset proxy beacons too sparse",
            failures,
        )
        require(
            float(stats.get("xineng_max_emergency_water_network_length_m", 0.0)) >= 250.0,
            "safety preset water network route too short",
            failures,
        )
    elif preset == "sprinkler":
        expected_hydraulic_water_source = expected_sprinkler_mode in ("all", "heads", "monitors", "deluge")
        expected_hydraulic_pressure = expected_sprinkler_mode in ("all", "heads", "monitors")
        expected_hydraulic_pumps = expected_sprinkler_mode in ("all", "heads", "runoff")
        expected_hydraulic_instances = (
            10
            + 2
            + 10
            + (6 if expected_hydraulic_water_source else 0)
            + (8 if expected_hydraulic_pressure else 0)
            + (4 if expected_hydraulic_pumps else 0)
            + (2 if expected_sprinkler_mode in ("all", "monitors") else 0)
            + (4 if expected_sprinkler_mode in ("all", "deluge") else 0)
            + (8 if expected_sprinkler_mode in ("all", "runoff") else 0)
        )
        require(stats.get("sprinkler_focus_active") is True, "sprinkler preset did not activate detail overlay", failures)
        require(
            int(stats.get("sprinkler_activation_zones", -1)) == expected_sprinkler_counts["zones"],
            "sprinkler activation zone count mismatch",
            failures,
        )
        require(
            int(stats.get("sprinkler_coverage_footprints", -1)) == expected_sprinkler_counts["coverage"],
            "sprinkler coverage footprint count mismatch",
            failures,
        )
        if expected_sprinkler_counts["coverage"]:
            require(
                float(stats.get("sprinkler_coverage_area_sqm", 0.0)) >= 8000.0,
                "sprinkler coverage area too small",
                failures,
            )
        else:
            require(
                float(stats.get("sprinkler_coverage_area_sqm", -1.0)) == 0.0,
                "sprinkler coverage area should be hidden in selected mode",
                failures,
            )
        require(
            int(stats.get("sprinkler_zone_control_valves", -1)) == expected_sprinkler_counts["valves"],
            "sprinkler zone valve count mismatch",
            failures,
        )
        require(
            int(stats.get("sprinkler_pressure_gauges", -1)) == expected_sprinkler_counts["gauges"],
            "sprinkler pressure gauge count mismatch",
            failures,
        )
        require(
            int(stats.get("sprinkler_pump_skids", -1)) == expected_sprinkler_counts["pumps"],
            "sprinkler pump skid count mismatch",
            failures,
        )
        require(
            int(stats.get("sprinkler_deluge_curtains", -1)) == expected_sprinkler_counts["deluge"],
            "sprinkler deluge curtain count mismatch",
            failures,
        )
        require(
            int(stats.get("sprinkler_runoff_channels", -1)) == expected_sprinkler_counts["runoff"],
            "sprinkler runoff channel count mismatch",
            failures,
        )
        require(
            int(stats.get("sprinkler_wind_drift_vectors", -1)) == expected_sprinkler_counts["wind_vectors"],
            "sprinkler wind drift vector count mismatch",
            failures,
        )
        require(
            int(stats.get("sprinkler_wet_floor_patches", -1)) == expected_sprinkler_counts["wet"],
            "sprinkler wet floor patch count mismatch",
            failures,
        )
        require(
            int(stats.get("sprinkler_alarm_beacons", -1)) == expected_sprinkler_counts["alarms"],
            "sprinkler alarm beacon count mismatch",
            failures,
        )
        require(
            int(stats.get("sprinkler_spray_canopies", -1)) == expected_sprinkler_counts["canopies"],
            "sprinkler spray canopy count mismatch",
            failures,
        )
        require(float(stats.get("sprinkler_wind_drift_m", 0.0)) >= 1.5, "sprinkler wind drift not visible enough", failures)
        require(stats.get("sprinkler_wind_response") is True, "sprinkler wind response missing", failures)
        if expected_sprinkler_mode in ("all", "monitors"):
            require(
                stats.get("xineng_max_emergency_proxy_focus_active") is True,
                "sprinkler monitor mode should activate Xineng MAX emergency proxies",
                failures,
            )
        require(
            stats.get("xineng_sprinkler_hydraulic_source_pass")
            == "max_hydrant_monitor_pressure_runoff_source_proxy",
            "sprinkler hydraulic source pass missing",
            failures,
        )
        require(
            stats.get("xineng_sprinkler_hydraulic_source_runtime_mode")
            == "procedural_sprinkler_hydraulic_proxy_until_dcc_export",
            "sprinkler hydraulic source runtime mode mismatch",
            failures,
        )
        require(
            stats.get("xineng_sprinkler_hydraulic_source_focus_active") is True,
            "sprinkler preset should activate hydraulic source proxies",
            failures,
        )
        require(
            int(stats.get("xineng_sprinkler_hydraulic_source_assets", 0)) >= 10,
            "sprinkler hydraulic source asset coverage too low",
            failures,
        )
        require(
            int(stats.get("xineng_sprinkler_hydraulic_source_asset_bytes", 0)) >= 153000000,
            "sprinkler hydraulic source represented bytes too low",
            failures,
        )
        require(
            int(stats.get("xineng_sprinkler_hydraulic_command_displays", 0)) >= 2,
            "sprinkler hydraulic command displays too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_sprinkler_hydraulic_source_beacons", 0)) >= 10,
            "sprinkler hydraulic source beacons too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_sprinkler_hydraulic_scene_instances", 0)) >= expected_hydraulic_instances,
            "sprinkler hydraulic source scene instances too low for selected sprinkler mode",
            failures,
        )
        require(
            int(stats.get("xineng_sprinkler_hydraulic_hydrant_feeds", -1))
            == (6 if expected_hydraulic_water_source else 0),
            "sprinkler hydraulic hydrant feeds mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_sprinkler_hydraulic_pressure_loop_links", -1))
            == (8 if expected_hydraulic_pressure else 0),
            "sprinkler hydraulic pressure loop links mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_sprinkler_hydraulic_pump_buses", -1))
            == (4 if expected_hydraulic_pumps else 0),
            "sprinkler hydraulic pump bus count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_sprinkler_hydraulic_monitor_intakes", -1))
            == (2 if expected_sprinkler_mode in ("all", "monitors") else 0),
            "sprinkler hydraulic monitor intake count mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_sprinkler_hydraulic_deluge_feed_links", -1))
            == (4 if expected_sprinkler_mode in ("all", "deluge") else 0),
            "sprinkler hydraulic deluge feed links mismatch",
            failures,
        )
        require(
            int(stats.get("xineng_sprinkler_hydraulic_runoff_recovery_links", -1))
            == (8 if expected_sprinkler_mode in ("all", "runoff") else 0),
            "sprinkler hydraulic runoff recovery links mismatch",
            failures,
        )
        require(float(stats.get("rain", 0.0)) >= 0.18, "sprinkler preset should wet the ground", failures)
        require(int(stats.get("puddle_instances", 0)) >= 40, "sprinkler preset should show wet floor feedback", failures)
        require(
            stats.get("cloud_geometry_visible") is False,
            "sprinkler preset should hide cloud geometry over water spray",
            failures,
        )
        require(int(stats.get("point_lights", 0)) >= 8, "sprinkler preset should keep water jets readable", failures)
    elif preset == "routes":
        expected_route_mode = args.route_mode or "combined"
        expected_rescue_active = expected_route_mode in ("combined", "rescue", "reroute")
        expected_evacuation_active = expected_route_mode in ("combined", "evacuation", "reroute")
        expected_graph_active = expected_route_mode in ("combined", "reroute")
        expected_active_routes = int(expected_rescue_active) + int(expected_evacuation_active)
        expected_eta_markers = (6 if expected_rescue_active else 0) + (7 if expected_evacuation_active else 0)
        expected_active_length = (496.0 if expected_rescue_active else 0.0) + (
            575.0 if expected_evacuation_active else 0.0
        )
        require(
            stats.get("emergency_route_planning_focus_active") is True,
            "routes preset did not activate detailed emergency route planning",
            failures,
        )
        require(
            stats.get("route_planning_mode") == expected_route_mode,
            f"route planning mode mismatch: {stats.get('route_planning_mode')} != {expected_route_mode}",
            failures,
        )
        require(
            stats.get("route_planning_interactive_controls") is True,
            "route planning interactive controls not reported",
            failures,
        )
        require(int(stats.get("route_planning_interactive_modes", 0)) >= 4, "route planning mode count too low", failures)
        require(
            stats.get("route_planning_interactive_hotkeys") == "v_y",
            "route planning hotkeys not reported",
            failures,
        )
        require(
            int(stats.get("route_planning_active_route_plans", -1)) == expected_active_routes,
            "route planning active route count mismatch",
            failures,
        )
        require(
            stats.get("route_planning_rescue_active") is expected_rescue_active,
            "route planning rescue active flag mismatch",
            failures,
        )
        require(
            stats.get("route_planning_evacuation_active") is expected_evacuation_active,
            "route planning evacuation active flag mismatch",
            failures,
        )
        require(
            stats.get("route_planning_reroute_graph_active") is expected_graph_active,
            "route planning reroute graph active flag mismatch",
            failures,
        )
        require(
            float(stats.get("route_planning_active_length_m", 0.0)) >= expected_active_length,
            "route planning active route length too short",
            failures,
        )
        expected_dispatch_instances = (
            14
            + 5
            + (6 if expected_evacuation_active else 0)
            + (8 if expected_rescue_active else 0)
            + (5 if expected_rescue_active else 0)
            + (10 if expected_rescue_active else 0)
            + (9 if expected_evacuation_active else 0)
            + (8 if expected_graph_active else 0)
            + (6 if expected_evacuation_active else 0)
            + 14
        )
        require(
            stats.get("xineng_route_source_dispatch_focus_active") is True,
            "routes preset did not activate Xineng source dispatch layer",
            failures,
        )
        require(
            int(stats.get("xineng_route_source_dispatch_access_nodes", 0)) >= 5,
            "route source dispatch access nodes missing",
            failures,
        )
        require(
            int(stats.get("xineng_route_source_dispatch_source_beacons", 0)) >= 14,
            "route source dispatch beacons missing",
            failures,
        )
        require(
            int(stats.get("xineng_route_source_dispatch_scene_instances", 0)) >= expected_dispatch_instances,
            "route source dispatch scene-instance count mismatch",
            failures,
        )
        if expected_rescue_active:
            require(
                int(stats.get("xineng_route_source_dispatch_firewater_nodes", 0)) >= 8,
                "route source dispatch firewater nodes missing",
                failures,
            )
            require(
                int(stats.get("xineng_route_source_dispatch_command_nodes", 0)) >= 5,
                "route source dispatch command nodes missing",
                failures,
            )
            require(
                int(stats.get("xineng_route_source_dispatch_rescue_links", 0)) >= 10,
                "route source dispatch rescue links missing",
                failures,
            )
        else:
            require(
                int(stats.get("xineng_route_source_dispatch_firewater_nodes", -1)) == 0,
                "route source dispatch firewater nodes should be hidden",
                failures,
            )
            require(
                int(stats.get("xineng_route_source_dispatch_command_nodes", -1)) == 0,
                "route source dispatch command nodes should be hidden",
                failures,
            )
            require(
                int(stats.get("xineng_route_source_dispatch_rescue_links", -1)) == 0,
                "route source dispatch rescue links should be hidden",
                failures,
            )
        if expected_evacuation_active:
            require(
                int(stats.get("xineng_route_source_dispatch_camera_nodes", 0)) >= 6,
                "route source dispatch camera nodes missing",
                failures,
            )
            require(
                int(stats.get("xineng_route_source_dispatch_evacuation_links", 0)) >= 9,
                "route source dispatch evacuation links missing",
                failures,
            )
            require(
                int(stats.get("xineng_route_source_dispatch_capacity_feeds", 0)) >= 6,
                "route source dispatch capacity feeds missing",
                failures,
            )
        else:
            require(
                int(stats.get("xineng_route_source_dispatch_camera_nodes", -1)) == 0,
                "route source dispatch camera nodes should be hidden",
                failures,
            )
            require(
                int(stats.get("xineng_route_source_dispatch_evacuation_links", -1)) == 0,
                "route source dispatch evacuation links should be hidden",
                failures,
            )
            require(
                int(stats.get("xineng_route_source_dispatch_capacity_feeds", -1)) == 0,
                "route source dispatch capacity feeds should be hidden",
                failures,
            )
        if expected_graph_active:
            require(
                int(stats.get("xineng_route_source_dispatch_traffic_segments", 0)) >= 8,
                "route source dispatch traffic segments missing",
                failures,
            )
        else:
            require(
                int(stats.get("xineng_route_source_dispatch_traffic_segments", -1)) == 0,
                "route source dispatch traffic segments should be hidden",
                failures,
            )
        if expected_graph_active:
            require(int(stats.get("emergency_route_graph_nodes", 0)) >= 14, "route graph nodes too sparse", failures)
            require(int(stats.get("emergency_route_graph_edges", 0)) >= 18, "route graph edges too sparse", failures)
            require(
                float(stats.get("emergency_route_graph_length_m", 0.0)) >= 1000.0,
                "route graph length too short",
                failures,
            )
            require(
                int(stats.get("emergency_route_blocked_hazard_zones", 0)) >= 3,
                "blocked hazard zones missing from route plan",
                failures,
            )
            require(
                int(stats.get("emergency_route_alternate_segments", 0)) >= 6,
                "alternate route segments too sparse",
                failures,
            )
            require(
                int(stats.get("emergency_route_separation_buffers", 0)) >= 2,
                "route separation buffers missing",
                failures,
            )
            require(
                stats.get("emergency_route_solver_active") is True,
                "route solver should be active with graph/reroute modes",
                failures,
            )
            require(
                int(stats.get("emergency_route_solver_blocked_edges", 0)) >= 3,
                "route solver blocked-edge count too low",
                failures,
            )
            require(
                int(stats.get("emergency_route_solver_open_edges", 0)) >= 15,
                "route solver open-edge count too low",
                failures,
            )
            require(
                int(stats.get("emergency_route_solver_chosen_path_nodes", 0)) >= 6,
                "route solver chosen path nodes too sparse",
                failures,
            )
            require(
                float(stats.get("emergency_route_solver_chosen_path_length_m", 0.0)) >= 500.0,
                "route solver chosen path too short",
                failures,
            )
            require(
                float(stats.get("emergency_route_solver_eta_minutes", 0.0)) >= 9.0,
                "route solver ETA not reported",
                failures,
            )
            require(
                float(stats.get("emergency_route_solver_capacity_persons_per_min", 0.0)) >= 180.0,
                "route solver capacity too low",
                failures,
            )
            require(
                float(stats.get("emergency_route_solver_safety_margin", 0.0)) >= 0.70,
                "route solver safety margin too low",
                failures,
            )
            require(
                int(stats.get("emergency_route_solver_constraint_layers", 0)) >= 5,
                "route solver constraint layers too sparse",
                failures,
            )
            require(
                int(stats.get("emergency_route_solver_cost_terms", 0)) >= 6,
                "route solver cost terms too sparse",
                failures,
            )
            require(
                int(stats.get("emergency_route_solver_capacity_samples", 0)) >= 8,
                "route solver capacity samples too sparse",
                failures,
            )
            require(
                stats.get("emergency_route_wind_gas_active") is True,
                "route wind/gas reroute audit should be active with graph/reroute modes",
                failures,
            )
            require(
                int(stats.get("emergency_route_wind_gas_exposure_samples", 0)) >= 16,
                "route wind/gas exposure samples too sparse",
                failures,
            )
            require(
                int(stats.get("emergency_route_wind_gas_hazard_cost_cells", 0)) >= 10,
                "route wind/gas hazard cost cells too sparse",
                failures,
            )
            require(
                int(stats.get("emergency_route_wind_gas_shelter_gates", 0)) >= 4,
                "route wind/gas shelter gates missing",
                failures,
            )
            require(
                int(stats.get("emergency_route_wind_gas_blocked_edges", 0)) >= 3,
                "route wind/gas blocked edges missing",
                failures,
            )
            require(
                int(stats.get("emergency_route_wind_gas_safe_corridors", 0)) >= 3,
                "route wind/gas safe corridors missing",
                failures,
            )
            require(
                int(stats.get("emergency_route_wind_gas_sensor_fusion_inputs", 0)) >= 4,
                "route wind/gas sensor fusion inputs missing",
                failures,
            )
            require(
                int(stats.get("emergency_route_wind_gas_escape_vectors", 0)) >= 6,
                "route wind/gas escape vectors missing",
                failures,
            )
            require(
                float(stats.get("emergency_route_wind_gas_static_route_max_exposure", 0.0)) > 0.30,
                "route wind/gas static route exposure should show a hazard crossing",
                failures,
            )
            require(
                float(stats.get("emergency_route_wind_gas_chosen_route_max_exposure", 1.0))
                <= float(stats.get("emergency_route_wind_gas_safe_exposure_limit", 0.0)),
                "route wind/gas chosen route should stay under safe exposure limit",
                failures,
            )
            require(
                float(stats.get("emergency_route_wind_gas_exposure_reduction_percent", 0.0)) >= 90.0,
                "route wind/gas reroute exposure reduction too low",
                failures,
            )
            require(
                stats.get("emergency_route_wind_gas_tracks_route_mode") == "combined_and_reroute",
                "route wind/gas mode tracking missing",
                failures,
            )
            require(
                int(stats.get("emergency_route_wind_gas_scene_instances", 0)) >= 46,
                "route wind/gas scene instances too sparse",
                failures,
            )
        else:
            require(int(stats.get("emergency_route_graph_nodes", -1)) == 0, "route graph should be hidden", failures)
            require(int(stats.get("emergency_route_graph_edges", -1)) == 0, "route graph edges should be hidden", failures)
            require(
                float(stats.get("emergency_route_graph_length_m", -1.0)) == 0.0,
                "route graph length should be zero",
                failures,
            )
            require(
                int(stats.get("emergency_route_blocked_hazard_zones", -1)) == 0,
                "blocked hazard zones should be hidden",
                failures,
            )
            require(
                int(stats.get("emergency_route_alternate_segments", -1)) == 0,
                "alternate route segments should be hidden",
                failures,
            )
            require(
                int(stats.get("emergency_route_separation_buffers", -1)) == 0,
                "route separation buffers should be hidden",
                failures,
            )
            require(
                stats.get("emergency_route_solver_active") is False,
                "route solver should be hidden without graph/reroute mode",
                failures,
            )
            require(
                int(stats.get("emergency_route_solver_blocked_edges", -1)) == 0,
                "route solver blocked edges should be hidden",
                failures,
            )
            require(
                int(stats.get("emergency_route_solver_open_edges", -1)) == 0,
                "route solver open edges should be hidden",
                failures,
            )
            require(
                int(stats.get("emergency_route_solver_chosen_path_nodes", -1)) == 0,
                "route solver chosen path should be hidden",
                failures,
            )
            require(
                stats.get("emergency_route_wind_gas_active") is False,
                "route wind/gas reroute audit should be hidden without graph/reroute mode",
                failures,
            )
            require(
                int(stats.get("emergency_route_wind_gas_exposure_samples", -1)) == 0,
                "route wind/gas exposure samples should be hidden",
                failures,
            )
            require(
                int(stats.get("emergency_route_wind_gas_hazard_cost_cells", -1)) == 0,
                "route wind/gas hazard cost cells should be hidden",
                failures,
            )
            require(
                int(stats.get("emergency_route_wind_gas_scene_instances", -1)) == 0,
                "route wind/gas scene instances should be hidden",
                failures,
            )
        if expected_evacuation_active:
            require(
                int(stats.get("emergency_route_muster_zones", 0)) >= 3,
                "muster zones too sparse",
                failures,
            )
            require(
                int(stats.get("emergency_route_capacity_bars", 0)) >= 12,
                "evacuation capacity bars too sparse",
                failures,
            )
        else:
            require(
                int(stats.get("emergency_route_muster_zones", -1)) == 0,
                "muster zones should be hidden",
                failures,
            )
            require(
                int(stats.get("emergency_route_capacity_bars", -1)) == 0,
                "evacuation capacity bars should be hidden",
                failures,
            )
        if expected_rescue_active:
            require(
                int(stats.get("emergency_route_triage_zones", 0)) >= 2,
                "triage zones missing",
                failures,
            )
            require(
                int(stats.get("emergency_route_vehicle_staging_markers", 0)) >= 4,
                "vehicle staging markers too sparse",
                failures,
            )
            require(
                int(stats.get("emergency_route_decision_gates", 0)) >= 5,
                "route decision gates too sparse",
                failures,
            )
        else:
            require(
                int(stats.get("emergency_route_triage_zones", -1)) == 0,
                "triage zones should be hidden",
                failures,
            )
            require(
                int(stats.get("emergency_route_vehicle_staging_markers", -1)) == 0,
                "vehicle staging markers should be hidden",
                failures,
            )
            require(
                int(stats.get("emergency_route_decision_gates", -1)) == 0,
                "route decision gates should be hidden",
                failures,
            )
        require(
            int(stats.get("emergency_route_eta_markers", 0)) >= expected_eta_markers,
            "route ETA marker count mismatch",
            failures,
        )
        require(
            stats.get("cloud_geometry_visible") is False,
            "routes preset should hide cloud geometry over route planning overlays",
            failures,
        )
        require(int(stats.get("point_lights", 0)) >= 8, "routes preset should keep route markers readable", failures)
        require(float(stats.get("fog_density", 1.0)) <= 0.12, "routes preset should keep route details readable", failures)
    elif preset == "warehouse":
        require(
            stats.get("xineng_asset_inspection_active") is True,
            "warehouse preset did not activate inspection overlay",
            failures,
        )
        require(
            int(stats.get("xineng_asset_inspection_highlighted_instances", 0)) >= 3,
            "warehouse preset should highlight all active LOD rack instances",
            failures,
        )
        require(
            int(stats.get("xineng_asset_inspection_scan_bars", 0)) >= 12,
            "warehouse preset scan overlay too sparse",
            failures,
        )
        require(
            int(stats.get("point_lights", 0)) >= 8,
            "warehouse preset should keep nearby inspection lights enabled",
            failures,
        )
        require(
            float(stats.get("fog_density", 1.0)) <= 0.12,
            "warehouse preset should keep source-model details readable",
            failures,
        )
        require(
            stats.get("xineng_asset_impact_active") is True,
            "warehouse preset did not activate impact analysis",
            failures,
        )
        require(
            int(stats.get("xineng_asset_impact_affected_process_nodes", 0)) >= 5,
            "warehouse impact analysis should expose affected process nodes",
            failures,
        )
        require(
            int(stats.get("xineng_asset_impact_process_links", 0)) >= 4,
            "warehouse impact analysis process links too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_asset_impact_response_routes", 0)) >= 2,
            "warehouse impact analysis should include rescue and evacuation routes",
            failures,
        )
        require(
            int(stats.get("xineng_asset_impact_firewater_coverage_rings", 0)) >= 3,
            "warehouse impact analysis firewater coverage too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_asset_impact_evacuation_links", 0)) >= 3,
            "warehouse impact evacuation links too sparse",
            failures,
        )
        require(
            float(stats.get("xineng_asset_impact_total_route_length_m", 0.0)) >= 850.0,
            "warehouse impact analysis route length too short",
            failures,
        )
        require(
            stats.get("xineng_warehouse_max_proxy_pass")
            == "max_only_warehouse_shell_logistics_procedural_runtime_proxies",
            "warehouse MAX shell/logistics proxy pass not reported",
            failures,
        )
        require(
            stats.get("xineng_warehouse_max_runtime_mode") == "procedural_proxy_until_dcc_export",
            "warehouse MAX proxy runtime mode not reported",
            failures,
        )
        require(
            stats.get("xineng_warehouse_max_proxy_focus_active") is True,
            "warehouse preset did not activate warehouse MAX proxy layer",
            failures,
        )
        require(
            int(stats.get("xineng_warehouse_max_proxy_assets", 0)) >= 12,
            "warehouse MAX proxy source asset count too low",
            failures,
        )
        require(
            int(stats.get("xineng_warehouse_max_source_asset_bytes", 0)) >= 139000000,
            "warehouse MAX proxy source byte count too low",
            failures,
        )
        require(
            int(stats.get("xineng_warehouse_max_shell_source_files", 0)) >= 10,
            "warehouse MAX shell source files too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_warehouse_max_shell_bays", 0)) >= 11,
            "warehouse MAX shell bay proxies too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_warehouse_max_forklift_bays", 0)) >= 1,
            "warehouse MAX forklift bay proxy missing",
            failures,
        )
        require(
            int(stats.get("xineng_warehouse_max_loading_docks", 0)) >= 14,
            "warehouse MAX loading dock proxies too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_warehouse_max_rollup_doors", 0)) >= 22,
            "warehouse MAX rollup doors too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_warehouse_max_rack_source_references", 0)) >= 1,
            "warehouse rack MAX source reference missing",
            failures,
        )
        require(
            int(stats.get("xineng_warehouse_max_yard_crates", 0)) >= 24,
            "warehouse MAX yard crates too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_warehouse_max_traffic_lanes", 0)) >= 10,
            "warehouse MAX traffic lane proxies too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_warehouse_max_canopy_sections", 0)) >= 6,
            "warehouse MAX canopy sections too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_warehouse_max_logistics_route_links", 0)) >= 6,
            "warehouse MAX logistics route links too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_warehouse_max_proxy_beacons", 0)) >= 12,
            "warehouse MAX proxy beacons too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_warehouse_max_scene_instances", 0)) >= 100,
            "warehouse MAX proxy scene instances too sparse",
            failures,
        )
        require(
            float(stats.get("xineng_warehouse_max_logistics_route_length_m", 0.0)) >= 200.0,
            "warehouse MAX logistics route length too short",
            failures,
        )
    elif preset == "drill":
        require(
            stats.get("emergency_drill_active") is True,
            "drill preset did not activate emergency drill overlay",
            failures,
        )
        require(
            int(stats.get("emergency_drill_timeline_stages", 0)) >= 6,
            "drill timeline stages too sparse",
            failures,
        )
        require(
            int(stats.get("emergency_drill_command_posts", 0)) >= 2,
            "drill command posts missing",
            failures,
        )
        require(
            int(stats.get("emergency_drill_response_teams", 0)) >= 4,
            "drill response teams too sparse",
            failures,
        )
        require(
            int(stats.get("emergency_drill_subsystem_links", 0)) >= 6,
            "drill subsystem links too sparse",
            failures,
        )
        require(
            int(stats.get("emergency_drill_isolation_actions", 0)) >= 4,
            "drill isolation actions too sparse",
            failures,
        )
        require(
            int(stats.get("emergency_drill_weather_sensors", 0)) >= 5,
            "drill weather sensors too sparse",
            failures,
        )
        require(
            int(stats.get("emergency_drill_status_beacons", 0)) >= 12,
            "drill status beacons too sparse",
            failures,
        )
        require(
            stats.get("emergency_drill_decision_audit_active") is True,
            "drill decision audit overlay not active",
            failures,
        )
        require(
            int(stats.get("emergency_drill_decision_gates", 0)) >= 6,
            "drill decision gates too sparse",
            failures,
        )
        require(
            int(stats.get("emergency_drill_decision_trigger_thresholds", 0)) >= 6,
            "drill decision trigger thresholds missing",
            failures,
        )
        require(
            int(stats.get("emergency_drill_decision_command_ack_cards", 0)) >= 6,
            "drill command acknowledgement cards missing",
            failures,
        )
        require(
            int(stats.get("emergency_drill_decision_closed_loop_checks", 0)) >= 6,
            "drill closed-loop checks missing",
            failures,
        )
        require(
            int(stats.get("emergency_drill_decision_stage_handoffs", 0)) >= 5,
            "drill decision stage handoffs missing",
            failures,
        )
        require(
            int(stats.get("emergency_drill_decision_sensor_vote_quorum", 0)) >= 3,
            "drill sensor vote quorum missing",
            failures,
        )
        require(
            0.0 < float(stats.get("emergency_drill_decision_latency_budget_s", 0.0)) <= 120.0,
            "drill decision latency budget outside expected range",
            failures,
        )
        require(
            int(stats.get("emergency_drill_decision_audit_scene_instances", 0)) >= 29,
            "drill decision audit scene instances too sparse",
            failures,
        )
        require(stats.get("emergency_drill_wind_coupled") is True, "drill wind coupling missing", failures)
        require(
            stats.get("emergency_drill_integrates_process_routes_sprinkler_particles") is True,
            "drill integrated subsystem flag missing",
            failures,
        )
        require(
            stats.get("emergency_drill_interactive_playback_controls") is True,
            "drill interactive playback controls not reported",
            failures,
        )
        require(
            stats.get("emergency_drill_interactive_time_slider") is True,
            "drill interactive time slider not reported",
            failures,
        )
        require(
            int(stats.get("emergency_drill_interactive_control_buttons", 0)) >= 3,
            "drill interactive playback buttons missing",
            failures,
        )
        require(
            stats.get("emergency_drill_interactive_hotkeys") == "space_j_k",
            "drill interactive hotkeys not reported",
            failures,
        )
        require(
            stats.get("emergency_drill_scenario_runtime_loaded") is True,
            "drill preset did not load runtime scenario JSON",
            failures,
        )
        require(
            stats.get("emergency_drill_local_scenario_loaded") is True,
            "drill preset did not load local scenario JSON",
            failures,
        )
        require(
            stats.get("process_management_focus_active") is True,
            "drill preset did not activate process management",
            failures,
        )
        require(
            stats.get("emergency_route_planning_focus_active") is True,
            "drill preset did not activate route planning",
            failures,
        )
        require(stats.get("sprinkler_focus_active") is True, "drill preset did not activate sprinkler detail", failures)
        require(
            stats.get("xineng_max_emergency_proxy_focus_active") is True,
            "drill preset did not activate MAX emergency proxies",
            failures,
        )
        if args.scenario_time is None:
            require(
                stats.get("emergency_drill_playback_mode") == "integrated_overview",
                "drill overview mode should remain integrated",
                failures,
            )
            require(
                stats.get("gas_diffusion_analysis_active") is True,
                "drill preset did not activate gas diffusion analysis",
                failures,
            )
            require(
                stats.get("explosion_effect_active") is True,
                "drill preset did not activate explosion effect",
                failures,
            )
            require(
                int(stats.get("gas_diffusion_concentration_bands", 0)) >= 4,
                "drill gas diffusion bands too sparse",
                failures,
            )
            require(
                int(stats.get("particle_instances", 0)) >= 1100,
                "drill should include gas, fire, explosion, and smoke particles",
                failures,
            )
        else:
            duration = max(float(stats.get("emergency_drill_duration_seconds", 45.0)), 1.0)
            sample_time = args.scenario_time % duration
            expected_plume = sample_time < 25.0
            expected_fire = 10.0 <= sample_time < 35.0
            expected_smoke = sample_time >= 15.0
            expected_shockwave = sample_time >= 15.0
            expected_hazard = 5.0 <= sample_time < 25.0
            expected_rescue = sample_time >= 20.0
            expected_fire_annotation = sample_time >= 10.0
            if sample_time < 5.0:
                expected_stage = "leak_detected"
            elif sample_time < 10.0:
                expected_stage = "exclusion_zone"
            elif sample_time < 15.0:
                expected_stage = "pool_fire"
            elif sample_time < 25.0:
                expected_stage = "explosion_response"
            elif sample_time < 35.0:
                expected_stage = "deluge_shutdown"
            else:
                expected_stage = "all_clear_monitoring"

            require(
                stats.get("emergency_drill_playback_mode") == "scenario_time_sample",
                "drill scenario-time mode not reported",
                failures,
            )
            require(
                abs(float(stats.get("emergency_drill_scenario_time_seconds", -999.0)) - sample_time) <= 0.02,
                "drill scenario time does not match requested sample",
                failures,
            )
            require(
                stats.get("emergency_drill_stage_name") == expected_stage,
                f"drill stage mismatch: {stats.get('emergency_drill_stage_name')} != {expected_stage}",
                failures,
            )
            require(stats.get("emergency_drill_plume_enabled") is expected_plume, "drill plume track mismatch", failures)
            require(stats.get("emergency_drill_fire_enabled") is expected_fire, "drill fire track mismatch", failures)
            require(stats.get("emergency_drill_smoke_enabled") is expected_smoke, "drill smoke track mismatch", failures)
            require(
                stats.get("emergency_drill_shockwave_triggered") is expected_shockwave,
                "drill shockwave trigger mismatch",
                failures,
            )
            require(
                stats.get("emergency_drill_hazard_polygon_visible") is expected_hazard,
                "drill hazard annotation mismatch",
                failures,
            )
            require(
                stats.get("emergency_drill_rescue_annotation_visible") is expected_rescue,
                "drill rescue annotation mismatch",
                failures,
            )
            require(
                stats.get("emergency_drill_fire_annotation_visible") is expected_fire_annotation,
                "drill fire annotation mismatch",
                failures,
            )
            require(
                stats.get("gas_diffusion_analysis_active") is expected_plume,
                "drill gas diffusion active flag mismatch",
                failures,
            )
            require(
                stats.get("explosion_effect_active") is expected_shockwave,
                "drill explosion active flag mismatch",
                failures,
            )
            require(int(stats.get("particle_instances", 0)) >= 0, "drill particle stat missing", failures)
        expected_decision_completion = float(stats.get("emergency_drill_response_progress", 0.0)) * 100.0
        require(
            abs(
                float(stats.get("emergency_drill_decision_response_completion_percent", -999.0))
                - expected_decision_completion
            )
            <= 0.2,
            "drill decision response completion did not track playback state",
            failures,
        )
        require(
            int(stats.get("emergency_drill_decision_stage_index", -1))
            == int(stats.get("emergency_drill_stage_index", -2)),
            "drill decision stage index did not track playback state",
            failures,
        )
        require(
            stats.get("emergency_drill_decision_stage_name") == stats.get("emergency_drill_stage_name"),
            "drill decision stage name did not track playback state",
            failures,
        )
        require(
            float(stats.get("gas_diffusion_downwind_distance_m", 0.0)) >= 420.0,
            "drill downwind gas diffusion footprint too short",
            failures,
        )
        require(
            float(stats.get("sprinkler_wind_drift_m", 0.0)) >= 1.8,
            "drill sprinkler wind drift too weak",
            failures,
        )
        require(
            float(stats.get("emergency_route_graph_length_m", 0.0)) >= 1000.0,
            "drill route graph length too short",
            failures,
        )
        require(
            float(stats.get("process_flow_route_length_m", 0.0)) >= 600.0,
            "drill process route too short",
            failures,
        )
        require(float(stats.get("particle_intensity", 0.0)) >= 1.20, "drill particle intensity too low", failures)
        require(float(stats.get("wind_speed", 0.0)) >= 6.5, "drill wind speed too low", failures)
        require(float(stats.get("rain", 0.0)) >= 0.15, "drill should show wet emergency response", failures)
        require(int(stats.get("point_lights", 0)) >= 8, "drill should keep response overlays readable", failures)
        require(float(stats.get("fog_density", 1.0)) <= 0.14, "drill fog should keep overlays readable", failures)
        require(
            stats.get("cloud_geometry_visible") is False,
            "drill preset should hide cloud geometry over emergency timeline",
            failures,
        )
    elif preset == "performance":
        require(
            stats.get("render_efficiency_active") is True,
            "performance preset did not activate render efficiency telemetry",
            failures,
        )
        require(
            int(stats.get("render_efficiency_telemetry_bars", 0)) >= 6,
            "performance telemetry bars too sparse",
            failures,
        )
        require(
            int(stats.get("render_efficiency_lod_rows", 0)) >= 4,
            "performance LOD rows too sparse",
            failures,
        )
        require(
            int(stats.get("render_efficiency_stress_profiles", 0)) >= 2,
            "performance stress profiles missing",
            failures,
        )
        require(
            int(stats.get("render_efficiency_culling_zones", 0)) >= 5,
            "performance culling zones too sparse",
            failures,
        )
        require(
            int(stats.get("render_efficiency_budget_markers", 0)) >= 4,
            "performance frame-budget markers missing",
            failures,
        )
        require(
            int(stats.get("render_efficiency_batch_nodes", 0)) >= 7,
            "performance batch graph nodes too sparse",
            failures,
        )
        require(
            int(stats.get("render_efficiency_runtime_gltf_instances", 0)) >= 100,
            "performance preset should keep the runtime glTF set loaded",
            failures,
        )
        require(
            stats.get("render_efficiency_visual_clearance")
            == "suppressed_cloud_fire_gas_geometry_for_budget_telemetry",
            "performance visual-clearance pass missing",
            failures,
        )
        require(
            stats.get("render_postfx_stack_active") is True,
            "performance preset did not activate render post-FX stack overlay",
            failures,
        )
        require(
            int(stats.get("render_postfx_budget_bars", 0)) >= 9,
            "performance post-FX budget bars too sparse",
            failures,
        )
        require(
            int(stats.get("render_postfx_cost_markers", 0)) >= 12,
            "performance post-FX cost markers too sparse",
            failures,
        )
        require(
            int(stats.get("render_postfx_scene_instances", 0)) >= 82,
            "performance post-FX scene instances too sparse",
            failures,
        )
        require(
            stats.get("render_resource_budget_active") is True,
            "performance preset did not activate render resource budget overlay",
            failures,
        )
        require(
            int(stats.get("render_resource_budget_memory_pools", 0)) >= 5,
            "performance resource memory pools too sparse",
            failures,
        )
        require(
            int(stats.get("render_resource_budget_residency_bars", 0)) >= 12,
            "performance resource residency bars too sparse",
            failures,
        )
        require(
            int(stats.get("render_resource_budget_upload_lanes", 0)) >= 4,
            "performance resource upload lanes missing",
            failures,
        )
        require(
            int(stats.get("render_resource_budget_heap_markers", 0)) >= 5,
            "performance resource heap markers missing",
            failures,
        )
        require(
            int(stats.get("render_resource_budget_eviction_markers", 0)) >= 8,
            "performance resource eviction markers missing",
            failures,
        )
        require(
            int(stats.get("render_resource_budget_alias_windows", 0)) >= 6,
            "performance resource alias windows missing",
            failures,
        )
        require(
            int(stats.get("render_resource_budget_scene_instances", 0)) >= 40,
            "performance resource budget scene instances too sparse",
            failures,
        )
        require(
            stats.get("xineng_whole_plant_max_streaming_focus_active") is True,
            "performance preset did not activate whole-plant MAX streaming proxy",
            failures,
        )
        require(
            int(stats.get("xineng_whole_plant_max_streaming_partitions", 0)) >= 24,
            "whole-plant MAX streaming partitions too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_whole_plant_max_streaming_visible_cells", 0)) >= 16,
            "whole-plant MAX streaming visible cell count too low",
            failures,
        )
        require(
            int(stats.get("xineng_whole_plant_max_streaming_resident_cells", 0)) >= 12,
            "whole-plant MAX streaming resident cell count too low",
            failures,
        )
        require(
            int(stats.get("xineng_whole_plant_max_streaming_lod_tiers", 0)) >= 4,
            "whole-plant MAX streaming LOD tiers missing",
            failures,
        )
        require(
            int(stats.get("xineng_whole_plant_max_streaming_culling_tiles", 0)) >= 12,
            "whole-plant MAX streaming culling tiles too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_whole_plant_max_streaming_occlusion_portals", 0)) >= 8,
            "whole-plant MAX streaming occlusion portals too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_whole_plant_max_streaming_budget_bars", 0)) >= 6,
            "whole-plant MAX streaming budget bars missing",
            failures,
        )
        require(
            int(stats.get("xineng_whole_plant_max_streaming_proxy_hulls", 0)) >= 18,
            "whole-plant MAX streaming proxy hulls too sparse",
            failures,
        )
        require(
            int(stats.get("xineng_whole_plant_max_streaming_material_buckets", 0)) >= 5,
            "whole-plant MAX streaming material buckets missing",
            failures,
        )
        require(
            int(stats.get("xineng_whole_plant_max_streaming_source_anchors", 0)) >= 6,
            "whole-plant MAX streaming source anchors missing",
            failures,
        )
        require(
            float(stats.get("xineng_whole_plant_max_streaming_cell_size_m", 0.0)) >= 96.0,
            "whole-plant MAX streaming cell size not reported",
            failures,
        )
        require(
            int(stats.get("xineng_whole_plant_max_streaming_scene_instances", 0)) >= 117,
            "whole-plant MAX streaming scene instances too sparse",
            failures,
        )
        require(
            stats.get("cloud_geometry_visible") is False,
            "performance preset should hide cloud geometry over telemetry",
            failures,
        )
        require(int(stats.get("point_lights", 0)) >= 6, "performance preset should keep telemetry lit", failures)
        require(float(stats.get("fog_density", 1.0)) <= 0.10, "performance preset should keep telemetry readable", failures)
        require(
            float(stats.get("particle_intensity", 1.0)) <= 0.30,
            "performance preset should suppress particle intensity around telemetry",
            failures,
        )

    if args.expected_point_lights is not None:
        require(
            int(stats.get("point_lights", -1)) == args.expected_point_lights,
            f"point light override mismatch: {stats.get('point_lights')} != {args.expected_point_lights}",
            failures,
        )
    for key, expected in (
        ("rain", args.expected_rain),
        ("snow", args.expected_snow),
        ("fog_density", args.expected_fog_density),
        ("cloud_amount", args.expected_clouds),
        ("wind_speed", args.expected_wind_speed),
        ("particle_intensity", args.expected_particle_intensity),
    ):
        if expected is None:
            continue
        actual = float(stats.get(key, -9999.0))
        require(abs(actual - expected) <= 0.02, f"{key} mismatch: {actual:.3f} != {expected:.3f}", failures)
    if args.expected_wind_dir is not None:
        actual = float(stats.get("wind_dir_degrees", -9999.0))
        delta = abs(((actual - args.expected_wind_dir + 180.0) % 360.0) - 180.0)
        require(delta <= 0.05, f"wind dir mismatch: {actual:.3f} != {args.expected_wind_dir:.3f}", failures)
    if args.expected_sun_azimuth is not None:
        actual = float(stats.get("sun_azimuth_degrees", -9999.0))
        delta = abs(((actual - args.expected_sun_azimuth + 180.0) % 360.0) - 180.0)
        require(delta <= 0.05, f"sun azimuth mismatch: {actual:.3f} != {args.expected_sun_azimuth:.3f}", failures)
    if args.expected_sun_elevation is not None:
        actual = float(stats.get("sun_elevation_degrees", -9999.0))
        require(abs(actual - args.expected_sun_elevation) <= 0.05, f"sun elevation mismatch: {actual:.3f}", failures)

    if args.stress == "models":
        require(int(stats.get("stress_model_instances", 0)) >= 1000, "model stress did not add 1000 instances", failures)
    elif args.stress == "particles":
        require(int(stats.get("particle_instances", 0)) >= 50000, "particle stress did not add 50000 particles", failures)

    if failures:
        detail = "\n  - ".join(failures)
        raise RuntimeError(f"showcase stats validation failed:\n  - {detail}\nstats={stats}")
    return stats


def run_demo(args: argparse.Namespace, image: Path, stats: Path) -> None:
    demo_args = [
        "--scene=industrial_park",
        f"--preset={args.preset}",
        f"--frames={args.frames}",
        f"--screenshot={image}",
        f"--stats={stats}",
        f"--width={args.width}",
        f"--height={args.height}",
    ]
    if args.stress:
        demo_args.append(f"--stress={args.stress}")
    if args.expected_point_lights is not None:
        demo_args.append(f"--point-lights={args.expected_point_lights}")
    if args.expected_rain is not None:
        demo_args.append(f"--rain={args.expected_rain}")
    if args.expected_snow is not None:
        demo_args.append(f"--snow={args.expected_snow}")
    if args.expected_fog_density is not None:
        demo_args.append(f"--fog-density={args.expected_fog_density}")
    if args.expected_clouds is not None:
        demo_args.append(f"--clouds={args.expected_clouds}")
    if args.expected_wind_dir is not None:
        demo_args.append(f"--wind-dir={args.expected_wind_dir}")
    if args.expected_wind_speed is not None:
        demo_args.append(f"--wind-speed={args.expected_wind_speed}")
    if args.expected_particle_intensity is not None:
        demo_args.append(f"--particle-intensity={args.expected_particle_intensity}")
    if args.expected_sun_azimuth is not None:
        demo_args.append(f"--sun-azimuth={args.expected_sun_azimuth}")
    if args.expected_sun_elevation is not None:
        demo_args.append(f"--sun-elevation={args.expected_sun_elevation}")
    if args.scenario_time is not None:
        demo_args.append(f"--scenario-time={args.scenario_time}")
    if args.gis_measure:
        demo_args.append(f"--gis-measure={args.gis_measure}")
    if args.route_mode:
        demo_args.append(f"--route-mode={args.route_mode}")
    if args.process_mode:
        demo_args.append(f"--process-mode={args.process_mode}")
    if args.sprinkler_mode:
        demo_args.append(f"--sprinkler-mode={args.sprinkler_mode}")
    if args.fountain_mode:
        demo_args.append(f"--fountain-mode={args.fountain_mode}")
    if args.gas_mode:
        demo_args.append(f"--gas-mode={args.gas_mode}")
    if args.disassembly_step:
        demo_args.append(f"--disassembly-step={args.disassembly_step}")
    if args.material_mode:
        demo_args.append(f"--material-mode={args.material_mode}")
    if args.max_catalog_mode:
        demo_args.append(f"--max-catalog-mode={args.max_catalog_mode}")
    if args.huilian_view:
        demo_args.append(f"--huilian-view={args.huilian_view}")
    if args.huilian_approach_progress is not None:
        demo_args.append(f"--huilian-approach-progress={args.huilian_approach_progress}")
    if args.dlg_layer:
        demo_args.append(f"--dlg-layer={args.dlg_layer}")

    for path in (image, stats):
        try:
            path.unlink()
        except FileNotFoundError:
            pass

    cwd = Path(args.cwd).resolve()
    demo = Path(args.demo)
    if not demo.is_absolute():
        demo = cwd / demo
    run_showcase_process(
        demo,
        demo_args,
        cwd,
        attempts=args.demo_attempts,
        timeout=args.demo_timeout,
        fallback_root=cwd,
        copy_fallback=True,
        label=f"showcase_demo {args.preset}",
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--demo", required=True)
    parser.add_argument("--preset", required=True, choices=PRESETS)
    parser.add_argument("--output", required=True)
    parser.add_argument("--stats-output", required=True)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--frames", type=int, default=30)
    parser.add_argument("--cwd", default=os.getcwd())
    parser.add_argument("--min-bytes", type=int, default=90000)
    parser.add_argument("--demo-timeout", type=float, default=35.0)
    parser.add_argument("--demo-attempts", type=int, default=2)
    parser.add_argument("--stress", choices=("models", "particles"))
    parser.add_argument("--scenario-time", type=float)
    parser.add_argument("--gis-measure", choices=("all", "distance", "area", "elevation", "volume"))
    parser.add_argument("--route-mode", choices=("combined", "rescue", "evacuation", "reroute"))
    parser.add_argument("--process-mode", choices=("all", "upstream", "downstream", "isolation", "shutdown"))
    parser.add_argument("--sprinkler-mode", choices=("all", "heads", "monitors", "deluge", "runoff"))
    parser.add_argument("--fountain-mode", choices=("all", "jets", "mist", "ripples", "wind"))
    parser.add_argument("--gas-mode", choices=("all", "plume", "footprint", "evacuation", "sensors"))
    parser.add_argument("--disassembly-step", choices=("all", "base", "housing", "rotor", "seal", "motor", "fasteners"))
    parser.add_argument("--material-mode", choices=("all", "layers", "uv", "surface", "degradation", "emissive"))
    parser.add_argument("--max-catalog-mode", choices=("all", "site", "warehouse", "safety", "power", "security"))
    parser.add_argument("--huilian-view", choices=("valve", "southwest", "overview", "dlg", "approach"))
    parser.add_argument("--huilian-approach-progress", type=float, choices=(0.0, 0.3, 0.4, 0.5, 0.65, 1.0))
    parser.add_argument("--dlg-layer", choices=("all", "transport", "hydro", "structures", "utilities"))
    parser.add_argument("--expected-point-lights", type=int)
    parser.add_argument("--expected-rain", type=float)
    parser.add_argument("--expected-snow", type=float)
    parser.add_argument("--expected-fog-density", type=float)
    parser.add_argument("--expected-clouds", type=float)
    parser.add_argument("--expected-wind-dir", type=float)
    parser.add_argument("--expected-wind-speed", type=float)
    parser.add_argument("--expected-particle-intensity", type=float)
    parser.add_argument("--expected-sun-azimuth", type=float)
    parser.add_argument("--expected-sun-elevation", type=float)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    suppress_windows_error_dialogs()
    # The demo is deliberately copied into and launched from an isolated
    # directory to catch accidental source-tree/runtime coupling. Keep output
    # destinations absolute so that isolation does not redirect screenshots
    # and stats into that temporary working directory.
    image = Path(args.output).resolve()
    stats_path = Path(args.stats_output).resolve()
    image.parent.mkdir(parents=True, exist_ok=True)
    stats_path.parent.mkdir(parents=True, exist_ok=True)
    try:
        run_demo(args, image, stats_path)
        metrics = image_verify.validate(image, args.preset, args.width, args.height, args.min_bytes)
        stats = validate_stats(stats_path, args.preset, args)
    except Exception as exc:
        print(f"[showcase_verify] {exc}", file=sys.stderr)
        return 1

    print(
        f"[showcase_verify] {args.preset}: ok "
        f"mean={metrics['mean_luma']:.1f} std={metrics['std_luma']:.1f} "
        f"vertices={stats['vertices']} indices={stats['indices']} "
        f"lights={stats['point_lights']} particles={stats['particle_instances']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
