#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <string>

namespace {

  std::string scenePath() {
    return std::string(HT_ENGINE_SOURCE_DIR) + "/data/showcase/industrial_park.scene.json";
  }

  nlohmann::json loadSceneJson() {
    std::ifstream input(scenePath());
    EXPECT_TRUE(input.good());
    nlohmann::json j;
    input >> j;
    return j;
  }

} // namespace

TEST(ShowcaseScene, IndustrialParkSchemaLoads) {
  auto j = loadSceneJson();
  ASSERT_FALSE(j.is_null());
  EXPECT_EQ(j.value("schema_version", 0), 1);
  EXPECT_EQ(j.value("name", ""), "xineng_benyuan_industrial_park");
  ASSERT_TRUE(j.contains("terrain"));
  EXPECT_EQ(j["terrain"].value("mode", ""), "procedural_with_open_data_fallback");
  ASSERT_TRUE(j.contains("source_model_assets"));
  EXPECT_GE(j["source_model_assets"].value("max_files", 0), 45);
  EXPECT_GE(j["source_model_assets"].value("fbx_files", 0), 7);
  ASSERT_TRUE(j["source_model_assets"].contains("direct_runtime_assets"));
  ASSERT_FALSE(j["source_model_assets"]["direct_runtime_assets"].empty());
  EXPECT_EQ(j["source_model_assets"]["direct_runtime_assets"][0].value("role", ""), "park_building_landmark");
  EXPECT_GE(j["source_model_assets"]["direct_runtime_assets"][0].value("triangles", 0), 500000);
  bool foundWarehouseLod = false;
  for (const auto& asset : j["source_model_assets"]["direct_runtime_assets"]) {
    if (asset.value("role", "") != "warehouse_logistics_rack_lod")
      continue;
    foundWarehouseLod = true;
    EXPECT_GE(asset.value("source_triangles", 0), 4000000);
    EXPECT_LE(asset.value("output_triangles", 999999), 60000);
    EXPECT_EQ(asset.value("lod", ""), "even_triangle_sampling");
  }
  EXPECT_TRUE(foundWarehouseLod);

  ASSERT_TRUE(j["source_model_assets"].contains("huilian_asset_intake"));
  const auto& huilian = j["source_model_assets"]["huilian_asset_intake"];
  EXPECT_EQ(huilian.value("source_root", ""), "assets/\u60e0\u70bc");
  EXPECT_EQ(huilian.value("mode", ""), "user_huilian_fbx_runtime_lod_and_legacy_dcc_export_queue");
  EXPECT_EQ(huilian.value("runtime_mode", ""), "converted_valve_and_pump_active_in_huilian_inspection_bay");
  EXPECT_EQ(huilian.value("total_files", 0), 1602);
  EXPECT_GE(huilian.value("total_bytes", 0LL), 1963000000LL);
  EXPECT_EQ(huilian.value("model_source_files", 0), 460);
  EXPECT_EQ(huilian.value("max_files", 0), 177);
  EXPECT_EQ(huilian.value("fbx_files", 0), 1);
  EXPECT_EQ(huilian.value("legacy_package_files", 0), 282);
  EXPECT_EQ(huilian.value("legacy_packages_with_local_max_peer", 0), 282);
  EXPECT_EQ(huilian.value("texture_files", 0), 1005);
  EXPECT_EQ(huilian.value("direct_runtime_assets", 0), 2);
  EXPECT_EQ(huilian.value("runtime_instances", 0), 2);
  EXPECT_EQ(huilian.value("runtime_triangles", 0), 912);
  EXPECT_EQ(huilian.value("material_manifest", ""),
            "data/showcase/huilian_equipment_material_manifest.json");
  EXPECT_EQ(huilian.value("format_policy", ""), "max_export_or_verified_legacy_decoder_then_gltf_lod");
  ASSERT_TRUE(huilian.contains("active_runtime_asset"));
  const auto& huilianValve = huilian["active_runtime_asset"];
  EXPECT_EQ(huilianValve.value("role", ""), "refinery_wz_check_valve_green");
  EXPECT_EQ(huilianValve.value("selected_equipment", ""), "WZ_check_valve_green");
  EXPECT_EQ(huilianValve.value("geometries", 0), 1625);
  EXPECT_GE(huilianValve.value("materials", 0), 380);
  EXPECT_EQ(huilianValve.value("source_triangles", 0), 135621);
  EXPECT_EQ(huilianValve.value("runtime_meshes", 0), 1);
  EXPECT_EQ(huilianValve.value("runtime_materials", 0), 324);
  EXPECT_EQ(huilianValve.value("output_triangles", 0), 400);
  EXPECT_EQ(huilianValve.value("lod", ""), "assimp_pretransform_center_ground_named_wz_mesh_lossless");
  EXPECT_FLOAT_EQ(huilianValve.value("scene_scale", 0.0f), 120.0f);
  ASSERT_TRUE(huilian.contains("active_runtime_assets"));
  ASSERT_EQ(huilian["active_runtime_assets"].size(), 2u);
  bool foundHuilianValve = false;
  bool foundProductionWaterPump = false;
  for (const auto& asset : huilian["active_runtime_assets"]) {
    const std::string equipment = asset.value("selected_equipment", "");
    if (equipment == "WZ_check_valve_green") {
      foundHuilianValve = true;
      EXPECT_EQ(asset.value("output_triangles", 0), 400);
      EXPECT_EQ(asset.value("runtime", ""), "assets/showcase/runtime/huilian_wz_check_valve_runtime.glb");
    } else if (equipment == "production_water_pump_p0401") {
      foundProductionWaterPump = true;
      EXPECT_EQ(asset.value("output_triangles", 0), 512);
      EXPECT_EQ(asset.value("runtime", ""),
                "assets/showcase/runtime/huilian_production_water_pump_p0401_runtime.glb");
      EXPECT_FALSE(asset.value("source_texture_available", true));
      EXPECT_EQ(asset.value("material_policy", ""),
                "real_geometry_legacy_diffuse_proxy_explicit_metallic_roughness");
    }
  }
  EXPECT_TRUE(foundHuilianValve);
  EXPECT_TRUE(foundProductionWaterPump);

  ASSERT_TRUE(j.contains("terrain"));
  const auto& terrain = j["terrain"];
  EXPECT_EQ(terrain.value("huilian_bin", ""), "assets/showcase/runtime/huilian_dem_30m.bin");
  ASSERT_TRUE(terrain.contains("huilian_bounds_m"));
  EXPECT_EQ(terrain["huilian_bounds_m"].size(), 4u);
  ASSERT_TRUE(terrain.contains("huilian_grid"));
  EXPECT_EQ(terrain["huilian_grid"][0].get<int>(), 408);
  EXPECT_EQ(terrain["huilian_grid"][1].get<int>(), 256);
  ASSERT_TRUE(terrain.contains("huilian_source"));
  const auto& huilianTerrain = terrain["huilian_source"];
  EXPECT_EQ(huilianTerrain.value("projection", ""), "WGS84");
  EXPECT_EQ(huilianTerrain.value("dem", ""), "assets/\u60e0\u70bc/DEM-DOM/hero_DEM_30M.tif");
  EXPECT_EQ(huilianTerrain.value("dom", ""), "assets/\u60e0\u70bc/DEM-DOM/hero_0.5M.tif");
  EXPECT_EQ(huilianTerrain["dem_grid"][0].get<int>(), 409);
  EXPECT_EQ(huilianTerrain["dem_grid"][1].get<int>(), 257);
  EXPECT_EQ(huilianTerrain["dom_grid"][0].get<int>(), 21170);
  EXPECT_EQ(huilianTerrain["dom_grid"][1].get<int>(), 13283);
  EXPECT_EQ(huilianTerrain.value("runtime_color_policy", ""),
            "wgs84_world_file_affine_dom_to_dem_plus_full_coverage_lod_pyramid_gpu_sampling");
  ASSERT_TRUE(terrain.contains("huilian_dom_gpu"));
  const auto& huilianDomGpu = terrain["huilian_dom_gpu"];
  EXPECT_EQ(huilianDomGpu.value("manifest", ""), "assets/showcase/runtime/huilian_dom_gpu_manifest.json");
  EXPECT_EQ(huilianDomGpu.value("overview", ""), "assets/showcase/runtime/huilian_dom_overview_2048.png");
  EXPECT_EQ(huilianDomGpu["overview_grid"][0].get<int>(), 2048);
  EXPECT_EQ(huilianDomGpu["overview_grid"][1].get<int>(), 1386);
  EXPECT_EQ(huilianDomGpu.value("detail", ""), "assets/showcase/runtime/huilian_dom_detail_valve_1024.png");
  EXPECT_EQ(huilianDomGpu["detail_grid"][0].get<int>(), 1024);
  EXPECT_EQ(huilianDomGpu["detail_grid"][1].get<int>(), 1024);
  EXPECT_FLOAT_EQ(huilianDomGpu["detail_resolution_m"][0].get<float>(), 0.5f);
  EXPECT_FLOAT_EQ(huilianDomGpu["detail_resolution_m"][1].get<float>(), 0.5f);
  EXPECT_EQ(huilianDomGpu.value("manifest_schema", ""), "hongtu.huilian_dom_gpu_textures.v7");
  EXPECT_EQ(huilianDomGpu.value("sampling", ""),
            "overview_plus_async_full_coverage_lod_pyramid_four_slot_priority_prefetch_crossfade");
  EXPECT_EQ(huilianDomGpu.value("shader_blend", ""), "descriptor_page_bounds_wgs84_uv_edge_feather");
  ASSERT_TRUE(huilianDomGpu.contains("tile_pyramid"));
  const auto& tilePyramid = huilianDomGpu["tile_pyramid"];
  EXPECT_EQ(tilePyramid.value("directory", ""), "assets/showcase/runtime/huilian_dom_pyramid");
  EXPECT_EQ(tilePyramid.value("coverage", ""), "full_dem_bounds_with_page_aligned_east_north_padding");
  EXPECT_EQ(tilePyramid.value("level_count", 0), 3);
  EXPECT_EQ(tilePyramid.value("total_page_count", 0), 1890);
  EXPECT_EQ(tilePyramid.value("total_page_bytes", 0LL), 150922432LL);
  EXPECT_EQ(tilePyramid["page_grid_px"][0].get<int>(), 512);
  EXPECT_EQ(tilePyramid["page_grid_px"][1].get<int>(), 512);
  EXPECT_EQ(tilePyramid["level_grids"][0], nlohmann::json::array({46, 31}));
  EXPECT_EQ(tilePyramid["level_grids"][1], nlohmann::json::array({23, 16}));
  EXPECT_EQ(tilePyramid["level_grids"][2], nlohmann::json::array({12, 8}));
  EXPECT_EQ(tilePyramid["level_page_counts"], nlohmann::json::array({1426, 368, 96}));
  EXPECT_EQ(tilePyramid["resident_working_set_pages"][0].get<int>(), 2);
  EXPECT_EQ(tilePyramid["resident_working_set_pages"][1].get<int>(), 2);
  EXPECT_EQ(tilePyramid.value("resident_slot_count", 0), 4);
  EXPECT_EQ(tilePyramid.value("resident_base_level_gpu_bytes_per_slot", 0), 4194304);
  EXPECT_EQ(tilePyramid.value("resident_gpu_bytes_per_slot", 0), 5592404);
  EXPECT_EQ(tilePyramid.value("resident_gpu_budget_bytes", 0), 22369616);
  EXPECT_EQ(tilePyramid.value("resident_mip_levels", 0), 11);
  EXPECT_EQ(tilePyramid.value("sampler", ""), "trilinear_16x_anisotropic_clamp_to_edge");
  EXPECT_EQ(tilePyramid.value("lod_hysteresis_m", nlohmann::json::array()), nlohmann::json({50.0, 75.0}));
  EXPECT_EQ(tilePyramid.value("cpu_loading", ""), "std_async_stb_jpeg_decode_cpu_box_mip_chain");
  EXPECT_EQ(tilePyramid.value("request_policy", ""),
            "foreground_preempts_directional_prefetch_epoch_cancel_stale_result_discard");
  EXPECT_EQ(tilePyramid.value("request_scheduler", ""), "single_worker_foreground_preempts_directional_prefetch");
  EXPECT_EQ(tilePyramid.value("prefetch_policy", ""), "camera_velocity_same_lod_one_atlas_lookahead");
  EXPECT_EQ(tilePyramid.value("cooperative_cancellation", ""), "epoch_token_page_mip_delay_checkpoints");
  EXPECT_EQ(tilePyramid.value("cache_policy", ""), "priority_aware_lru_hash_page_table");
  EXPECT_EQ(tilePyramid.value("page_table", ""), "cpu_hash_residency_to_gpu_atlas_slot");
  EXPECT_EQ(tilePyramid.value("eviction_policy", ""),
            "prefetched_first_then_true_lru_excluding_current_and_active_transition");
  EXPECT_EQ(tilePyramid.value("stream_telemetry", ""),
            "resident_upload_cache_frame_time_driver_budget_endurance_accounting");
  EXPECT_EQ(tilePyramid.value("driver_memory_budget", ""), "optional_vk_ext_memory_budget_device_local_heaps");
  EXPECT_EQ(tilePyramid.value("driver_resource_lifetime", ""), "vulkan_texture_raii_vma_image_view_release");
  EXPECT_EQ(tilePyramid.value("endurance_probe_seconds", 0), 60);
  EXPECT_EQ(tilePyramid.value("driver_peak_growth_allowance_bytes", 0), 89478464);
  EXPECT_EQ(tilePyramid.value("driver_plateau_growth_allowance_bytes", 0), 44739232);
  EXPECT_EQ(tilePyramid.value("cache_probe_station_count", 0), 12);
  EXPECT_DOUBLE_EQ(tilePyramid.value("crossfade_seconds", 0.0), 0.35);
  EXPECT_EQ(tilePyramid.value("transition_metadata_grid_px", nlohmann::json::array()), nlohmann::json({3, 1}));
  EXPECT_EQ(tilePyramid.value("gpu_commit", ""), "main_thread_fence_safe_descriptor_swap");

  ASSERT_TRUE(j["source_model_assets"].contains("max_only_runtime_proxy_assets"));
  const auto& maxProxies = j["source_model_assets"]["max_only_runtime_proxy_assets"];
  EXPECT_GE(maxProxies.size(), 4u);
  bool foundHydrantProxy = false;
  bool foundFireMonitorProxy = false;
  for (const auto& proxy : maxProxies) {
    EXPECT_EQ(proxy.value("runtime_mode", ""), "procedural_proxy_until_dcc_export");
    foundHydrantProxy |= proxy.value("role", "") == "warehouse_hydrant_stations";
    foundFireMonitorProxy |= proxy.value("role", "") == "parking_fire_water_monitor";
  }
  EXPECT_TRUE(foundHydrantProxy);
  EXPECT_TRUE(foundFireMonitorProxy);

  ASSERT_TRUE(j["source_model_assets"].contains("power_utility_proxy_assets"));
  const auto& powerProxy = j["source_model_assets"]["power_utility_proxy_assets"];
  EXPECT_EQ(powerProxy.value("preset", ""), "models");
  EXPECT_EQ(powerProxy.value("mode", ""), "max_only_high_voltage_power_corridor_procedural_runtime_proxies");
  EXPECT_EQ(powerProxy.value("runtime_mode", ""), "procedural_proxy_until_dcc_export_and_lod_partition");
  EXPECT_EQ(powerProxy.value("camera_focus", ""), "side_profile_corridor_when_power_mode");
  EXPECT_GE(powerProxy.value("source_asset_count", 0), 4);
  EXPECT_GE(powerProxy.value("source_asset_bytes", 0LL), 190000000LL);
  EXPECT_GE(powerProxy.value("transmission_towers", 0), 4);
  EXPECT_GE(powerProxy.value("tower_crossarms", 0), 12);
  EXPECT_GE(powerProxy.value("insulator_strings", 0), 24);
  EXPECT_GE(powerProxy.value("conductor_spans", 0), 18);
  EXPECT_GE(powerProxy.value("substation_transformers", 0), 3);
  EXPECT_GE(powerProxy.value("switchgear_bays", 0), 6);
  EXPECT_GE(powerProxy.value("monitor_cabinets", 0), 3);
  EXPECT_GE(powerProxy.value("scene_instances", 0), 100);
  ASSERT_TRUE(powerProxy.contains("source_assets"));
  EXPECT_GE(powerProxy["source_assets"].size(), 4u);
  bool foundHighVoltageTower = false;
  bool foundHeatStationTieIn = false;
  bool foundPowerCabinet = false;
  for (const auto& asset : powerProxy["source_assets"]) {
    EXPECT_GE(asset.value("proxy_instances", 0), 1);
    foundHighVoltageTower |= asset.value("role", "") == "high_voltage_transmission_tower";
    foundHeatStationTieIn |= asset.value("role", "") == "heat_station_tie_in";
    foundPowerCabinet |= asset.value("role", "") == "fire_power_monitor_cabinet";
  }
  EXPECT_TRUE(foundHighVoltageTower);
  EXPECT_TRUE(foundHeatStationTieIn);
  EXPECT_TRUE(foundPowerCabinet);

  ASSERT_TRUE(j["source_model_assets"].contains("site_operations_proxy_assets"));
  const auto& siteOps = j["source_model_assets"]["site_operations_proxy_assets"];
  EXPECT_EQ(siteOps.value("preset", ""), "models");
  EXPECT_EQ(siteOps.value("mode", ""), "max_only_site_operations_assets_procedural_runtime_proxies");
  EXPECT_EQ(siteOps.value("runtime_mode", ""), "procedural_proxy_until_dcc_export");
  EXPECT_GE(siteOps.value("source_asset_count", 0), 18);
  EXPECT_GE(siteOps.value("source_asset_bytes", 0LL), 14700000LL);
  EXPECT_GE(siteOps.value("scene_instances", 0), 60);
  EXPECT_GE(siteOps.value("guardhouses", 0), 3);
  EXPECT_GE(siteOps.value("barrier_gates", 0), 2);
  EXPECT_GE(siteOps.value("camera_poles", 0), 6);
  EXPECT_GE(siteOps.value("camera_heads", 0), 6);
  EXPECT_GE(siteOps.value("signage_screens", 0), 4);
  EXPECT_GE(siteOps.value("brand_signage_panels", 0), 1);
  EXPECT_GE(siteOps.value("street_lamps", 0), 8);
  EXPECT_GE(siteOps.value("heat_station_units", 0), 1);
  EXPECT_GE(siteOps.value("weighbridge_pads", 0), 1);
  EXPECT_GE(siteOps.value("parking_slots", 0), 12);
  EXPECT_GE(siteOps.value("lane_markings", 0), 16);
  EXPECT_GE(siteOps.value("route_links", 0), 8);
  EXPECT_GE(siteOps.value("proxy_beacons", 0), 18);
  ASSERT_TRUE(siteOps.contains("source_assets"));
  EXPECT_GE(siteOps["source_assets"].size(), 18u);
  bool foundGuardhouse = false;
  bool foundBarrierBase = false;
  bool foundCameraHead = false;
  bool foundStreetLamp = false;
  bool foundWeighbridge = false;
  bool foundLaneMarking = false;
  bool foundBrandFont = false;
  for (const auto& asset : siteOps["source_assets"]) {
    EXPECT_GE(asset.value("proxy_instances", 0), 1);
    foundGuardhouse |= asset.value("role", "") == "security_guardhouse_b";
    foundBarrierBase |= asset.value("role", "") == "barrier_gate_base";
    foundCameraHead |= asset.value("role", "") == "security_camera_head";
    foundStreetLamp |= asset.value("role", "") == "street_lamp";
    foundWeighbridge |= asset.value("role", "") == "weighbridge";
    foundLaneMarking |= asset.value("role", "") == "lane_marking";
    foundBrandFont |= asset.value("role", "") == "brand_font_landmark";
  }
  EXPECT_TRUE(foundGuardhouse);
  EXPECT_TRUE(foundBarrierBase);
  EXPECT_TRUE(foundCameraHead);
  EXPECT_TRUE(foundStreetLamp);
  EXPECT_TRUE(foundWeighbridge);
  EXPECT_TRUE(foundLaneMarking);
  EXPECT_TRUE(foundBrandFont);

  ASSERT_TRUE(j["source_model_assets"].contains("security_surveillance_source_proxy"));
  const auto& security = j["source_model_assets"]["security_surveillance_source_proxy"];
  EXPECT_EQ(security.value("preset", ""), "models");
  EXPECT_EQ(security.value("mode", ""), "max_camera_guardhouse_access_surveillance_source_proxy");
  EXPECT_EQ(security.value("runtime_mode", ""), "procedural_security_surveillance_proxy_until_dcc_export");
  EXPECT_GE(security.value("source_asset_count", 0), 12);
  EXPECT_GE(security.value("source_asset_bytes", 0LL), 11500000LL);
  EXPECT_GE(security.value("surveillance_nodes", 0), 6);
  EXPECT_GE(security.value("coverage_cones", 0), 6);
  EXPECT_GE(security.value("handoff_links", 0), 7);
  EXPECT_GE(security.value("access_interlocks", 0), 5);
  EXPECT_GE(security.value("plate_readers", 0), 2);
  EXPECT_GE(security.value("tripwire_segments", 0), 8);
  EXPECT_GE(security.value("blind_zone_patches", 0), 4);
  EXPECT_GE(security.value("command_displays", 0), 4);
  EXPECT_GE(security.value("brand_signage_panels", 0), 1);
  EXPECT_GE(security.value("source_beacons", 0), 12);
  EXPECT_GE(security.value("scene_instances", 0), 67);
  EXPECT_TRUE(security.value("tracks_catalog_mode", false));
  ASSERT_TRUE(security.contains("catalog_modes"));
  EXPECT_EQ(security["catalog_modes"].size(), 2u);
  ASSERT_TRUE(security.contains("source_assets"));
  EXPECT_GE(security["source_assets"].size(), 12u);
  bool foundSecurityPole = false;
  bool foundAccessGateDisplay = false;
  bool foundTripwireLane = false;
  bool foundSecurityBrandFont = false;
  for (const auto& asset : security["source_assets"]) {
    EXPECT_GE(asset.value("proxy_instances", 0), 1);
    foundSecurityPole |= asset.value("role", "") == "security_surveillance_pole_source";
    foundAccessGateDisplay |= asset.value("role", "") == "access_gate_display_source";
    foundTripwireLane |= asset.value("role", "") == "security_tripwire_lane_source";
    foundSecurityBrandFont |= asset.value("role", "") == "security_brand_font_landmark_source";
  }
  EXPECT_TRUE(foundSecurityPole);
  EXPECT_TRUE(foundAccessGateDisplay);
  EXPECT_TRUE(foundTripwireLane);
  EXPECT_TRUE(foundSecurityBrandFont);

  ASSERT_TRUE(j["source_model_assets"].contains("site_max_catalog"));
  const auto& siteCatalog = j["source_model_assets"]["site_max_catalog"];
  EXPECT_EQ(siteCatalog.value("preset", ""), "models");
  EXPECT_EQ(siteCatalog.value("mode", ""), "site_terrain_factory_power_safety_security_catalog_overlay");
  EXPECT_GE(siteCatalog.value("catalogued_files", 0), 19);
  EXPECT_GE(siteCatalog.value("selected_bytes", 0LL), 1000000000LL);
  EXPECT_GE(siteCatalog.value("heavy_assets", 0), 9);
  EXPECT_EQ(siteCatalog.value("direct_load_policy", ""), "max_requires_dcc_export_or_fbx_intermediate");
  EXPECT_EQ(siteCatalog.value("runtime_use_mode", ""), "gltf_lod_and_procedural_proxy_staging");
  EXPECT_GE(siteCatalog.value("coverage_footprints", 0), 8);
  EXPECT_GE(siteCatalog.value("conversion_stages", 0), 4);
  EXPECT_GE(siteCatalog.value("risk_links", 0), 6);
  EXPECT_GE(siteCatalog.value("anchor_beacons", 0), 19);
  ASSERT_TRUE(siteCatalog.contains("interactive_catalog_modes"));
  const auto& catalogModes = siteCatalog["interactive_catalog_modes"];
  ASSERT_TRUE(catalogModes.contains("modes"));
  EXPECT_GE(catalogModes["modes"].size(), 6u);
  EXPECT_EQ(catalogModes.value("cli", ""), "--max-catalog-mode=all|site|warehouse|safety|power|security");
  ASSERT_TRUE(catalogModes.contains("counts"));
  EXPECT_EQ(catalogModes["counts"]["all"].value("heavy_assets", 0), 9);
  EXPECT_EQ(catalogModes["counts"]["warehouse"].value("heavy_assets", -1), 1);
  EXPECT_EQ(catalogModes["counts"]["safety"].value("risk_links", 0), 3);
  ASSERT_TRUE(siteCatalog.contains("selected_assets"));
  EXPECT_GE(siteCatalog["selected_assets"].size(), 19u);
  bool foundHeavyMergedPlant = false;
  bool foundWarehouseHeavy = false;
  bool foundCatalogBrandFont = false;
  for (const auto& asset : siteCatalog["selected_assets"]) {
    foundHeavyMergedPlant |= asset.value("role", "") == "merged_chemical_plant_heavy";
    foundWarehouseHeavy |= asset.value("role", "") == "warehouse_logistics_heavy";
    foundCatalogBrandFont |= asset.value("role", "") == "security_brand_font_landmark";
  }
  EXPECT_TRUE(foundHeavyMergedPlant);
  EXPECT_TRUE(foundWarehouseHeavy);
  EXPECT_TRUE(foundCatalogBrandFont);

  ASSERT_TRUE(j["source_model_assets"].contains("source_asset_coverage_audit"));
  const auto& coverage = j["source_model_assets"]["source_asset_coverage_audit"];
  EXPECT_EQ(coverage.value("preset", ""), "models");
  EXPECT_EQ(coverage.value("mode", ""), "all_max_fbx_sources_mapped_to_runtime_capability_layers");
  EXPECT_EQ(coverage.value("model_source_assets", 0), 52);
  EXPECT_EQ(coverage.value("covered_source_assets", 0), 52);
  EXPECT_EQ(coverage.value("native_max_files", 0), 45);
  EXPECT_EQ(coverage.value("interchange_fbx_files", 0), 7);
  EXPECT_GE(coverage.value("model_source_bytes", 0LL), 1726000000LL);
  EXPECT_GE(coverage.value("coverage_percent", 0.0), 100.0);
  EXPECT_GE(coverage.value("coverage_groups", 0), 8);
  EXPECT_GE(coverage.value("runtime_capability_layers", 0), 18);
  EXPECT_GE(coverage.value("asset_grid_cells", 0), 52);
  EXPECT_GE(coverage.value("dcc_pipeline_stages", 0), 4);
  EXPECT_GE(coverage.value("evidence_links", 0), 12);
  EXPECT_GE(coverage.value("source_beacons", 0), 8);
  EXPECT_EQ(coverage.value("unreferenced_model_sources", -1), 0);
  EXPECT_GE(coverage.value("scene_instances", 0), 103);
}

TEST(ShowcaseScene, RequiredCameraPresetsExist) {
  auto j = loadSceneJson();
  ASSERT_TRUE(j.contains("camera_presets"));
  const auto& presets = j["camera_presets"];
  for (const char* name : {"overview",
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
                           "performance"}) {
    ASSERT_TRUE(presets.contains(name)) << name;
    EXPECT_EQ(presets[name]["eye"].size(), 3u);
    EXPECT_EQ(presets[name]["target"].size(), 3u);
    EXPECT_GT(presets[name].value("fov_deg", 0.0), 20.0);
  }
  EXPECT_EQ(presets["huilian"]["target"][0].get<float>(), 780.0f);
  EXPECT_EQ(presets["huilian"]["target"][1].get<float>(), 520.0f);
}

TEST(ShowcaseScene, XinengWarehouseInspectionPresetExists) {
  auto j = loadSceneJson();
  ASSERT_TRUE(j.contains("source_model_assets"));
  const auto& sourceAssets = j["source_model_assets"];
  ASSERT_TRUE(sourceAssets.contains("warehouse_max_proxy_assets"));
  const auto& warehouseProxy = sourceAssets["warehouse_max_proxy_assets"];
  EXPECT_EQ(warehouseProxy.value("preset", ""), "warehouse");
  EXPECT_EQ(warehouseProxy.value("mode", ""), "max_only_warehouse_shell_logistics_procedural_runtime_proxies");
  EXPECT_EQ(warehouseProxy.value("runtime_mode", ""), "procedural_proxy_until_dcc_export");
  EXPECT_GE(warehouseProxy.value("source_asset_count", 0), 12);
  EXPECT_GE(warehouseProxy.value("source_asset_bytes", 0LL), 139000000LL);
  EXPECT_GE(warehouseProxy.value("shell_source_files", 0), 10);
  EXPECT_GE(warehouseProxy.value("shell_bays", 0), 11);
  EXPECT_GE(warehouseProxy.value("forklift_bays", 0), 1);
  EXPECT_GE(warehouseProxy.value("loading_docks", 0), 14);
  EXPECT_GE(warehouseProxy.value("rollup_doors", 0), 22);
  EXPECT_GE(warehouseProxy.value("rack_source_references", 0), 1);
  EXPECT_GE(warehouseProxy.value("yard_crates", 0), 24);
  EXPECT_GE(warehouseProxy.value("traffic_lanes", 0), 10);
  EXPECT_GE(warehouseProxy.value("canopy_sections", 0), 6);
  EXPECT_GE(warehouseProxy.value("logistics_route_links", 0), 6);
  EXPECT_GE(warehouseProxy.value("proxy_beacons", 0), 12);
  EXPECT_GE(warehouseProxy.value("scene_instances", 0), 100);
  ASSERT_TRUE(warehouseProxy.contains("source_assets"));
  EXPECT_GE(warehouseProxy["source_assets"].size(), 12u);
  bool foundWarehousePair = false;
  bool foundForkliftBay = false;
  bool foundRackMaxReference = false;
  for (const auto& asset : warehouseProxy["source_assets"]) {
    EXPECT_GE(asset.value("proxy_instances", 0), 1);
    foundWarehousePair |= asset.value("role", "") == "warehouse_shell_04_05";
    foundForkliftBay |= asset.value("role", "") == "forklift_bay";
    foundRackMaxReference |= asset.value("role", "") == "warehouse_rack_max_source_reference";
  }
  EXPECT_TRUE(foundWarehousePair);
  EXPECT_TRUE(foundForkliftBay);
  EXPECT_TRUE(foundRackMaxReference);

  ASSERT_TRUE(sourceAssets.contains("warehouse_street_hazmat_proxy_assets"));
  const auto& streetHazmat = sourceAssets["warehouse_street_hazmat_proxy_assets"];
  EXPECT_EQ(streetHazmat.value("preset", ""), "overview_warehouse");
  EXPECT_EQ(streetHazmat.value("mode", ""), "tourable_warehouse_street_hazmat_vehicle_runtime_proxies");
  EXPECT_EQ(streetHazmat.value("runtime_mode", ""), "procedural_proxy_until_dcc_export");
  EXPECT_GE(streetHazmat.value("road_segments", 0), 7);
  EXPECT_GE(streetHazmat.value("intersections", 0), 3);
  EXPECT_GE(streetHazmat.value("dock_aprons", 0), 5);
  EXPECT_GE(streetHazmat.value("guard_bollards", 0), 42);
  EXPECT_GE(streetHazmat.value("crosswalk_bars", 0), 20);
  EXPECT_GE(streetHazmat.value("direction_arrows", 0), 10);
  EXPECT_GE(streetHazmat.value("tour_route_links", 0), 7);
  EXPECT_GE(streetHazmat.value("tour_route_length_m", 0.0), 400.0);
  EXPECT_GE(streetHazmat.value("hazmat_vehicle_convoy_trucks", 0), 6);
  EXPECT_GE(streetHazmat.value("hazmat_vehicle_placards", 0), 24);
  EXPECT_GE(streetHazmat.value("hazmat_vehicle_wheel_sets", 0), 18);
  EXPECT_GE(streetHazmat.value("hazmat_vehicle_containment_pads", 0), 6);
  EXPECT_GE(streetHazmat.value("scene_instances", 0), 140);
  ASSERT_TRUE(streetHazmat.contains("source_assets"));
  EXPECT_GE(streetHazmat["source_assets"].size(), 6u);
  bool foundStreetGrid = false;
  bool foundHazmatConvoy = false;
  bool foundDetectionGantry = false;
  for (const auto& asset : streetHazmat["source_assets"]) {
    EXPECT_GE(asset.value("proxy_instances", 0), 1);
    foundStreetGrid |= asset.value("role", "") == "tourable_warehouse_street_grid";
    foundHazmatConvoy |= asset.value("role", "") == "hazmat_tanker_truck_convoy";
    foundDetectionGantry |= asset.value("role", "") == "hazmat_vehicle_detection_gantry";
  }
  EXPECT_TRUE(foundStreetGrid);
  EXPECT_TRUE(foundHazmatConvoy);
  EXPECT_TRUE(foundDetectionGantry);

  ASSERT_TRUE(sourceAssets.contains("dcc_export_priority_queue"));
  const auto& dccQueue = sourceAssets["dcc_export_priority_queue"];
  EXPECT_EQ(dccQueue.value("preset", ""), "performance");
  EXPECT_EQ(dccQueue.value("mode", ""), "max_to_gltf_priority_queue_material_lod_handoff");
  EXPECT_EQ(dccQueue.value("runtime_mode", ""), "performance_preset_dcc_export_readiness_overlay");
  EXPECT_EQ(dccQueue.value("exact_mesh_runtime_policy", ""),
            "max_authoring_sources_require_fbx_glb_export_before_runtime_load");
  EXPECT_GE(dccQueue.value("priority_assets", 0), 10);
  EXPECT_GE(dccQueue.value("heavy_partition_assets", 0), 7);
  EXPECT_GE(dccQueue.value("export_batches", 0), 4);
  EXPECT_GE(dccQueue.value("gate_checks", 0), 6);
  EXPECT_EQ(dccQueue.value("fbx_ready_assets", 0), 7);
  EXPECT_EQ(dccQueue.value("runtime_converted_assets", 0), 7);
  EXPECT_EQ(dccQueue.value("proxy_backed_max_assets", 0), 45);
  EXPECT_GE(dccQueue.value("material_slot_queue_files", 0), 700);
  EXPECT_GE(dccQueue.value("texture_sidecar_files", 0), 800);
  EXPECT_GE(dccQueue.value("scene_instances", 0), 90);
  ASSERT_TRUE(dccQueue.contains("priority_source_assets"));
  EXPECT_GE(dccQueue["priority_source_assets"].size(), 10u);
  bool foundHeavyRoot = false;
  bool foundFirewater = false;
  bool foundWarehouseRack = false;
  for (const auto& asset : dccQueue["priority_source_assets"]) {
    EXPECT_FALSE(asset.value("export_batch", "").empty());
    EXPECT_GT(asset.value("size_bytes", 0), 0);
    foundHeavyRoot |= asset.value("role", "") == "whole_plant_heavy_root";
    foundFirewater |= asset.value("role", "") == "warehouse_hydrant_heavy_source";
    foundWarehouseRack |= asset.value("role", "") == "warehouse_rack_max_source";
  }
  EXPECT_TRUE(foundHeavyRoot);
  EXPECT_TRUE(foundFirewater);
  EXPECT_TRUE(foundWarehouseRack);

  ASSERT_TRUE(sourceAssets.contains("inspection_presets"));
  ASSERT_TRUE(sourceAssets["inspection_presets"].contains("warehouse"));
  const auto& warehouse = sourceAssets["inspection_presets"]["warehouse"];
  EXPECT_EQ(warehouse.value("role", ""), "warehouse_logistics_rack_lod");
  EXPECT_GE(warehouse.value("active_instances", 0), 3);
  EXPECT_GE(warehouse.value("source_triangles", 0), 4000000);
  EXPECT_LE(warehouse.value("output_triangles", 999999), 60000);
  EXPECT_EQ(warehouse.value("overlay", ""), "closeup_selection_scan_and_bounds");
  ASSERT_TRUE(warehouse.contains("impact_analysis"));
  const auto& impact = warehouse["impact_analysis"];
  EXPECT_EQ(impact.value("mode", ""), "process_safety_impact_overlay");
  EXPECT_GE(impact.value("affected_process_nodes", 0), 5);
  EXPECT_GE(impact.value("process_links", 0), 4);
  EXPECT_GE(impact.value("response_routes", 0), 2);
  EXPECT_GE(impact.value("firewater_coverage_rings", 0), 3);
  EXPECT_GE(impact.value("evacuation_links", 0), 3);

  ASSERT_TRUE(j.contains("presets"));
  bool foundGisPreset = false;
  bool foundFusionPreset = false;
  bool foundMaterialsPreset = false;
  bool foundProcessPreset = false;
  bool foundDisassemblyPreset = false;
  bool foundSprinklerPreset = false;
  bool foundRoutesPreset = false;
  bool foundWarehousePreset = false;
  bool foundDrillPreset = false;
  bool foundPerformancePreset = false;
  for (const auto& preset : j["presets"]) {
    if (preset.get<std::string>() == "gis")
      foundGisPreset = true;
    if (preset.get<std::string>() == "fusion")
      foundFusionPreset = true;
    if (preset.get<std::string>() == "materials")
      foundMaterialsPreset = true;
    if (preset.get<std::string>() == "process")
      foundProcessPreset = true;
    if (preset.get<std::string>() == "disassembly")
      foundDisassemblyPreset = true;
    if (preset.get<std::string>() == "sprinkler")
      foundSprinklerPreset = true;
    if (preset.get<std::string>() == "routes")
      foundRoutesPreset = true;
    if (preset.get<std::string>() == "warehouse")
      foundWarehousePreset = true;
    if (preset.get<std::string>() == "drill")
      foundDrillPreset = true;
    if (preset.get<std::string>() == "performance")
      foundPerformancePreset = true;
  }
  EXPECT_TRUE(foundGisPreset);
  EXPECT_TRUE(foundFusionPreset);
  EXPECT_TRUE(foundMaterialsPreset);
  EXPECT_TRUE(foundProcessPreset);
  EXPECT_TRUE(foundDisassemblyPreset);
  EXPECT_TRUE(foundSprinklerPreset);
  EXPECT_TRUE(foundRoutesPreset);
  EXPECT_TRUE(foundWarehousePreset);
  EXPECT_TRUE(foundDrillPreset);
  EXPECT_TRUE(foundPerformancePreset);
}

TEST(ShowcaseScene, RenderEfficiencyTelemetryMetadataExists) {
  auto j = loadSceneJson();
  ASSERT_TRUE(j.contains("render_efficiency"));
  const auto& efficiency = j["render_efficiency"];
  EXPECT_EQ(efficiency.value("preset", ""), "performance");
  EXPECT_EQ(efficiency.value("mode", ""), "lod_stress_frame_budget_telemetry_overlay");
  EXPECT_GE(efficiency.value("target_fps", 0), 60);
  EXPECT_LE(efficiency.value("frame_budget_ms", 99.0), 16.8);
  EXPECT_GE(efficiency.value("telemetry_bars", 0), 6);
  EXPECT_GE(efficiency.value("lod_rows", 0), 4);
  EXPECT_GE(efficiency.value("stress_profiles", 0), 2);
  EXPECT_GE(efficiency.value("culling_zones", 0), 5);
  EXPECT_GE(efficiency.value("budget_markers", 0), 4);
  EXPECT_GE(efficiency.value("batch_nodes", 0), 7);
  EXPECT_GE(efficiency.value("lod_source_triangles", 0), 4000000);
  EXPECT_LE(efficiency.value("lod_output_triangles", 999999), 60000);
  EXPECT_LE(efficiency.value("lod_reduction_ratio", 1.0), 0.02);
  EXPECT_GE(efficiency.value("model_stress_instances", 0), 1000);
  EXPECT_GE(efficiency.value("particle_stress_instances", 0), 50000);
  EXPECT_EQ(efficiency.value("draw_call_model", ""), "single_indexed_scene_batch_with_material_kind_channels");
  EXPECT_GE(efficiency.value("material_kind_channels", 0), 5);
  EXPECT_LE(efficiency.value("push_constant_bytes", 999), 128);
  EXPECT_EQ(efficiency.value("descriptor_sets", 0), 1);
  EXPECT_TRUE(efficiency.value("runtime_fps_title", false));
  EXPECT_EQ(efficiency.value("visual_clearance", ""), "suppressed_cloud_fire_gas_geometry_for_budget_telemetry");
  ASSERT_TRUE(efficiency.contains("postfx_stack"));
  const auto& postfx = efficiency["postfx_stack"];
  EXPECT_EQ(postfx.value("mode", ""), "hdr_ssao_bloom_tonemap_fxaa_colorgrade_budget_stack");
  EXPECT_EQ(postfx.value("runtime_mode", ""), "performance_preset_visual_postfx_stack_audit");
  EXPECT_GE(postfx.value("stack_stages", 0), 9);
  EXPECT_GE(postfx.value("hdr_targets", 0), 2);
  EXPECT_TRUE(postfx.value("auto_exposure", false));
  EXPECT_GE(postfx.value("ssao_samples", 0), 32);
  EXPECT_GE(postfx.value("bloom_mips", 0), 5);
  EXPECT_GE(postfx.value("tonemap_operators", 0), 5);
  ASSERT_TRUE(postfx.contains("tonemap_modes"));
  EXPECT_EQ(postfx["tonemap_modes"].size(), 5u);
  EXPECT_GE(postfx.value("fxaa_taps", 0), 5);
  EXPECT_GE(postfx.value("grade_controls", 0), 8);
  EXPECT_EQ(postfx.value("fullscreen_antialiasing_pass", ""), "2x_supersample_box_resolve_for_showcase_screenshots");
  EXPECT_TRUE(postfx.value("fullscreen_antialiasing_active", false));
  EXPECT_GE(postfx.value("fullscreen_antialiasing_samples", 0), 4);
  EXPECT_GE(postfx.value("budget_bars", 0), 9);
  EXPECT_GE(postfx.value("cost_markers", 0), 12);
  EXPECT_GE(postfx.value("scene_instances", 0), 82);
  ASSERT_TRUE(efficiency.contains("resource_budget"));
  const auto& budget = efficiency["resource_budget"];
  EXPECT_EQ(budget.value("mode", ""), "gpu_memory_residency_streaming_upload_heap_audit");
  EXPECT_EQ(budget.value("runtime_mode", ""), "performance_preset_gpu_resource_budget_overlay");
  EXPECT_GE(budget.value("gpu_budget_mb", 0), 4096);
  EXPECT_LE(budget.value("resident_mb", 9999), budget.value("gpu_budget_mb", 0));
  EXPECT_GE(budget.value("headroom_percent", 0.0), 60.0);
  EXPECT_GE(budget.value("memory_pools", 0), 5);
  EXPECT_GE(budget.value("residency_bars", 0), 12);
  EXPECT_GE(budget.value("upload_lanes", 0), 4);
  EXPECT_GE(budget.value("heap_markers", 0), 5);
  EXPECT_GE(budget.value("eviction_markers", 0), 8);
  EXPECT_GE(budget.value("alias_windows", 0), 6);
  EXPECT_EQ(budget.value("resident_mb", 0),
            budget.value("geometry_mb", 0) + budget.value("texture_mb", 0) + budget.value("particle_mb", 0) +
                budget.value("postfx_mb", 0) + budget.value("upload_staging_mb", 0));
  EXPECT_GE(budget.value("frame_upload_mb", 0.0), 48.0);
  EXPECT_GE(budget.value("streaming_batch_mb", 0), 32);
  EXPECT_GE(budget.value("source_folder_bytes", 0LL), 2200000000LL);
  EXPECT_GE(budget.value("source_model_bytes", 0LL), 1720000000LL);
  EXPECT_GE(budget.value("source_texture_bytes", 0LL), 482000000LL);
  EXPECT_TRUE(budget.value("tracks_whole_plant_streaming", false));
  EXPECT_GE(budget.value("scene_instances", 0), 40);
  ASSERT_TRUE(efficiency.contains("whole_plant_max_streaming"));
  const auto& streaming = efficiency["whole_plant_max_streaming"];
  EXPECT_EQ(streaming.value("mode", ""), "whole_plant_max_partition_lod_streaming_budget_proxy");
  EXPECT_EQ(streaming.value("runtime_mode", ""), "procedural_streaming_proxy_until_dcc_export_partition");
  EXPECT_EQ(streaming.value("source_asset_count", 0), 6);
  EXPECT_GE(streaming.value("source_asset_bytes", 0), 843000000);
  EXPECT_GE(streaming.value("root_asset_bytes", 0), 608000000);
  EXPECT_GE(streaming.value("partitions", 0), 24);
  EXPECT_GE(streaming.value("visible_cells", 0), 16);
  EXPECT_GE(streaming.value("resident_cells", 0), 12);
  EXPECT_GE(streaming.value("lod_tiers", 0), 4);
  EXPECT_GE(streaming.value("culling_tiles", 0), 12);
  EXPECT_GE(streaming.value("occlusion_portals", 0), 8);
  EXPECT_GE(streaming.value("budget_bars", 0), 6);
  EXPECT_GE(streaming.value("proxy_hulls", 0), 18);
  EXPECT_GE(streaming.value("material_buckets", 0), 5);
  EXPECT_GE(streaming.value("source_anchors", 0), 6);
  EXPECT_GE(streaming.value("resident_budget_mb", 0), 256);
  EXPECT_GE(streaming.value("cell_size_m", 0.0), 96.0);
  EXPECT_GE(streaming.value("scene_instances", 0), 117);
  ASSERT_TRUE(streaming.contains("source_assets"));
  EXPECT_GE(streaming["source_assets"].size(), 6u);
}

TEST(ShowcaseScene, EmergencyDrillScenarioMetadataExists) {
  auto j = loadSceneJson();
  ASSERT_TRUE(j.contains("emergency_drill"));
  const auto& drill = j["emergency_drill"];
  EXPECT_EQ(drill.value("preset", ""), "drill");
  EXPECT_EQ(drill.value("mode", ""), "leak_to_fire_integrated_response_timeline_overlay");
  EXPECT_EQ(drill.value("scenario", ""), "data/scenarios/leak_to_fire.json");
  EXPECT_EQ(drill.value("local_scenario", ""), "data/scenarios/leak_to_fire_local.json");
  EXPECT_EQ(drill.value("duration_seconds", 0), 45);
  EXPECT_GE(drill.value("timeline_stages", 0), 6);
  EXPECT_GE(drill.value("scenario_annotations", 0), 4);
  EXPECT_GE(drill.value("scenario_tracks", 0), 7);
  EXPECT_GE(drill.value("command_posts", 0), 2);
  EXPECT_GE(drill.value("response_teams", 0), 4);
  EXPECT_GE(drill.value("subsystem_links", 0), 6);
  EXPECT_GE(drill.value("isolation_actions", 0), 4);
  EXPECT_GE(drill.value("weather_sensors", 0), 5);
  EXPECT_GE(drill.value("status_beacons", 0), 12);
  ASSERT_TRUE(drill.contains("decision_audit"));
  const auto& decisionAudit = drill["decision_audit"];
  EXPECT_EQ(decisionAudit.value("mode", ""), "sensor_alarm_command_ack_closed_loop");
  EXPECT_GE(decisionAudit.value("decision_gates", 0), 6);
  EXPECT_GE(decisionAudit.value("trigger_thresholds", 0), 6);
  EXPECT_GE(decisionAudit.value("command_ack_cards", 0), 6);
  EXPECT_GE(decisionAudit.value("closed_loop_checks", 0), 6);
  EXPECT_GE(decisionAudit.value("stage_handoffs", 0), 5);
  EXPECT_GE(decisionAudit.value("sensor_vote_quorum", 0), 3);
  EXPECT_LE(decisionAudit.value("latency_budget_seconds", 999.0), 120.0);
  EXPECT_GE(decisionAudit.value("scene_instances", 0), 29);
  EXPECT_TRUE(decisionAudit.value("tracks_scenario_time", false));
  EXPECT_EQ(drill.value("playback_control", ""), "--scenario-time=SECONDS");
  ASSERT_TRUE(drill.contains("sample_times_seconds"));
  EXPECT_GE(drill["sample_times_seconds"].size(), 3u);
  ASSERT_TRUE(drill.contains("interactive_playback"));
  const auto& playback = drill["interactive_playback"];
  ASSERT_TRUE(playback.contains("controls"));
  EXPECT_GE(playback["controls"].size(), 4u);
  EXPECT_EQ(playback.value("duration_seconds", 0), 45);
  EXPECT_TRUE(playback.value("stage_rebuilds", false));
  ASSERT_TRUE(playback.contains("hotkeys"));
  EXPECT_GE(playback["hotkeys"].size(), 3u);
  EXPECT_TRUE(drill.value("wind_coupled", false));
  ASSERT_TRUE(drill.contains("integrated_subsystems"));
  EXPECT_GE(drill["integrated_subsystems"].size(), 6u);
}

TEST(ShowcaseScene, LightingAndWeatherAreBounded) {
  auto j = loadSceneJson();
  const auto& env = j["environment"];
  EXPECT_GE(env.value("fog_density", -1.0), 0.0);
  EXPECT_LE(env.value("fog_density", 2.0), 1.0);
  EXPECT_GE(env.value("rain_amount", -1.0), 0.0);
  EXPECT_LE(env.value("rain_amount", 2.0), 1.0);
  EXPECT_GE(env.value("snow_amount", -1.0), 0.0);
  EXPECT_LE(env.value("snow_amount", 2.0), 1.0);
  EXPECT_GE(env.value("cloud_amount", -1.0), 0.0);
  EXPECT_LE(env.value("cloud_amount", 2.0), 1.0);
  ASSERT_TRUE(env.contains("wind"));
  EXPECT_EQ(env["wind"].size(), 3u);
  EXPECT_GT(env.value("wind_speed", 0.0), 0.0);

  ASSERT_TRUE(j.contains("point_lights"));
  EXPECT_GE(j["point_lights"].size(), 16u);
}

TEST(ShowcaseScene, PipelineNetworkUsesOrthogonalFlangedTieIns) {
  auto j = loadSceneJson();
  ASSERT_TRUE(j.contains("pipeline_network"));
  const auto& pipeline = j["pipeline_network"];
  EXPECT_EQ(pipeline.value("layout", ""), "orthogonal_xy_runs_vertical_risers");
  EXPECT_EQ(pipeline.value("equipment_tie_in", ""), "flanged_short_spool_connections");
  EXPECT_EQ(pipeline.value("diagonal_process_pipe_segments", -1), 0);
  EXPECT_GE(pipeline.value("axis_aligned_route_solver_passes", 0), 2);
  EXPECT_EQ(pipeline.value("route_solver_scope", ""), "routed_process_pipes_cables_lamp_arms_overlay_links");
  EXPECT_EQ(pipeline.value("attached_weld_band_pass", ""), "flush_bead_bands_no_floating_sleeves");
  EXPECT_GE(pipeline.value("attached_weld_bands", 0), 96);
  EXPECT_EQ(pipeline.value("pipe_material_uvs", ""), "circumference_length_brushed_metal_channels");
  EXPECT_GE(pipeline.value("structured_pipe_rack_levels", 0), 4);
  EXPECT_GE(pipeline.value("structured_pipe_rack_runs", 0), 18);
  ASSERT_TRUE(pipeline.contains("foreground_readability_overlay"));
  const auto& readability = pipeline["foreground_readability_overlay"];
  EXPECT_EQ(readability.value("mode", ""), "box_portal_columns_low_luma_supports_no_white_rod_field");
  EXPECT_GE(readability.value("portal_columns", 0), 56);
  EXPECT_LE(readability.value("support_luma_scale", 1.0), 0.70);
  EXPECT_TRUE(readability.value("vertical_clutter_reduced", false));
  EXPECT_TRUE(readability.value("process_pipe_paths_remain_axis_aligned", false));
  EXPECT_GE(pipeline.value("branch_connections", 0), 11);
  EXPECT_GE(pipeline.value("equipment_flanged_connections", 0), 22);
  EXPECT_GE(pipeline.value("equipment_flange_plates", 0), 44);
  EXPECT_GE(pipeline.value("equipment_flange_bolt_markers", 0), 264);
  EXPECT_GE(pipeline.value("equipment_tie_in_gaskets", 0), 22);
  EXPECT_GE(pipeline.value("equipment_tie_in_short_spools", 0), 22);
  EXPECT_EQ(pipeline.value("equipment_connection_detail", ""),
            "double_flange_short_spool_gasket_bolt_ring_at_equipment_crossings");
  EXPECT_TRUE(pipeline.value("requires_flanges_at_equipment_crossings", false));
  ASSERT_TRUE(pipeline.contains("operability_overlay"));
  const auto& operability = pipeline["operability_overlay"];
  EXPECT_EQ(operability.value("mode", ""), "rack_layered_manifolds_valve_platforms_flanged_tie_ins");
  EXPECT_GE(operability.value("manifold_stations", 0), 6);
  EXPECT_GE(operability.value("orthogonal_cross_headers", 0), 12);
  EXPECT_GE(operability.value("vertical_risers", 0), 24);
  EXPECT_GE(operability.value("valve_stations", 0), 12);
  EXPECT_GE(operability.value("inspection_platforms", 0), 6);
  EXPECT_GE(operability.value("pipe_clamp_sets", 0), 48);
  EXPECT_EQ(operability.value("nonorthogonal_process_pipe_segments", -1), 0);
  EXPECT_EQ(operability.value("unflanged_equipment_crossings", -1), 0);
  EXPECT_EQ(operability.value("visual_rule", ""), "axis_aligned_process_pipe_segments_only");
}

TEST(ShowcaseScene, EffectsDeclareExpectedParticleSources) {
  auto j = loadSceneJson();
  ASSERT_TRUE(j.contains("effects"));
  const auto& effects = j["effects"];
  ASSERT_TRUE(effects.contains("gas_plume"));
  ASSERT_TRUE(effects.contains("gas_diffusion_analysis"));
  ASSERT_TRUE(effects.contains("pool_fire"));
  ASSERT_TRUE(effects.contains("jet_fire"));
  ASSERT_TRUE(effects.contains("tank_explosion"));
  ASSERT_TRUE(effects.contains("cooling_water_fountain"));
  EXPECT_GE(effects["gas_plume"].value("particles", 0), 150);
  EXPECT_EQ(effects["gas_diffusion_analysis"].value("mode", ""),
            "wind_driven_ground_footprint_and_evacuation_guidance");
  EXPECT_GE(effects["gas_diffusion_analysis"].value("concentration_bands", 0), 4);
  EXPECT_GE(effects["gas_diffusion_analysis"].value("crosswind_samples", 0), 10);
  EXPECT_GE(effects["gas_diffusion_analysis"].value("evacuation_vectors", 0), 3);
  EXPECT_GE(effects["gas_diffusion_analysis"].value("sensor_posts", 0), 6);
  EXPECT_GE(effects["gas_diffusion_analysis"].value("wind_probe_vectors", 0), 8);
  EXPECT_TRUE(effects["gas_diffusion_analysis"].value("realtime_wind_response", false));
  ASSERT_TRUE(effects["gas_diffusion_analysis"].contains("realtime_wind_field"));
  const auto& windField = effects["gas_diffusion_analysis"]["realtime_wind_field"];
  EXPECT_EQ(windField.value("mode", ""), "live_wind_vector_streamlines_crosswind_envelope");
  EXPECT_GE(windField.value("streamlines", 0), 12);
  EXPECT_GE(windField.value("envelope_ribs", 0), 8);
  EXPECT_GE(windField.value("speed_samples", 0), 6);
  EXPECT_GE(windField.value("profile_cards", 0), 4);
  EXPECT_TRUE(windField.value("tracks_cli_wind_overrides", false));
  ASSERT_TRUE(effects["gas_diffusion_analysis"].contains("source_wind_gas_sensor_network"));
  const auto& sourceNetwork = effects["gas_diffusion_analysis"]["source_wind_gas_sensor_network"];
  EXPECT_EQ(sourceNetwork.value("mode", ""), "max_detector_tower_camera_power_wind_profile_gas_sensor_proxy");
  EXPECT_EQ(sourceNetwork.value("runtime_mode", ""), "procedural_wind_gas_sensor_proxy_until_dcc_export");
  EXPECT_GE(sourceNetwork.value("source_asset_count", 0), 7);
  EXPECT_GE(sourceNetwork.value("source_asset_bytes", 0), 198000000);
  EXPECT_GE(sourceNetwork.value("sensor_nodes", 0), 6);
  EXPECT_GE(sourceNetwork.value("profile_towers", 0), 3);
  EXPECT_GE(sourceNetwork.value("wind_samples", 0), 8);
  EXPECT_GE(sourceNetwork.value("plume_sample_links", 0), 10);
  EXPECT_GE(sourceNetwork.value("alarm_links", 0), 4);
  EXPECT_GE(sourceNetwork.value("evacuation_gates", 0), 3);
  EXPECT_GE(sourceNetwork.value("profile_cards", 0), 6);
  EXPECT_GE(sourceNetwork.value("source_beacons", 0), 7);
  EXPECT_GE(sourceNetwork.value("scene_instances", 0), 54);
  EXPECT_TRUE(sourceNetwork.value("wind_response", false));
  ASSERT_TRUE(sourceNetwork.contains("source_assets"));
  EXPECT_GE(sourceNetwork["source_assets"].size(), 7u);
  ASSERT_TRUE(effects["gas_diffusion_analysis"].contains("interactive_diffusion_modes"));
  const auto& gasModes = effects["gas_diffusion_analysis"]["interactive_diffusion_modes"];
  EXPECT_EQ(gasModes.value("cli", ""), "--gas-mode=all|plume|footprint|evacuation|sensors");
  ASSERT_TRUE(gasModes.contains("modes"));
  EXPECT_EQ(gasModes["modes"].size(), 5u);
  EXPECT_EQ(gasModes["modes"][0].get<std::string>(), "all");
  EXPECT_EQ(gasModes["modes"][1].get<std::string>(), "plume");
  EXPECT_EQ(gasModes["modes"][2].get<std::string>(), "footprint");
  EXPECT_EQ(gasModes["modes"][3].get<std::string>(), "evacuation");
  EXPECT_EQ(gasModes["modes"][4].get<std::string>(), "sensors");
  ASSERT_TRUE(gasModes.contains("hotkeys"));
  EXPECT_EQ(gasModes["hotkeys"].size(), 2u);
  EXPECT_EQ(gasModes["hotkeys"][0].get<std::string>(), "5");
  EXPECT_EQ(gasModes["hotkeys"][1].get<std::string>(), "H");
  EXPECT_TRUE(gasModes.value("control_panel_readout", false));
  EXPECT_GE(effects["pool_fire"].value("particles", 0), 120);
  EXPECT_GE(effects["jet_fire"].value("particles", 0), 80);
  EXPECT_EQ(effects["tank_explosion"].value("mode", ""), "tank_fireball_shockwave_debris_smoke_crown");
  EXPECT_GE(effects["tank_explosion"].value("fireball_puffs", 0), 80);
  EXPECT_GE(effects["tank_explosion"].value("shockwave_rings", 0), 5);
  EXPECT_GE(effects["tank_explosion"].value("pressure_bands", 0), 4);
  EXPECT_GE(effects["tank_explosion"].value("pressure_vectors", 0), 8);
  EXPECT_GE(effects["tank_explosion"].value("debris_sparks", 0), 70);
  EXPECT_GE(effects["tank_explosion"].value("smoke_crown_puffs", 0), 60);
  EXPECT_TRUE(effects["tank_explosion"].value("wind_coupled_smoke", false));
  ASSERT_TRUE(effects["tank_explosion"].contains("source_response_network"));
  const auto& fireExplosionSource = effects["tank_explosion"]["source_response_network"];
  EXPECT_EQ(fireExplosionSource.value("mode", ""), "max_detector_firewater_camera_alarm_source_response_proxy");
  EXPECT_EQ(fireExplosionSource.value("runtime_mode", ""), "procedural_fire_explosion_response_proxy_until_dcc_export");
  EXPECT_GE(fireExplosionSource.value("source_asset_count", 0), 9);
  EXPECT_GE(fireExplosionSource.value("source_asset_bytes", 0), 153000000);
  EXPECT_GE(fireExplosionSource.value("detector_triggers", 0), 6);
  EXPECT_GE(fireExplosionSource.value("ignition_links", 0), 6);
  EXPECT_GE(fireExplosionSource.value("shockwave_source_rings", 0), 5);
  EXPECT_GE(fireExplosionSource.value("overpressure_panels", 0), 6);
  EXPECT_GE(fireExplosionSource.value("firewater_response_links", 0), 8);
  EXPECT_GE(fireExplosionSource.value("camera_tracks", 0), 4);
  EXPECT_GE(fireExplosionSource.value("alarm_displays", 0), 4);
  EXPECT_GE(fireExplosionSource.value("source_beacons", 0), 9);
  EXPECT_GE(fireExplosionSource.value("scene_instances", 0), 57);
  EXPECT_TRUE(fireExplosionSource.value("tracks_particle_preset", false));
  ASSERT_TRUE(fireExplosionSource.contains("source_assets"));
  EXPECT_GE(fireExplosionSource["source_assets"].size(), 9u);
  EXPECT_GE(effects["cooling_water_fountain"].value("water_jets", 0), 36);
  EXPECT_GE(effects["cooling_water_fountain"].value("mist_puffs", 0), 32);
  EXPECT_GE(effects["cooling_water_fountain"].value("ripple_discs", 0), 10);
  EXPECT_GE(effects["cooling_water_fountain"].value("spray_canopies", 0), 2);
  EXPECT_GE(effects["cooling_water_fountain"].value("wind_vectors", 0), 8);
  EXPECT_TRUE(effects["cooling_water_fountain"].value("wind_affected", false));
  ASSERT_TRUE(effects["cooling_water_fountain"].contains("interactive_fountain_modes"));
  const auto& fountainModes = effects["cooling_water_fountain"]["interactive_fountain_modes"];
  EXPECT_EQ(fountainModes.value("cli", ""), "--fountain-mode=all|jets|mist|ripples|wind");
  ASSERT_TRUE(fountainModes.contains("modes"));
  EXPECT_EQ(fountainModes["modes"].size(), 5u);
  ASSERT_TRUE(fountainModes.contains("counts"));
  EXPECT_EQ(fountainModes["counts"]["all"].value("active_layers", 0), 4);
  EXPECT_EQ(fountainModes["counts"]["wind"].value("wind_vectors", 0), 8);
  EXPECT_EQ(fountainModes["counts"]["ripples"].value("ripple_discs", 0), 10);
  ASSERT_TRUE(effects.contains("weather_source_telemetry"));
  const auto& weatherTelemetry = effects["weather_source_telemetry"];
  EXPECT_EQ(weatherTelemetry.value("mode", ""), "max_display_lamp_firetower_weather_warning_proxy");
  EXPECT_EQ(weatherTelemetry.value("runtime_mode", ""), "procedural_weather_telemetry_proxy_until_dcc_export");
  EXPECT_GE(weatherTelemetry.value("source_asset_count", 0), 12);
  EXPECT_GE(weatherTelemetry.value("source_asset_bytes", 0), 17300000);
  EXPECT_GE(weatherTelemetry.value("telemetry_masts", 0), 5);
  EXPECT_GE(weatherTelemetry.value("alert_displays", 0), 4);
  EXPECT_GE(weatherTelemetry.value("lamp_wet_beacons", 0), 8);
  EXPECT_GE(weatherTelemetry.value("rain_gauges", 0), 6);
  EXPECT_GE(weatherTelemetry.value("fog_visibility_posts", 0), 5);
  EXPECT_GE(weatherTelemetry.value("snow_depth_pins", 0), 6);
  EXPECT_GE(weatherTelemetry.value("fountain_wind_links", 0), 8);
  EXPECT_GE(weatherTelemetry.value("source_beacons", 0), 12);
  EXPECT_GE(weatherTelemetry.value("scene_instances", 0), 66);
  EXPECT_TRUE(weatherTelemetry.value("tracks_fountain_wind_mode", false));
  ASSERT_TRUE(weatherTelemetry.contains("source_assets"));
  EXPECT_GE(weatherTelemetry["source_assets"].size(), 12u);
}

TEST(ShowcaseScene, OperationalTwinAnalysisMetadataExists) {
  auto j = loadSceneJson();

  ASSERT_TRUE(j.contains("gis_measurements"));
  const auto& gis = j["gis_measurements"];
  ASSERT_TRUE(gis.contains("distance_line"));
  ASSERT_TRUE(gis.contains("area_polygon"));
  ASSERT_TRUE(gis.contains("elevation_profile"));
  ASSERT_TRUE(gis.contains("terrain_volume"));
  EXPECT_EQ(gis["area_polygon"]["vertices"].size(), 4u);
  const auto& terrainVolume = gis["terrain_volume"];
  EXPECT_EQ(terrainVolume["vertices"].size(), 4u);
  EXPECT_EQ(terrainVolume.value("mode", ""), "dem_cut_fill_net_volume");
  EXPECT_EQ(terrainVolume.value("terrain_surface", ""), "rendered_dem_surface_bilinear_with_visible_grade_blend");
  EXPECT_EQ(terrainVolume.value("reference_policy", ""), "mean_boundary_vertex_elevation");
  EXPECT_LE(terrainVolume.value("sample_spacing_m", 99.0), 30.0);
  EXPECT_TRUE(terrainVolume.value("wgs84_anchor", false));
  EXPECT_GE(terrainVolume.value("visualization_samples", 0), 20);
  EXPECT_GE(terrainVolume.value("reference_grid_lines", 0), 11);
  ASSERT_TRUE(gis.contains("interactive_measurement"));
  const auto& interactiveGis = gis["interactive_measurement"];
  EXPECT_EQ(interactiveGis.value("cli", ""), "--gis-measure=all|distance|area|elevation|volume");
  ASSERT_TRUE(interactiveGis.contains("modes"));
  EXPECT_EQ(interactiveGis["modes"].size(), 5u);
  ASSERT_TRUE(interactiveGis.contains("hotkeys"));
  EXPECT_GE(interactiveGis["hotkeys"].size(), 2u);
  EXPECT_TRUE(interactiveGis.value("control_panel_readout", false));
  ASSERT_TRUE(gis.contains("digital_earth_gis"));
  const auto& digitalEarth = gis["digital_earth_gis"];
  EXPECT_EQ(digitalEarth.value("mode", ""), "globe_graticule_tile_footprints_measurement_volume_projection");
  EXPECT_GE(digitalEarth.value("globe_meridians", 0), 12);
  EXPECT_GE(digitalEarth.value("globe_parallels", 0), 7);
  EXPECT_GE(digitalEarth.value("tile_footprints", 0), 16);
  EXPECT_GE(digitalEarth.value("projection_links", 0), 6);
  EXPECT_GE(digitalEarth.value("coordinate_grid_lines", 0), 12);
  EXPECT_GE(digitalEarth.value("sample_markers", 0), 9);
  EXPECT_EQ(digitalEarth.value("measurement_units", ""), "meters_square_meters_cubic_meters_height_delta");
  ASSERT_TRUE(digitalEarth.contains("georef_audit"));
  const auto& georef = digitalEarth["georef_audit"];
  EXPECT_EQ(georef.value("mode", ""), "epsg3857_bounds_wgs84_webmercator_tile_audit");
  EXPECT_EQ(georef.value("source_crs", ""), "EPSG:3857");
  EXPECT_EQ(georef.value("target_crs", ""), "EPSG:4326");
  EXPECT_EQ(georef.value("tile_matrix", ""), "WebMercatorQuad");
  EXPECT_NEAR(georef.value("center_lat_deg", 0.0), 38.945098, 0.001);
  EXPECT_NEAR(georef.value("center_lon_deg", 0.0), 117.593455, 0.001);
  EXPECT_GE(georef.value("mercator_width_m", 0.0), 103000.0);
  EXPECT_GE(georef.value("mercator_height_m", 0.0), 111000.0);
  EXPECT_GE(georef.value("dom_meters_per_sample", 0.0), 100.0);
  EXPECT_EQ(georef.value("tile_zoom", 0), 15);
  EXPECT_GE(georef.value("tile_columns", 0), 86);
  EXPECT_GE(georef.value("tile_rows", 0), 92);
  EXPECT_GE(georef.value("georef_anchors", 0), 4);
  EXPECT_GE(georef.value("transform_pipelines", 0), 3);
  EXPECT_LE(georef.value("roundtrip_error_cm", 99.0), 5.0);
  ASSERT_TRUE(gis.contains("source_survey_control_network"));
  const auto& surveyControl = gis["source_survey_control_network"];
  EXPECT_EQ(surveyControl.value("mode", ""), "max_terrain_road_landmark_survey_control_proxy");
  EXPECT_EQ(surveyControl.value("runtime_mode", ""), "procedural_gis_control_proxy_until_dcc_export");
  EXPECT_GE(surveyControl.value("source_asset_count", 0), 8);
  EXPECT_GE(surveyControl.value("source_asset_bytes", 0), 16700000);
  EXPECT_GE(surveyControl.value("control_points", 0), 9);
  EXPECT_GE(surveyControl.value("tile_anchors", 0), 16);
  EXPECT_GE(surveyControl.value("datum_posts", 0), 3);
  EXPECT_GE(surveyControl.value("distance_tie_lines", 0), 2);
  EXPECT_GE(surveyControl.value("area_tie_lines", 0), 4);
  EXPECT_GE(surveyControl.value("elevation_tie_lines", 0), 2);
  EXPECT_GE(surveyControl.value("volume_tie_lines", 0), 4);
  EXPECT_GE(surveyControl.value("source_beacons", 0), 8);
  EXPECT_GE(surveyControl.value("scene_instances", 0), 56);
  EXPECT_TRUE(surveyControl.value("measurement_modes", false));
  ASSERT_TRUE(surveyControl.contains("source_assets"));
  EXPECT_GE(surveyControl["source_assets"].size(), 8u);

  ASSERT_TRUE(j.contains("operations_analysis"));
  const auto& process = j["operations_analysis"]["process_flow"];
  EXPECT_EQ(process.value("mode", ""), "directed_upstream_downstream");
  EXPECT_GE(process.value("nodes", 0), 7);
  EXPECT_GE(process.value("edges", 0), 6);
  EXPECT_GE(process.value("upstream_highlighted_nodes", 0), 3);
  EXPECT_GE(process.value("downstream_highlighted_nodes", 0), 4);
  EXPECT_EQ(process.value("visual_layout", ""), "orthogonal_folded_xy_runs_vertical_risers");
  EXPECT_EQ(process.value("equipment_tie_in", ""), "flanged_short_spool_connections");
  EXPECT_EQ(process.value("diagonal_pipe_segments", -1), 0);
  EXPECT_EQ(process.value("flanged_connections_per_active_edge", 0), 2);
  EXPECT_GE(process.value("visual_route_length_m", 0.0), 780.0);
  ASSERT_TRUE(process.contains("visual_route"));
  EXPECT_GE(process["visual_route"].size(), 12u);
  ASSERT_TRUE(process.contains("management_overlay"));
  const auto& management = process["management_overlay"];
  EXPECT_EQ(management.value("preset", ""), "process");
  EXPECT_EQ(management.value("mode", ""), "isolation_bypass_dependency_shutdown_overlay");
  EXPECT_GE(management.value("isolation_valves", 0), 4);
  EXPECT_GE(management.value("bypass_links", 0), 3);
  EXPECT_GE(management.value("dependency_beams", 0), 6);
  EXPECT_GE(management.value("impact_zones", 0), 5);
  EXPECT_GE(management.value("shutdown_stages", 0), 2);
  EXPECT_GE(management.value("topology_panel_nodes", 0), 7);
  EXPECT_GE(management.value("flow_pulse_markers", 0), 18);
  ASSERT_TRUE(management.contains("interactive_analysis_modes"));
  const auto& processModes = management["interactive_analysis_modes"];
  EXPECT_EQ(processModes.value("cli", ""), "--process-mode=all|upstream|downstream|isolation|shutdown");
  ASSERT_TRUE(processModes.contains("modes"));
  EXPECT_EQ(processModes["modes"].size(), 5u);
  EXPECT_EQ(processModes["modes"][0].get<std::string>(), "all");
  EXPECT_EQ(processModes["modes"][1].get<std::string>(), "upstream");
  EXPECT_EQ(processModes["modes"][2].get<std::string>(), "downstream");
  EXPECT_EQ(processModes["modes"][3].get<std::string>(), "isolation");
  EXPECT_EQ(processModes["modes"][4].get<std::string>(), "shutdown");
  ASSERT_TRUE(processModes.contains("hotkeys"));
  EXPECT_EQ(processModes["hotkeys"].size(), 1u);
  EXPECT_EQ(processModes["hotkeys"][0].get<std::string>(), "O");
  EXPECT_TRUE(processModes.value("control_panel_readout", false));
  ASSERT_TRUE(management.contains("source_impact_overlay"));
  const auto& sourceImpact = management["source_impact_overlay"];
  EXPECT_EQ(sourceImpact.value("mode", ""), "max_source_process_lineage_impact_analysis_proxy");
  EXPECT_EQ(sourceImpact.value("runtime_mode", ""), "procedural_process_unit_proxy_until_dcc_export");
  EXPECT_EQ(sourceImpact.value("source_asset_count", 0), 7);
  EXPECT_GE(sourceImpact.value("source_asset_bytes", 0), 771000000);
  EXPECT_GE(sourceImpact.value("root_asset_bytes", 0), 608000000);
  EXPECT_GE(sourceImpact.value("unit_anchors", 0), 7);
  EXPECT_GE(sourceImpact.value("source_bindings", 0), 7);
  EXPECT_GE(sourceImpact.value("lineage_edges", 0), 6);
  EXPECT_GE(sourceImpact.value("upstream_impact_cards", 0), 3);
  EXPECT_GE(sourceImpact.value("downstream_demand_cards", 0), 4);
  EXPECT_GE(sourceImpact.value("isolation_propagation_links", 0), 6);
  EXPECT_GE(sourceImpact.value("shutdown_locks", 0), 2);
  EXPECT_GE(sourceImpact.value("scada_readouts", 0), 7);
  EXPECT_GE(sourceImpact.value("budget_columns", 0), 7);
  EXPECT_GE(sourceImpact.value("source_beacons", 0), 7);
  EXPECT_GE(sourceImpact.value("scene_instances", 0), 56);
  ASSERT_TRUE(sourceImpact.contains("source_assets"));
  EXPECT_GE(sourceImpact["source_assets"].size(), 7u);

  ASSERT_TRUE(management.contains("control_interlock_source_overlay"));
  const auto& control = management["control_interlock_source_overlay"];
  EXPECT_EQ(control.value("mode", ""), "max_screen_detector_firepower_scada_interlock_proxy");
  EXPECT_EQ(control.value("runtime_mode", ""), "procedural_scada_interlock_proxy_until_dcc_export");
  EXPECT_EQ(control.value("source_asset_count", 0), 10);
  EXPECT_GE(control.value("source_asset_bytes", 0), 15000000);
  EXPECT_GE(control.value("control_rooms", 0), 1);
  EXPECT_GE(control.value("detector_nodes", 0), 6);
  EXPECT_GE(control.value("operator_screens", 0), 5);
  EXPECT_GE(control.value("command_links", 0), 6);
  EXPECT_GE(control.value("ack_cards", 0), 7);
  EXPECT_GE(control.value("interlock_relays", 0), 6);
  EXPECT_GE(control.value("alarm_buses", 0), 4);
  EXPECT_GE(control.value("shutdown_locks", 0), 2);
  EXPECT_GE(control.value("source_beacons", 0), 10);
  ASSERT_TRUE(control.contains("scene_instances"));
  EXPECT_GE(control["scene_instances"].value("all", 0), 57);
  EXPECT_GE(control["scene_instances"].value("isolation", 0), 42);
  EXPECT_GE(control["scene_instances"].value("shutdown", 0), 38);
  EXPECT_TRUE(control.value("tracks_process_modes", false));
  ASSERT_TRUE(control.contains("source_assets"));
  EXPECT_GE(control["source_assets"].size(), 10u);
  bool foundDetectorSource = false;
  bool foundFirePowerScada = false;
  bool foundControlRoom = false;
  for (const auto& asset : control["source_assets"]) {
    foundDetectorSource |= asset.value("role", "") == "indoor_detector_process_sample_source";
    foundFirePowerScada |= asset.value("role", "") == "fire_power_scada_interlock_source";
    foundControlRoom |= asset.value("role", "") == "control_room_building_source";
  }
  EXPECT_TRUE(foundDetectorSource);
  EXPECT_TRUE(foundFirePowerScada);
  EXPECT_TRUE(foundControlRoom);

  ASSERT_TRUE(management.contains("telemetry_trend_overlay"));
  const auto& telemetry = management["telemetry_trend_overlay"];
  EXPECT_EQ(telemetry.value("mode", ""), "dcs_realtime_pressure_temperature_flow_gas_valve_trends");
  EXPECT_EQ(telemetry.value("runtime_mode", ""), "process_preset_scada_trend_alarm_interlock_overlay");
  EXPECT_EQ(telemetry.value("source_asset_count", 0), 10);
  EXPECT_GE(telemetry.value("source_asset_bytes", 0), 15000000);
  EXPECT_EQ(telemetry.value("source_assets_reused_from", ""), "control_interlock_source_overlay");
  EXPECT_GE(telemetry.value("channels", 0), 6);
  EXPECT_GE(telemetry.value("trend_samples", 0), 18);
  EXPECT_GE(telemetry.value("threshold_bands", 0), 4);
  EXPECT_GE(telemetry.value("alarm_beacons", 0), 6);
  EXPECT_GE(telemetry.value("interlock_cards", 0), 5);
  EXPECT_GE(telemetry.value("sensor_vote_inputs", 0), 6);
  EXPECT_GE(telemetry.value("control_loops", 0), 4);
  EXPECT_GE(telemetry.value("orthogonal_links", 0), 6);
  EXPECT_GE(telemetry.value("flanged_sensor_tie_ins", 0), 6);
  EXPECT_EQ(telemetry.value("diagonal_segments", -1), 0);
  EXPECT_GE(telemetry.value("pressure_mpa", 0.0), 2.3);
  EXPECT_GE(telemetry.value("pressure_high_mpa", 0.0), 2.8);
  EXPECT_GE(telemetry.value("temperature_c", 0.0), 80.0);
  EXPECT_GE(telemetry.value("temperature_high_c", 0.0), 90.0);
  EXPECT_GE(telemetry.value("flow_tph", 0.0), 120.0);
  EXPECT_GE(telemetry.value("gas_alarm_ppm", 0.0), 25.0);
  EXPECT_GE(telemetry.value("health_percent", 0.0), 90.0);
  EXPECT_TRUE(telemetry.value("tracks_process_modes", false));
  EXPECT_EQ(telemetry.value("sensor_tie_in", ""), "orthogonal_links_with_flanged_transmitter_taps");
  EXPECT_GE(telemetry.value("scene_instances", 0), 61);

  ASSERT_TRUE(j.contains("safety_planning"));
  const auto& safety = j["safety_planning"];
  EXPECT_GE(safety["fire_rescue_route"].size(), 6u);
  EXPECT_GE(safety["personnel_evacuation_route"].size(), 7u);
  ASSERT_TRUE(safety.contains("route_planning_overlay"));
  const auto& routeOverlay = safety["route_planning_overlay"];
  EXPECT_EQ(routeOverlay.value("preset", ""), "routes");
  EXPECT_EQ(routeOverlay.value("mode", ""), "graph_reroute_capacity_eta_overlay");
  EXPECT_GE(routeOverlay.value("graph_nodes", 0), 14);
  EXPECT_GE(routeOverlay.value("graph_edges", 0), 18);
  EXPECT_GE(routeOverlay.value("blocked_hazard_zones", 0), 3);
  EXPECT_GE(routeOverlay.value("alternate_segments", 0), 6);
  EXPECT_GE(routeOverlay.value("muster_zones", 0), 3);
  EXPECT_GE(routeOverlay.value("triage_zones", 0), 2);
  EXPECT_GE(routeOverlay.value("vehicle_staging_markers", 0), 4);
  EXPECT_GE(routeOverlay.value("decision_gates", 0), 5);
  EXPECT_GE(routeOverlay.value("capacity_bars", 0), 12);
  EXPECT_GE(routeOverlay.value("eta_markers", 0), 13);
  ASSERT_TRUE(routeOverlay.contains("route_solver"));
  const auto& routeSolver = routeOverlay["route_solver"];
  EXPECT_EQ(routeSolver.value("mode", ""), "multi_constraint_a_star_hazard_capacity_eta_audit");
  EXPECT_EQ(routeSolver.value("algorithm", ""), "a_star_weighted_length_hazard_capacity_clearance");
  EXPECT_GE(routeSolver.value("candidate_routes", 0), 4);
  EXPECT_GE(routeSolver.value("blocked_edges", 0), 3);
  EXPECT_GE(routeSolver.value("open_edges", 0), 15);
  EXPECT_GE(routeSolver.value("chosen_path_nodes", 0), 6);
  EXPECT_GE(routeSolver.value("chosen_path_length_m", 0.0), 500.0);
  EXPECT_GE(routeSolver.value("eta_minutes", 0.0), 9.0);
  EXPECT_GE(routeSolver.value("capacity_persons_per_min", 0.0), 180.0);
  EXPECT_GE(routeSolver.value("safety_margin", 0.0), 0.70);
  EXPECT_GE(routeSolver.value("constraint_layers", 0), 5);
  EXPECT_GE(routeSolver.value("cost_terms", 0), 6);
  EXPECT_GE(routeSolver.value("capacity_samples", 0), 8);
  EXPECT_EQ(routeSolver.value("tracks_route_mode", ""), "combined_and_reroute");
  ASSERT_TRUE(routeOverlay.contains("wind_gas_reroute"));
  const auto& windGasRoute = routeOverlay["wind_gas_reroute"];
  EXPECT_EQ(windGasRoute.value("mode", ""), "wind_field_gas_plume_exposure_cost_route_audit");
  EXPECT_EQ(windGasRoute.value("runtime_mode", ""), "routes_preset_wind_gas_cost_overlay");
  EXPECT_EQ(windGasRoute.value("incident_source", ""), "route_blocked_zone_gas_release");
  EXPECT_TRUE(windGasRoute.value("uses_gas_diffusion_wind_model", false));
  ASSERT_TRUE(windGasRoute.contains("incident_source_xy"));
  EXPECT_EQ(windGasRoute["incident_source_xy"].size(), 2u);
  EXPECT_GE(windGasRoute.value("exposure_samples", 0), 16);
  EXPECT_GE(windGasRoute.value("hazard_cost_cells", 0), 10);
  EXPECT_GE(windGasRoute.value("shelter_gates", 0), 4);
  EXPECT_GE(windGasRoute.value("blocked_edges", 0), 3);
  EXPECT_GE(windGasRoute.value("safe_corridors", 0), 3);
  EXPECT_GE(windGasRoute.value("sensor_fusion_inputs", 0), 4);
  EXPECT_GE(windGasRoute.value("escape_vectors", 0), 6);
  EXPECT_GE(windGasRoute.value("penalty_weight", 0.0), 2.0);
  EXPECT_GE(windGasRoute.value("safe_exposure_limit", 0.0), 0.55);
  EXPECT_GT(windGasRoute.value("static_route_max_exposure", 0.0), 0.30);
  EXPECT_LE(windGasRoute.value("chosen_route_max_exposure", 1.0), windGasRoute.value("safe_exposure_limit", 0.0));
  EXPECT_GE(windGasRoute.value("exposure_reduction_percent", 0.0), 90.0);
  EXPECT_EQ(windGasRoute.value("tracks_route_mode", ""), "combined_and_reroute");
  EXPECT_GE(windGasRoute.value("scene_instances", 0), 46);
  ASSERT_TRUE(routeOverlay.contains("interactive_route_modes"));
  const auto& routeModes = routeOverlay["interactive_route_modes"];
  EXPECT_EQ(routeModes.value("cli", ""), "--route-mode=combined|rescue|evacuation|reroute");
  ASSERT_TRUE(routeModes.contains("modes"));
  EXPECT_EQ(routeModes["modes"].size(), 4u);
  EXPECT_EQ(routeModes["modes"][0].get<std::string>(), "combined");
  EXPECT_EQ(routeModes["modes"][1].get<std::string>(), "rescue");
  EXPECT_EQ(routeModes["modes"][2].get<std::string>(), "evacuation");
  EXPECT_EQ(routeModes["modes"][3].get<std::string>(), "reroute");
  ASSERT_TRUE(routeModes.contains("hotkeys"));
  EXPECT_EQ(routeModes["hotkeys"].size(), 2u);
  EXPECT_EQ(routeModes["hotkeys"][0].get<std::string>(), "V");
  EXPECT_EQ(routeModes["hotkeys"][1].get<std::string>(), "Y");
  EXPECT_TRUE(routeModes.value("control_panel_readout", false));
  ASSERT_TRUE(routeOverlay.contains("source_dispatch_assets"));
  const auto& sourceDispatch = routeOverlay["source_dispatch_assets"];
  EXPECT_EQ(sourceDispatch.value("mode", ""), "max_access_fire_security_asset_route_dispatch_proxy");
  EXPECT_EQ(sourceDispatch.value("runtime_mode", ""), "procedural_route_asset_proxy_until_dcc_export");
  EXPECT_GE(sourceDispatch.value("source_asset_count", 0), 14);
  EXPECT_GE(sourceDispatch.value("source_asset_bytes", 0), 155000000);
  EXPECT_GE(sourceDispatch.value("access_nodes", 0), 5);
  EXPECT_GE(sourceDispatch.value("camera_nodes", 0), 6);
  EXPECT_GE(sourceDispatch.value("firewater_nodes", 0), 8);
  EXPECT_GE(sourceDispatch.value("command_nodes", 0), 5);
  EXPECT_GE(sourceDispatch.value("rescue_dispatch_links", 0), 10);
  EXPECT_GE(sourceDispatch.value("evacuation_dispatch_links", 0), 9);
  EXPECT_GE(sourceDispatch.value("traffic_control_segments", 0), 8);
  EXPECT_GE(sourceDispatch.value("capacity_feeds", 0), 6);
  EXPECT_GE(sourceDispatch.value("source_beacons", 0), 14);
  EXPECT_GE(sourceDispatch.value("scene_instances", 0), 85);
  ASSERT_TRUE(sourceDispatch.contains("source_assets"));
  EXPECT_GE(sourceDispatch["source_assets"].size(), 14u);
  EXPECT_GE(safety["sprinkler"].value("heads", 0), 16);
  EXPECT_GE(safety["sprinkler"].value("water_jets", 0), 64);
  ASSERT_TRUE(safety["sprinkler"].contains("detail_overlay"));
  const auto& sprinkler = safety["sprinkler"]["detail_overlay"];
  EXPECT_EQ(sprinkler.value("preset", ""), "sprinkler");
  EXPECT_EQ(sprinkler.value("mode", ""), "zone_valve_pressure_deluge_wind_wetfloor_overlay");
  EXPECT_GE(sprinkler.value("activation_zones", 0), 4);
  EXPECT_GE(sprinkler.value("coverage_footprints", 0), 16);
  EXPECT_GE(sprinkler.value("zone_control_valves", 0), 4);
  EXPECT_GE(sprinkler.value("pressure_gauges", 0), 8);
  EXPECT_GE(sprinkler.value("pump_skids", 0), 2);
  EXPECT_GE(sprinkler.value("deluge_curtains", 0), 4);
  EXPECT_GE(sprinkler.value("runoff_channels", 0), 8);
  EXPECT_GE(sprinkler.value("wind_drift_vectors", 0), 10);
  EXPECT_GE(sprinkler.value("wet_floor_patches", 0), 18);
  EXPECT_GE(sprinkler.value("spray_canopies", 0), 4);
  EXPECT_TRUE(sprinkler.value("wind_response", false));
  ASSERT_TRUE(sprinkler.contains("interactive_sprinkler_modes"));
  const auto& sprinklerModes = sprinkler["interactive_sprinkler_modes"];
  EXPECT_EQ(sprinklerModes.value("cli", ""), "--sprinkler-mode=all|heads|monitors|deluge|runoff");
  ASSERT_TRUE(sprinklerModes.contains("modes"));
  EXPECT_EQ(sprinklerModes["modes"].size(), 5u);
  EXPECT_EQ(sprinklerModes["modes"][0].get<std::string>(), "all");
  EXPECT_EQ(sprinklerModes["modes"][1].get<std::string>(), "heads");
  EXPECT_EQ(sprinklerModes["modes"][2].get<std::string>(), "monitors");
  EXPECT_EQ(sprinklerModes["modes"][3].get<std::string>(), "deluge");
  EXPECT_EQ(sprinklerModes["modes"][4].get<std::string>(), "runoff");
  ASSERT_TRUE(sprinklerModes.contains("hotkeys"));
  EXPECT_EQ(sprinklerModes["hotkeys"].size(), 1u);
  EXPECT_EQ(sprinklerModes["hotkeys"][0].get<std::string>(), "I");
  EXPECT_TRUE(sprinklerModes.value("control_panel_readout", false));
  ASSERT_TRUE(sprinkler.contains("source_hydraulic_network"));
  const auto& hydraulic = sprinkler["source_hydraulic_network"];
  EXPECT_EQ(hydraulic.value("mode", ""), "max_hydrant_monitor_pressure_runoff_source_proxy");
  EXPECT_EQ(hydraulic.value("runtime_mode", ""), "procedural_sprinkler_hydraulic_proxy_until_dcc_export");
  EXPECT_GE(hydraulic.value("source_asset_count", 0), 10);
  EXPECT_GE(hydraulic.value("source_asset_bytes", 0), 153000000);
  EXPECT_GE(hydraulic.value("hydrant_feeds", 0), 6);
  EXPECT_GE(hydraulic.value("monitor_intakes", 0), 2);
  EXPECT_GE(hydraulic.value("pressure_loop_links", 0), 8);
  EXPECT_GE(hydraulic.value("pump_buses", 0), 4);
  EXPECT_GE(hydraulic.value("deluge_feed_links", 0), 4);
  EXPECT_GE(hydraulic.value("runoff_recovery_links", 0), 8);
  EXPECT_GE(hydraulic.value("command_displays", 0), 2);
  EXPECT_GE(hydraulic.value("source_beacons", 0), 10);
  EXPECT_GE(hydraulic.value("scene_instances", 0), 54);
  EXPECT_TRUE(hydraulic.value("tracks_sprinkler_modes", false));
  ASSERT_TRUE(hydraulic.contains("source_assets"));
  EXPECT_GE(hydraulic["source_assets"].size(), 10u);
  ASSERT_TRUE(safety.contains("max_emergency_proxy_assets"));
  const auto& maxEmergency = safety["max_emergency_proxy_assets"];
  EXPECT_EQ(maxEmergency.value("mode", ""), "max_only_fire_assets_procedural_runtime_proxies");
  EXPECT_EQ(maxEmergency.value("runtime_mode", ""), "procedural_proxy_until_dcc_export");
  EXPECT_GE(maxEmergency.value("hydrant_stations", 0), 6);
  EXPECT_GE(maxEmergency.value("fire_water_monitors", 0), 2);
  EXPECT_GE(maxEmergency.value("fire_towers", 0), 2);
  EXPECT_GE(maxEmergency.value("power_cabinets", 0), 3);
  EXPECT_GE(maxEmergency.value("coverage_rings", 0), 8);
  EXPECT_GE(maxEmergency.value("route_links", 0), 6);
  EXPECT_GE(maxEmergency.value("proxy_beacons", 0), 13);

  ASSERT_TRUE(j.contains("equipment_disassembly"));
  EXPECT_EQ(j["equipment_disassembly"].value("mode", ""), "exploded_pump_skid");
  EXPECT_GE(j["equipment_disassembly"].value("parts", 0), 8);
  EXPECT_GE(j["equipment_disassembly"].value("leader_links", 0), 7);
  EXPECT_EQ(j["equipment_disassembly"].value("inspection_preset", ""), "disassembly");
  EXPECT_EQ(j["equipment_disassembly"].value("detail_mode", ""), "maintenance_sequence_fasteners_material_callouts");
  EXPECT_EQ(j["equipment_disassembly"].value("visual_clearance", ""),
            "suppressed_cloud_fire_gas_geometry_for_part_inspection");
  EXPECT_GE(j["equipment_disassembly"].value("workstations", 0), 8);
  EXPECT_GE(j["equipment_disassembly"].value("sequence_arrows", 0), 7);
  EXPECT_GE(j["equipment_disassembly"].value("fastener_markers", 0), 32);
  EXPECT_GE(j["equipment_disassembly"].value("material_callouts", 0), 4);
  EXPECT_GE(j["equipment_disassembly"].value("ghost_parts", 0), 8);
  EXPECT_GE(j["equipment_disassembly"].value("alignment_axes", 0), 3);
  ASSERT_TRUE(j["equipment_disassembly"].contains("interactive_steps"));
  const auto& disassemblySteps = j["equipment_disassembly"]["interactive_steps"];
  EXPECT_EQ(disassemblySteps.value("cli", ""), "--disassembly-step=all|base|housing|rotor|seal|motor|fasteners");
  ASSERT_TRUE(disassemblySteps.contains("modes"));
  EXPECT_EQ(disassemblySteps["modes"].size(), 7u);
  EXPECT_EQ(disassemblySteps["modes"][0].get<std::string>(), "all");
  EXPECT_EQ(disassemblySteps["modes"][1].get<std::string>(), "base");
  EXPECT_EQ(disassemblySteps["modes"][2].get<std::string>(), "housing");
  EXPECT_EQ(disassemblySteps["modes"][3].get<std::string>(), "rotor");
  EXPECT_EQ(disassemblySteps["modes"][4].get<std::string>(), "seal");
  EXPECT_EQ(disassemblySteps["modes"][5].get<std::string>(), "motor");
  EXPECT_EQ(disassemblySteps["modes"][6].get<std::string>(), "fasteners");
  ASSERT_TRUE(disassemblySteps.contains("hotkeys"));
  EXPECT_EQ(disassemblySteps["hotkeys"].size(), 2u);
  EXPECT_EQ(disassemblySteps["hotkeys"][0].get<std::string>(), "0");
  EXPECT_EQ(disassemblySteps["hotkeys"][1].get<std::string>(), "Tab");
  EXPECT_TRUE(disassemblySteps.value("control_panel_readout", false));
  ASSERT_TRUE(j["equipment_disassembly"].contains("source_equipment_disassembly"));
  const auto& sourceDisassembly = j["equipment_disassembly"]["source_equipment_disassembly"];
  EXPECT_EQ(sourceDisassembly.value("mode", ""), "max_fire_monitor_cabinet_detector_tower_camera_exploded_proxy");
  EXPECT_EQ(sourceDisassembly.value("runtime_mode", ""), "procedural_equipment_part_proxy_until_dcc_export");
  EXPECT_EQ(sourceDisassembly.value("source_asset_count", 0), 5);
  EXPECT_GE(sourceDisassembly.value("source_asset_bytes", 0), 58000000);
  EXPECT_GE(sourceDisassembly.value("fire_monitor_source_bytes", 0), 48600000);
  EXPECT_GE(sourceDisassembly.value("equipment_bays", 0), 5);
  EXPECT_GE(sourceDisassembly.value("exploded_components", 0), 28);
  EXPECT_GE(sourceDisassembly.value("fastener_clusters", 0), 22);
  EXPECT_GE(sourceDisassembly.value("wiring_harnesses", 0), 8);
  EXPECT_GE(sourceDisassembly.value("material_tags", 0), 5);
  EXPECT_GE(sourceDisassembly.value("inspection_axes", 0), 6);
  EXPECT_GE(sourceDisassembly.value("source_beacons", 0), 5);
  EXPECT_GE(sourceDisassembly.value("scene_instances", 0), 84);
  ASSERT_TRUE(sourceDisassembly.contains("source_assets"));
  EXPECT_GE(sourceDisassembly["source_assets"].size(), 5u);

  ASSERT_TRUE(j.contains("material_inspection"));
  const auto& materials = j["material_inspection"];
  EXPECT_EQ(materials.value("preset", ""), "materials");
  EXPECT_EQ(materials.value("mode", ""), "atlas_layers_uv_roughness_normal_wet_corrosion_overlay");
  EXPECT_GE(materials.value("layer_swatches", 0), 8);
  EXPECT_GE(materials.value("channel_strips", 0), 6);
  EXPECT_GE(materials.value("sample_panels", 0), 12);
  EXPECT_GE(materials.value("uv_grid_lines", 0), 16);
  EXPECT_GE(materials.value("roughness_bands", 0), 8);
  EXPECT_GE(materials.value("normal_glyphs", 0), 24);
  EXPECT_GE(materials.value("wetness_patches", 0), 18);
  EXPECT_GE(materials.value("corrosion_patches", 0), 22);
  EXPECT_GE(materials.value("emission_markers", 0), 6);
  EXPECT_GE(materials.value("atlas_tiles", 0), 4);
  EXPECT_GE(materials.value("area_sqm", 0.0), 8000.0);
  EXPECT_EQ(materials.value("runtime_texture_source", ""), "polyhaven_runtime");
  EXPECT_EQ(materials.value("multilayer_channels", ""), "base_metal_roughness_normal_wet_corrosion_emissive");
  ASSERT_TRUE(materials.contains("source_texture_sidecars"));
  const auto& sidecars = materials["source_texture_sidecars"];
  EXPECT_EQ(sidecars.value("mode", ""), "source_texture_sidecar_inventory_material_audit_layer");
  EXPECT_EQ(sidecars.value("runtime_mode", ""), "inventory_proxy_until_channel_renaming_and_runtime_atlas_pack");
  EXPECT_GE(sidecars.value("texture_files", 0), 840);
  EXPECT_GE(sidecars.value("texture_bytes", 0LL), 480000000LL);
  EXPECT_GE(sidecars.value("fbm_directories", 0), 7);
  EXPECT_GE(sidecars.value("fbm_texture_files", 0), 273);
  EXPECT_GE(sidecars.value("loose_and_map_texture_files", 0), 560);
  EXPECT_GE(sidecars.value("jpg_files", 0), 675);
  EXPECT_GE(sidecars.value("png_files", 0), 44);
  EXPECT_GE(sidecars.value("tga_files", 0), 121);
  EXPECT_GE(sidecars.value("semantic_hint_files", 0), 12);
  EXPECT_GE(sidecars.value("preview_swatches", 0), 64);
  EXPECT_GE(sidecars.value("scene_instances", 0), 99);
  ASSERT_TRUE(sidecars.contains("source_packages"));
  EXPECT_GE(sidecars["source_packages"].size(), 7u);
  ASSERT_TRUE(materials.contains("pbr_channel_resolver"));
  const auto& pbrResolver = materials["pbr_channel_resolver"];
  EXPECT_EQ(pbrResolver.value("mode", ""), "filename_semantic_pbr_channel_binding_and_atlas_queue");
  EXPECT_EQ(pbrResolver.value("runtime_mode", ""),
            "materials_preset_source_texture_channel_resolver_until_dcc_slots_export");
  EXPECT_GE(pbrResolver.value("channel_groups", 0), 6);
  EXPECT_GE(pbrResolver.value("semantic_texture_files", 0), 69);
  EXPECT_GE(pbrResolver.value("semantic_texture_bytes", 0LL), 14700000LL);
  EXPECT_GE(pbrResolver.value("needs_material_slot_mapping_files", 0), 771);
  EXPECT_GE(pbrResolver.value("needs_material_slot_mapping_bytes", 0LL), 467000000LL);
  EXPECT_GE(pbrResolver.value("base_color_candidates", 0), 61);
  EXPECT_GE(pbrResolver.value("normal_bump_candidates", 0), 4);
  EXPECT_GE(pbrResolver.value("rough_spec_candidates", 0), 2);
  EXPECT_GE(pbrResolver.value("metal_candidates", 0), 4);
  EXPECT_GE(pbrResolver.value("emissive_light_candidates", 0), 6);
  EXPECT_GE(pbrResolver.value("opacity_alpha_candidates", 0), 1);
  EXPECT_GE(pbrResolver.value("package_links", 0), 7);
  EXPECT_GE(pbrResolver.value("atlas_packing_slots", 0), 6);
  EXPECT_GE(pbrResolver.value("uv_set_remap_rows", 0), 4);
  EXPECT_GE(pbrResolver.value("unmapped_queue_bars", 0), 7);
  EXPECT_GE(pbrResolver.value("scene_instances", 0), 99);
  ASSERT_TRUE(materials.contains("interactive_material_modes"));
  const auto& materialModes = materials["interactive_material_modes"];
  EXPECT_EQ(materialModes.value("cli", ""), "--material-mode=all|layers|uv|surface|degradation|emissive");
  ASSERT_TRUE(materialModes.contains("modes"));
  ASSERT_EQ(materialModes["modes"].size(), 6u);
  EXPECT_EQ(materialModes["modes"][0].get<std::string>(), "all");
  EXPECT_EQ(materialModes["modes"][1].get<std::string>(), "layers");
  EXPECT_EQ(materialModes["modes"][2].get<std::string>(), "uv");
  EXPECT_EQ(materialModes["modes"][3].get<std::string>(), "surface");
  EXPECT_EQ(materialModes["modes"][4].get<std::string>(), "degradation");
  EXPECT_EQ(materialModes["modes"][5].get<std::string>(), "emissive");
  ASSERT_TRUE(materialModes.contains("hotkeys"));
  EXPECT_EQ(materialModes["hotkeys"].size(), 1u);
  EXPECT_EQ(materialModes["hotkeys"][0].get<std::string>(), "M");
  EXPECT_TRUE(materialModes.value("control_panel_readout", false));

  ASSERT_TRUE(j.contains("terrain_model_fusion"));
  const auto& fusion = j["terrain_model_fusion"];
  EXPECT_EQ(fusion.value("preset", ""), "fusion");
  EXPECT_EQ(fusion.value("mode", ""), "terrain_cutfill_foundation_contact_shadow_slope_blend_overlay");
  EXPECT_GE(fusion.value("blend_collars", 0), 10);
  EXPECT_GE(fusion.value("cut_fill_bands", 0), 6);
  EXPECT_GE(fusion.value("foundation_pads", 0), 14);
  EXPECT_GE(fusion.value("contact_shadow_checks", 0), 14);
  EXPECT_GE(fusion.value("grade_sample_posts", 0), 18);
  EXPECT_GE(fusion.value("slope_feather_strips", 0), 24);
  EXPECT_GE(fusion.value("drain_tie_ins", 0), 12);
  EXPECT_GE(fusion.value("road_shoulder_stitches", 0), 16);
  EXPECT_GE(fusion.value("vegetation_masks", 0), 8);
  EXPECT_GE(fusion.value("imported_asset_anchors", 0), 4);
  EXPECT_GE(fusion.value("coverage_area_sqm", 0.0), 90000.0);
  EXPECT_EQ(fusion.value("terrain_source", ""), "binhai_dom_kml_package");
  EXPECT_TRUE(fusion.value("cut_fill_pad", false));
  EXPECT_TRUE(fusion.value("contact_ao", false));
  ASSERT_TRUE(fusion.contains("max_source_proxy_assets"));
  const auto& maxFusion = fusion["max_source_proxy_assets"];
  EXPECT_EQ(maxFusion.value("mode", ""), "terrain_road_landscape_max_source_runtime_fusion_proxies");
  EXPECT_EQ(maxFusion.value("runtime_mode", ""), "procedural_proxy_until_dcc_export_with_dem_dom_alignment");
  EXPECT_GE(maxFusion.value("source_asset_count", 0), 5);
  EXPECT_GE(maxFusion.value("source_asset_bytes", 0LL), 12000000LL);
  EXPECT_GE(maxFusion.value("gravel_shoulders", 0), 18);
  EXPECT_GE(maxFusion.value("paddy_tiles", 0), 12);
  EXPECT_GE(maxFusion.value("lane_strips", 0), 22);
  EXPECT_GE(maxFusion.value("parking_stalls", 0), 12);
  EXPECT_GE(maxFusion.value("seam_feather_bands", 0), 20);
  EXPECT_GE(maxFusion.value("drainage_swales", 0), 8);
  EXPECT_GE(maxFusion.value("grade_tie_samples", 0), 10);
  EXPECT_GE(maxFusion.value("scene_instances", 0), 150);
  ASSERT_TRUE(maxFusion.contains("source_assets"));
  EXPECT_GE(maxFusion["source_assets"].size(), 5u);
}

TEST(ShowcaseScene, CameraClippingFitsHuilianTerrainExtent) {
  auto j = loadSceneJson();

  ASSERT_TRUE(j.contains("camera_clipping"));
  const auto& clipping = j["camera_clipping"];
  EXPECT_EQ(clipping.value("policy", ""), "dynamic_loaded_terrain_bounds_eight_corners_quantized_margin");
  EXPECT_DOUBLE_EQ(clipping.value("legacy_far_clip_m", 0.0), 3600.0);
  EXPECT_GE(clipping.value("minimum_far_margin_m", 0.0), 800.0);
  EXPECT_GE(clipping.value("relative_far_margin", 0.0), 0.18);
  EXPECT_LE(clipping.value("maximum_far_clip_m", 0.0), 250000.0);
  ASSERT_TRUE(clipping.contains("huilian_near_clip_range_m"));
  EXPECT_EQ(clipping["huilian_near_clip_range_m"].size(), 2u);
  EXPECT_GE(clipping["huilian_near_clip_range_m"][0].get<double>(), 0.25);
  EXPECT_LE(clipping["huilian_near_clip_range_m"][1].get<double>(), 4.0);
  ASSERT_TRUE(clipping.contains("huilian_overview_fit"));
  const auto& overview = clipping["huilian_overview_fit"];
  EXPECT_EQ(overview.value("eye_scene_xy_m", std::vector<double>{}), (std::vector<double>{0.0, 0.0}));
  EXPECT_DOUBLE_EQ(overview.value("eye_center_clearance_m", 0.0), 900.0);
  EXPECT_DOUBLE_EQ(overview.value("target_clearance_m", 0.0), 20.0);
  EXPECT_GE(overview.value("vertical_fov_deg", 0.0), 58.0);
  ASSERT_TRUE(overview.contains("minimum_verified_viewport"));
  EXPECT_EQ(overview["minimum_verified_viewport"], nlohmann::json({1417, 970}));
}

TEST(ShowcaseScene, GlobalEarthTransitionsContinuouslyIntoHuilianLocalData) {
  auto j = loadSceneJson();

  ASSERT_TRUE(j.contains("global_local_transition"));
  const auto& transition = j["global_local_transition"];
  EXPECT_EQ(transition.value("preset", ""), "huilian");
  EXPECT_EQ(transition.value("view", ""), "approach");
  EXPECT_EQ(transition.value("coordinate_path", ""), "wgs84_geodetic_to_ecef_double_to_enu_rtc_float");
  EXPECT_DOUBLE_EQ(transition.value("global_altitude_m", 0.0), 12000000.0);
  EXPECT_DOUBLE_EQ(transition.value("local_altitude_m", 0.0), 900.0);
  EXPECT_DOUBLE_EQ(transition.value("duration_seconds", 0.0), 18.0);
  EXPECT_EQ(transition["scale_levels"].size(), 6u);
  EXPECT_EQ(transition["fixed_regression_progress"], nlohmann::json({0.0, 0.3, 0.4, 0.5, 0.65, 1.0}));

  ASSERT_TRUE(transition.contains("global_earth"));
  const auto& earth = transition["global_earth"];
  EXPECT_EQ(earth.value("ellipsoid", ""), "WGS84");
  EXPECT_EQ(earth.value("latitude_segments", 0), 90);
  EXPECT_EQ(earth.value("longitude_segments", 0), 180);
  EXPECT_EQ(earth["imagery"].size(), 2u);
  EXPECT_EQ(earth["source_grid"], nlohmann::json({4096, 4096}));
  EXPECT_EQ(earth["runtime_grid"], nlohmann::json({1024, 1024}));
  ASSERT_TRUE(earth.contains("regional_imagery_lod"));
  const auto& regional = earth["regional_imagery_lod"];
  EXPECT_EQ(regional.value("source_root", ""), "assets/tiles/tianditu_img");
  EXPECT_EQ(regional.value("scheme", ""), "WebMercatorQuad_XYZ_EPSG3857");
  EXPECT_EQ(regional["coarse_parent_tile"], nlohmann::json({3, 6, 3}));
  EXPECT_EQ(regional["detail_child_tiles"], nlohmann::json({{4, 12, 6}, {4, 13, 6}, {4, 12, 7}, {4, 13, 7}}));
  EXPECT_EQ(regional["source_roots"], nlohmann::json({"assets/tiles/tianditu_img", "assets/tiles/esri_huilian"}));
  EXPECT_EQ(regional["context_tile_origin_grid"], nlohmann::json({8, 205, 107, 8, 8}));
  EXPECT_EQ(regional["near_tile_origin_grid"], nlohmann::json({10, 833, 441, 8, 8}));
  EXPECT_EQ(regional["tile_grid_px"], nlohmann::json({256, 256}));
  EXPECT_EQ(regional["coarse_runtime_grid"], nlohmann::json({256, 256}));
  EXPECT_EQ(regional["detail_runtime_grid"], nlohmann::json({512, 512}));
  EXPECT_EQ(regional["context_runtime_grid"], nlohmann::json({2048, 2048}));
  EXPECT_EQ(regional["near_runtime_grid"], nlohmann::json({2048, 2048}));
  EXPECT_EQ(regional["loaded_tile_counts"], nlohmann::json({1, 4, 64, 64}));
  EXPECT_EQ(regional.value("manifest_tile_count", 0), 128);
  EXPECT_EQ(regional.value("manifest_tile_bytes", 0), 1699537);
  EXPECT_EQ(regional.value("manifest_sha256", ""), "e5e99434ea1e35ac156d34aa7de1e1febbd4bd61fe72dca76c15430431a78a28");
  EXPECT_EQ(regional["coverage_altitude_range_m"], nlohmann::json({5000000.0, 2000000.0}));
  EXPECT_EQ(regional["detail_altitude_range_m"], nlohmann::json({1600000.0, 400000.0}));
  EXPECT_EQ(regional["context_altitude_range_m"], nlohmann::json({1000000.0, 250000.0}));
  EXPECT_EQ(regional["near_altitude_range_m"], nlohmann::json({200000.0, 50000.0}));
  EXPECT_EQ(regional["attributions"],
            nlohmann::json({"assets/tiles/tianditu_img/SOURCE_ATTRIBUTION.md",
                            "assets/tiles/esri_huilian/SOURCE_ATTRIBUTION.md"}));

  ASSERT_TRUE(transition.contains("local_handoff"));
  const auto& handoff = transition["local_handoff"];
  EXPECT_EQ(handoff.value("terrain", ""), "assets/showcase/runtime/huilian_dem_30m.bin");
  EXPECT_EQ(handoff.value("dom", ""), "assets/showcase/runtime/huilian_dom_gpu_manifest.json");
  EXPECT_EQ(handoff.value("dlg", ""), "data/showcase/huilian_dlg_osm.geojson");
  EXPECT_EQ(handoff.value("interactive_replay_hotkey", ""), "G");

  const auto& globalLocalClipping = j["camera_clipping"]["global_local_policy"];
  EXPECT_EQ(globalLocalClipping.value("mode", ""), "wgs84_visible_horizon_to_local_terrain_adaptive");
  EXPECT_DOUBLE_EQ(globalLocalClipping.value("horizon_margin", 0.0), 1.12);
  EXPECT_DOUBLE_EQ(globalLocalClipping.value("local_far_clip_m", 0.0), 20000.0);
}
