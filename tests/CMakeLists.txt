enable_testing()
include(GoogleTest)

# Test executable
add_executable(ht_tests
  src/asset/test_shader_compiler.cpp
  src/scene/test_landscape_document.cpp
  src/core/test_logger.cpp
  src/core/test_memory.cpp
  src/core/test_types.cpp
  src/core/test_math.cpp
  src/core/test_http_client.cpp
  src/rhi/test_vulkan_device.cpp
  src/rhi/test_descriptor_lifetime.cpp
  src/rhi/test_buffer.cpp
  src/rhi/test_texture.cpp
  src/rhi/test_texture_upload_layout.cpp
  src/measure/test_measurement.cpp
  src/renderer/test_ancestor_fallback.cpp
  src/renderer/test_material.cpp
  src/renderer/test_camera.cpp
  src/renderer/test_lod.cpp
  src/renderer/test_dom_view_lod.cpp
  src/renderer/test_native_geometry_lod.cpp
  src/renderer/test_native_source_frontier.cpp
  src/renderer/test_ssao_kernel.cpp
  src/renderer/test_instance_buffer.cpp
  src/renderer/test_postprocess.cpp
  src/renderer/test_ssr.cpp
  src/renderer/test_probe_gi.cpp
  src/renderer/test_hdr.cpp
  src/renderer/test_render_profiler.cpp
  src/renderer/test_camera_base.cpp
  src/renderer/test_local_camera.cpp
  src/renderer/test_globe_camera.cpp
  src/renderer/test_globe_renderer.cpp
  src/renderer/test_regional_imagery_lod.cpp
  src/renderer/test_water_surface.cpp
  src/renderer/test_vegetation.cpp
  src/renderer/test_index_buffer_table.cpp
  src/renderer/test_tile_gpu_cache.cpp
  src/renderer/test_rgba8_mip_chain.cpp
  src/renderer/test_polyline_renderer.cpp
  src/renderer/test_billboard_renderer.cpp
  src/renderer/test_polygon_renderer.cpp
  src/renderer/test_skeleton.cpp
  src/renderer/test_animation_sampler.cpp
  src/renderer/test_skinned_pbr_layout.cpp
  src/renderer/test_picking.cpp
  src/renderer/test_triangle_bvh.cpp
  src/renderer/test_geo_gizmo.cpp
  src/renderer/test_geo_rtc_projection.cpp
  src/renderer/test_path_ribbon.cpp
  src/renderer/test_geo_render_semantics.cpp
  src/renderer/test_hdr_compose_offscreen.cpp
  src/renderer/test_instanced_pbr_layout.cpp
  src/renderer/test_lod_partition_integration.cpp
  src/renderer/test_particle_system.cpp
  src/renderer/test_environment_settings.cpp
  src/renderer/test_apply_environment.cpp
  src/renderer/test_showcase_scene.cpp
  src/scene/test_entity.cpp
  src/scene/test_local_entity.cpp
  src/scene/test_local_polyline.cpp
  src/scene/test_geo_polyline.cpp
  src/scene/test_local_billboard.cpp
  src/scene/test_geo_billboard.cpp
  src/scene/test_local_emitter.cpp
  src/scene/test_geo_emitter.cpp
  src/scene/test_local_volumetric.cpp
  src/scene/test_geo_volumetric.cpp
  src/scene/test_shockwave_entity.cpp
  src/scene/test_picking_service.cpp
  src/scene/test_polyline_annotation.cpp
  src/scene/test_polygon_annotation.cpp
  src/scene/test_symbol_annotation.cpp
  src/scene/test_text_annotation.cpp
  src/scene/test_arrow_annotation.cpp
  src/scene/test_annotation_manager.cpp
  src/scene/test_command_stack.cpp
  src/scene/test_drawing_tool.cpp
  src/scene/test_track.cpp
  src/scene/test_timeline.cpp
  src/scene/test_local_agent.cpp
  src/scene/test_geo_agent.cpp
  src/scene/test_drill_runtime.cpp
  src/scene/test_drill_log.cpp
  src/scene/test_crowd_simulator.cpp
  src/scene/test_scene_graph.cpp
  src/scene/test_scene_loader.cpp
  src/scene/test_component.cpp
  src/scene/test_ecs.cpp
  src/asset/test_asset_manager.cpp
  src/asset/test_gltf_loader.cpp
  src/asset/test_indexed_mesh_compaction.cpp
  src/asset/test_gltf_node_transforms.cpp
  src/renderer/test_time_of_day.cpp
  src/renderer/test_point_light.cpp
  src/asset/test_gltf_skin.cpp
  src/asset/test_gltf_skinned_character.cpp
  src/window/test_window.cpp
  src/window/test_input.cpp
  src/window/test_sdl_event_pump.cpp
  src/gis/test_gis.cpp
  src/gis/test_tile_coord.cpp
  src/gis/test_tile_cache.cpp
  src/gis/test_routed_imagery_provider.cpp
  src/gis/test_obb.cpp
  src/gis/test_tile_quad_tree_selector.cpp
  src/gis/test_tile_geometry_cache.cpp
  src/gis/test_dem_baker.cpp
  src/gis/test_3dtiles.cpp
  src/gis/test_esri_provider.cpp
  src/streaming/test_world_partition.cpp
  src/streaming/test_culling.cpp
  src/streaming/test_terrain.cpp
  src/coordinate/test_rtc.cpp
  src/coordinate/test_precision_origin.cpp
  src/coordinate/test_scale_switch.cpp
  src/coordinate/test_global_local_transition.cpp
  src/cloud/test_cloud.cpp
  src/cloud/test_network_event_pump.cpp
  src/cloud/test_vt_encoder.cpp
  src/coordinate/test_geodetic.cpp
  src/coordinate/test_enu_frame.cpp
  src/coordinate/test_projection.cpp
  src/coordinate/test_datum_transform.cpp
  src/coordinate/test_anchor_manager.cpp
  src/coordinate/test_gaussian_plume.cpp
)

set_target_properties(ht_tests PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

# Link engine libraries and testing framework
target_link_libraries(ht_tests
  PRIVATE
    ht_core
    ht_rhi
    ht_renderer
    ht_scene
    ht_asset
    ht_window
    ht_coordinate
    ht_measure
    ht_gis
    ht_cloud
    GTest::gtest
    GTest::gtest_main
    GTest::gmock
)

# Include directories
target_include_directories(ht_tests
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Compiler flags
if(MSVC)
  target_compile_options(ht_tests PRIVATE /W4)
else()
  target_compile_options(ht_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Tests can reach the source-tree-root for committed fixture files
# (data/scenes/*.scene.json etc).
target_compile_definitions(ht_tests PRIVATE
  HT_ENGINE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# Register tests
gtest_discover_tests(ht_tests
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  PROPERTIES
    TIMEOUT 10
)

# Coverage support (for Debug builds)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  if(NOT MSVC)
    target_compile_options(ht_tests PRIVATE --coverage)
    target_link_libraries(ht_tests PRIVATE --coverage)
  endif()
endif()

message(STATUS "ht_tests configured")
message(STATUS "Run tests with: ctest --output-on-failure")

# The OSGB converter must survive workspace moves without replacing real data.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(NAME ZhumadianSourceAliasMigration
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/../tools/test_zhumadian_source_alias.py)
endif()
