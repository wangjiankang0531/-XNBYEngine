set(SHOWCASE_DEMO_SOURCES main.cpp)
if(WIN32)
  list(APPEND SHOWCASE_DEMO_SOURCES showcase_demo.rc)
endif()

add_executable(showcase_demo ${SHOWCASE_DEMO_SOURCES})

target_link_libraries(showcase_demo
  PRIVATE
    ht_core
    ht_rhi
    ht_asset
    ht_gis
    ht_measure
    ht_renderer
    glm::glm
    nlohmann_json::nlohmann_json
    stb
)

target_compile_definitions(showcase_demo PRIVATE
  HT_SOURCE_ROOT="${CMAKE_SOURCE_DIR}"
)

if(WIN32)
  target_link_libraries(showcase_demo PRIVATE user32 comctl32 gdi32 shell32)
  target_compile_definitions(showcase_demo PRIVATE
    VK_USE_PLATFORM_WIN32_KHR
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
endif()

set_target_properties(showcase_demo PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

if(MSVC)
  target_compile_options(showcase_demo PRIVATE /W4 /utf-8)
  target_link_options(showcase_demo PRIVATE
    "/MANIFEST:EMBED"
    "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/showcase_demo.manifest"
  )
else()
  target_compile_options(showcase_demo PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

message(STATUS "showcase_demo configured")

option(HT_BUILD_LEGACY_LANDSCAPE_EDITOR "Build the archived demo-derived landscape editor" OFF)
if(WIN32 AND HT_BUILD_LEGACY_LANDSCAPE_EDITOR)
  add_executable(ht_landscape_editor ${SHOWCASE_DEMO_SOURCES})
  target_link_libraries(ht_landscape_editor PRIVATE ht_core ht_rhi ht_asset ht_gis ht_measure ht_renderer
    glm::glm nlohmann_json::nlohmann_json stb user32 comctl32 comdlg32 gdi32 shell32)
  target_compile_definitions(ht_landscape_editor PRIVATE HT_LANDSCAPE_EDITOR VK_USE_PLATFORM_WIN32_KHR
    WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_features(ht_landscape_editor PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(ht_landscape_editor PRIVATE /W4 /utf-8)
    target_link_options(ht_landscape_editor PRIVATE "/MANIFEST:EMBED"
      "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/showcase_demo.manifest")
  endif()
endif()

if(HT_BUILD_RENDER_REGRESSION_TESTS)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME ShowcaseDemo_ZhumadianNativeFrontierUnit
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/test_zhumadian_native_frontier.py)
    add_test(NAME ShowcaseDemo_ZhumadianNativeHierarchy
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build_zhumadian_native_hierarchy.py)
    add_test(NAME ShowcaseDemo_ZhumadianNativeRender
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_native_lod.py
        --demo=$<TARGET_FILE:showcase_demo> --output-dir=${CMAKE_CURRENT_BINARY_DIR}/native_lod)
    add_test(NAME ShowcaseDemo_ZhumadianNativeTravel
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_native_lod.py
        --demo=$<TARGET_FILE:showcase_demo> --output-dir=${CMAKE_CURRENT_BINARY_DIR}/native_lod --travel)
    set_tests_properties(ShowcaseDemo_ZhumadianNativeFrontierUnit ShowcaseDemo_ZhumadianNativeHierarchy
      ShowcaseDemo_ZhumadianNativeRender ShowcaseDemo_ZhumadianNativeTravel
      PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600)
    add_test(NAME ShowcaseDemo_HuilianDomViewLodRender
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_huilian_dom_view_lod.py
        --demo=$<TARGET_FILE:showcase_demo> --output-dir=${CMAKE_BINARY_DIR}/huilian_dom_view_lod)
    add_test(NAME ShowcaseDemo_HuilianDomViewLodTravel
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_huilian_dom_view_lod.py --travel
        --demo=$<TARGET_FILE:showcase_demo> --output-dir=${CMAKE_BINARY_DIR}/huilian_dom_view_lod)
    set_tests_properties(ShowcaseDemo_HuilianDomViewLodRender ShowcaseDemo_HuilianDomViewLodTravel
      PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180)
    add_test(NAME ShowcaseDemo_MeshSunShadows
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_mesh_sun_shadows.py
        --demo=$<TARGET_FILE:showcase_demo> --output-dir=${CMAKE_BINARY_DIR}/mesh_sun_shadows)
    set_tests_properties(ShowcaseDemo_MeshSunShadows PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180)
    if(WIN32)
      add_test(NAME ShowcaseDemo_MeshSunShadowsInteractive
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/run_showcase_demo.py
          --launcher-demo=$<TARGET_FILE:showcase_demo> --preset=huilian --huilian-view=valve
          --mesh-sun-shadows --frames=30 --width=1440 --height=900)
      set_tests_properties(ShowcaseDemo_MeshSunShadowsInteractive
        PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60)
    endif()
    add_test(NAME ShowcaseDemo_ZhumadianContextFusionAudit
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build_zhumadian_context_fusion.py)
    add_test(NAME ShowcaseDemo_ZhumadianContextFusionRender
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_context_fusion.py
        --demo=$<TARGET_FILE:showcase_demo> --output-dir=${CMAKE_BINARY_DIR}/zhumadian_context_fusion)
    add_test(NAME ShowcaseDemo_ZhumadianContextFusionTravel
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_geometry_travel.py
        --context --context-color-matched --context-fused
        --demo=$<TARGET_FILE:showcase_demo> --output=${CMAKE_BINARY_DIR}/zhumadian_context_fusion/travel.json)
    set_tests_properties(ShowcaseDemo_ZhumadianContextFusionAudit ShowcaseDemo_ZhumadianContextFusionRender
      ShowcaseDemo_ZhumadianContextFusionTravel PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300)
    add_test(NAME ShowcaseDemo_ZhumadianContextColorAudit
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build_zhumadian_context_color.py)
    add_test(NAME ShowcaseDemo_ZhumadianContextColorRender
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_context_color.py
        --demo=$<TARGET_FILE:showcase_demo> --output-dir=${CMAKE_BINARY_DIR}/zhumadian_context_color)
    add_test(NAME ShowcaseDemo_ZhumadianContextColorTravel
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_geometry_travel.py --context --context-color-matched
        --demo=$<TARGET_FILE:showcase_demo> --output=${CMAKE_BINARY_DIR}/zhumadian_context_color/travel.json)
    set_tests_properties(ShowcaseDemo_ZhumadianContextColorAudit ShowcaseDemo_ZhumadianContextColorRender
      ShowcaseDemo_ZhumadianContextColorTravel PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 150)
    add_test(NAME ShowcaseDemo_ZhumadianContextAudit
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build_zhumadian_context.py)
    add_test(NAME ShowcaseDemo_ZhumadianContextRender
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_context.py
        --demo=$<TARGET_FILE:showcase_demo> --output-dir=${CMAKE_BINARY_DIR}/zhumadian_context)
    add_test(NAME ShowcaseDemo_ZhumadianContextTravel
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_geometry_travel.py --context
        --demo=$<TARGET_FILE:showcase_demo> --output=${CMAKE_BINARY_DIR}/zhumadian_context/travel.json)
    set_tests_properties(ShowcaseDemo_ZhumadianContextAudit ShowcaseDemo_ZhumadianContextRender
      ShowcaseDemo_ZhumadianContextTravel PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 150)
    add_test(NAME ShowcaseDemo_ZhumadianGeometryChunkAudit
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build_zhumadian_geometry_chunks.py)
    add_test(NAME ShowcaseDemo_ZhumadianGeometryStreaming
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_geometry_streaming.py
        --demo=$<TARGET_FILE:showcase_demo> --output-dir=${CMAKE_BINARY_DIR}/zhumadian_streaming)
    set_tests_properties(ShowcaseDemo_ZhumadianGeometryChunkAudit ShowcaseDemo_ZhumadianGeometryStreaming PROPERTIES
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 150)
    add_test(NAME ShowcaseDemo_ZhumadianGeometryTravel
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_geometry_travel.py
        --demo=$<TARGET_FILE:showcase_demo> --output=${CMAKE_BINARY_DIR}/zhumadian_streaming/travel.json)
    set_tests_properties(ShowcaseDemo_ZhumadianGeometryTravel PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 100)
    add_test(NAME ShowcaseDemo_ZhumadianGeometryCompaction
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_geometry_compaction.py
        --demo=$<TARGET_FILE:showcase_demo> --output-dir=${CMAKE_BINARY_DIR}/zhumadian_compaction)
    set_tests_properties(ShowcaseDemo_ZhumadianGeometryCompaction PROPERTIES
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180)
    add_test(NAME ShowcaseDemo_ZhumadianForegroundAudit
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build_zhumadian_geometry_detail.py --check --profile=foreground)
    add_test(NAME ShowcaseDemo_ZhumadianForegroundRender
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_geometry_detail.py --profile=foreground
        --demo=$<TARGET_FILE:showcase_demo> --output-dir=${CMAKE_BINARY_DIR}/zhumadian_foreground)
    set_tests_properties(ShowcaseDemo_ZhumadianForegroundAudit ShowcaseDemo_ZhumadianForegroundRender
      PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 180)
    add_test(NAME ShowcaseDemo_ZhumadianGeometryDetailUnit
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_zhumadian_geometry_detail.py)
    add_test(NAME ShowcaseDemo_ZhumadianGeometryDetailAudit
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build_zhumadian_geometry_detail.py --check)
    add_test(NAME ShowcaseDemo_ZhumadianGeometryDetailRender
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_geometry_detail.py
        --demo=$<TARGET_FILE:showcase_demo> --output-dir=${CMAKE_BINARY_DIR}/zhumadian_geometry_detail)
    set_tests_properties(ShowcaseDemo_ZhumadianGeometryDetailUnit ShowcaseDemo_ZhumadianGeometryDetailAudit
      ShowcaseDemo_ZhumadianGeometryDetailRender PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    add_test(NAME ShowcaseDemo_ZhumadianVisibilityFade
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_visibility_fade.py
        --demo=$<TARGET_FILE:showcase_demo> --output-dir=${CMAKE_BINARY_DIR}/zhumadian_visibility_fade)
    set_tests_properties(ShowcaseDemo_ZhumadianVisibilityFade PROPERTIES
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    add_test(NAME ShowcaseDemo_ZhumadianTexturePageAudit
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build_zhumadian_texture_pages.py)
    set_tests_properties(ShowcaseDemo_ZhumadianTexturePageAudit PROPERTIES
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 60)
    add_test(NAME ShowcaseDemo_ZhumadianTexturePaging
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_texture_paging.py
        --demo=$<TARGET_FILE:showcase_demo> --output-dir=${CMAKE_BINARY_DIR}/zhumadian_texture_paging)
    set_tests_properties(ShowcaseDemo_ZhumadianTexturePaging PROPERTIES
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    add_test(NAME ShowcaseDemo_OutputEncodingParity
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_showcase_output_encoding.py
        --demo=$<TARGET_FILE:showcase_demo> --output-dir=${CMAKE_BINARY_DIR}/output_encoding)
    set_tests_properties(ShowcaseDemo_OutputEncodingParity PROPERTIES
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)

    add_test(NAME ShowcaseDemo_ZhumadianFullPlant
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_full_stage.py
        --demo=$<TARGET_FILE:showcase_demo> --view=plant
        --output=${CMAKE_BINARY_DIR}/showcase_zhumadian_full_plant.png
        --stats-output=${CMAKE_BINARY_DIR}/showcase_zhumadian_full_plant.stats.json)
    set_tests_properties(ShowcaseDemo_ZhumadianFullPlant PROPERTIES
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)

    add_test(
      NAME ShowcaseDemo_XinengModelAssetAudit
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/audit_xineng_model_assets.py
        --check
        --write-json=${CMAKE_BINARY_DIR}/xineng_model_asset_audit.json
        --write-markdown=${CMAKE_BINARY_DIR}/xineng_model_asset_audit.md
    )
    set_tests_properties(ShowcaseDemo_XinengModelAssetAudit
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 30
    )

    add_test(
      NAME ShowcaseDemo_HuilianAssetAudit
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/audit_huilian_assets.py
        --check
        --write-json=${CMAKE_BINARY_DIR}/huilian_asset_audit.json
        --write-markdown=${CMAKE_BINARY_DIR}/huilian_asset_inventory.md
    )
    set_tests_properties(ShowcaseDemo_HuilianAssetAudit
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 60
    )

    add_test(
      NAME ShowcaseDemo_HuilianDlgSourceAudit
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/fetch_huilian_dlg.py
        --check
    )
    set_tests_properties(ShowcaseDemo_HuilianDlgSourceAudit
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 30
    )

    foreach(preset overview terrain fusion gis models huilian materials process disassembly lighting particles weather safety sprinkler routes warehouse drill performance)
      add_test(
        NAME ShowcaseDemo_${preset}
        COMMAND
          ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
          --demo=$<TARGET_FILE:showcase_demo>
          --preset=${preset}
          --output=${CMAKE_BINARY_DIR}/showcase_${preset}.png
          --stats-output=${CMAKE_BINARY_DIR}/showcase_${preset}.stats.json
          --width=1280
          --height=720
          --frames=30
          --cwd=${CMAKE_SOURCE_DIR}
      )
      set_tests_properties(ShowcaseDemo_${preset}
        PROPERTIES
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          TIMEOUT 90
      )
    endforeach()

    foreach(huilian_view southwest overview dlg)
      add_test(
        NAME ShowcaseDemo_HuilianView_${huilian_view}
        COMMAND
          ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
          --demo=$<TARGET_FILE:showcase_demo>
          --preset=huilian
          --huilian-view=${huilian_view}
          --output=${CMAKE_BINARY_DIR}/showcase_huilian_${huilian_view}.png
          --stats-output=${CMAKE_BINARY_DIR}/showcase_huilian_${huilian_view}.stats.json
          --width=1280
          --height=720
          --frames=30
          --cwd=${CMAKE_SOURCE_DIR}
      )
      set_tests_properties(ShowcaseDemo_HuilianView_${huilian_view}
        PROPERTIES
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          TIMEOUT 90
      )
    endforeach()

    # Zhumadian is an independent 18-root ContextCapture OSGB stage ROI, not
    # the complete industrial-park assembly expected by verify_showcase_demo.
    foreach(zhumadian_view overview oblique close)
      add_test(
        NAME ShowcaseDemo_ZhumadianStage_${zhumadian_view}
        COMMAND
          ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_stage.py
          --demo=$<TARGET_FILE:showcase_demo>
          --view=${zhumadian_view}
          --output=${CMAKE_BINARY_DIR}/showcase_zhumadian_${zhumadian_view}.png
          --stats-output=${CMAKE_BINARY_DIR}/showcase_zhumadian_${zhumadian_view}.stats.json
          --width=1280
          --height=720
          --frames=30
          --cwd=${CMAKE_SOURCE_DIR}
      )
      set_tests_properties(ShowcaseDemo_ZhumadianStage_${zhumadian_view}
        PROPERTIES
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          TIMEOUT 120
      )
    endforeach()

    add_test(NAME ShowcaseDemo_ZhumadianChild_AssetAudit
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/build_zhumadian_child_stage.py --check)
    add_test(NAME ShowcaseDemo_ZhumadianChild_SelectionContract
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_zhumadian_child_stage.py)
    foreach(zhumadian_view overview oblique close)
      add_test(NAME ShowcaseDemo_ZhumadianChild_${zhumadian_view}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_stage.py
          --demo=$<TARGET_FILE:showcase_demo> --view=${zhumadian_view} --detail=child1
          --output=${CMAKE_BINARY_DIR}/showcase_zhumadian_child_${zhumadian_view}.png
          --stats-output=${CMAKE_BINARY_DIR}/showcase_zhumadian_child_${zhumadian_view}.stats.json
          --width=1280 --height=720 --frames=30 --cwd=${CMAKE_SOURCE_DIR})
      set_tests_properties(ShowcaseDemo_ZhumadianChild_${zhumadian_view}
        PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    endforeach()

    add_test(NAME ShowcaseDemo_ZhumadianFull_Contract
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_zhumadian_full_stage.py)
    add_test(NAME ShowcaseDemo_LauncherOutputs
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/test_showcase_launcher_outputs.py)
    foreach(zhumadian_view overview oblique close)
      add_test(NAME ShowcaseDemo_ZhumadianFull_${zhumadian_view}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/verify_zhumadian_full_stage.py
          --demo=$<TARGET_FILE:showcase_demo> --view=${zhumadian_view}
          --output=${CMAKE_BINARY_DIR}/showcase_zhumadian_full_${zhumadian_view}.png
          --stats-output=${CMAKE_BINARY_DIR}/showcase_zhumadian_full_${zhumadian_view}.stats.json)
      set_tests_properties(ShowcaseDemo_ZhumadianFull_${zhumadian_view}
        PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 120)
    endforeach()

    foreach(huilian_approach_progress 0.0 0.3 0.4 0.5 0.65 1.0)
      string(REPLACE "." "_" huilian_approach_suffix "${huilian_approach_progress}")
      add_test(
        NAME ShowcaseDemo_HuilianApproach_${huilian_approach_suffix}
        COMMAND
          ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
          --demo=$<TARGET_FILE:showcase_demo>
          --preset=huilian
          --huilian-view=approach
          --huilian-approach-progress=${huilian_approach_progress}
          --output=${CMAKE_BINARY_DIR}/showcase_huilian_approach_${huilian_approach_suffix}.png
          --stats-output=${CMAKE_BINARY_DIR}/showcase_huilian_approach_${huilian_approach_suffix}.stats.json
          --width=1280
          --height=720
          --frames=30
          --cwd=${CMAKE_SOURCE_DIR}
      )
      set_tests_properties(ShowcaseDemo_HuilianApproach_${huilian_approach_suffix}
        PROPERTIES
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          TIMEOUT 120
      )
    endforeach()

    add_test(
      NAME ShowcaseDemo_HuilianRegionalImageryAudit
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/fetch_huilian_regional_imagery.py
        --check
    )
    set_tests_properties(ShowcaseDemo_HuilianRegionalImageryAudit
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 60
    )

    add_test(
      NAME ShowcaseDemo_HuilianWideFrustum
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
        --demo=$<TARGET_FILE:showcase_demo>
        --preset=huilian
        --huilian-view=overview
        --output=${CMAKE_BINARY_DIR}/showcase_huilian_wide_frustum.png
        --stats-output=${CMAKE_BINARY_DIR}/showcase_huilian_wide_frustum.stats.json
        --width=1417
        --height=970
        --frames=30
        --cwd=${CMAKE_SOURCE_DIR}
    )
    set_tests_properties(ShowcaseDemo_HuilianWideFrustum
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 90
    )

    add_test(
      NAME ShowcaseDemo_GisMeasureVolume
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
        --demo=$<TARGET_FILE:showcase_demo>
        --preset=gis
        --output=${CMAKE_BINARY_DIR}/showcase_gis_measure_volume.png
        --stats-output=${CMAKE_BINARY_DIR}/showcase_gis_measure_volume.stats.json
        --width=1280
        --height=720
        --frames=30
        --cwd=${CMAKE_SOURCE_DIR}
        --gis-measure=volume
    )
    set_tests_properties(ShowcaseDemo_GisMeasureVolume
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 90
    )

    add_test(
      NAME ShowcaseDemo_MaxCatalogModeSafety
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
        --demo=$<TARGET_FILE:showcase_demo>
        --preset=models
        --output=${CMAKE_BINARY_DIR}/showcase_max_catalog_mode_safety.png
        --stats-output=${CMAKE_BINARY_DIR}/showcase_max_catalog_mode_safety.stats.json
        --width=1280
        --height=720
        --frames=30
        --cwd=${CMAKE_SOURCE_DIR}
        --max-catalog-mode=safety
    )
    set_tests_properties(ShowcaseDemo_MaxCatalogModeSafety
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 90
    )

    add_test(
      NAME ShowcaseDemo_MaxCatalogModePower
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
        --demo=$<TARGET_FILE:showcase_demo>
        --preset=models
        --output=${CMAKE_BINARY_DIR}/showcase_max_catalog_mode_power.png
        --stats-output=${CMAKE_BINARY_DIR}/showcase_max_catalog_mode_power.stats.json
        --width=1280
        --height=720
        --frames=30
        --cwd=${CMAKE_SOURCE_DIR}
        --max-catalog-mode=power
    )
    set_tests_properties(ShowcaseDemo_MaxCatalogModePower
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 90
    )

    add_test(
      NAME ShowcaseDemo_GisMeasureDistance
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
        --demo=$<TARGET_FILE:showcase_demo>
        --preset=gis
        --output=${CMAKE_BINARY_DIR}/showcase_gis_measure_distance.png
        --stats-output=${CMAKE_BINARY_DIR}/showcase_gis_measure_distance.stats.json
        --width=1280
        --height=720
        --frames=30
        --cwd=${CMAKE_SOURCE_DIR}
        --gis-measure=distance
    )
    set_tests_properties(ShowcaseDemo_GisMeasureDistance
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 90
    )

    add_test(
      NAME ShowcaseDemo_RouteModeEvacuation
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
        --demo=$<TARGET_FILE:showcase_demo>
        --preset=routes
        --output=${CMAKE_BINARY_DIR}/showcase_route_mode_evacuation.png
        --stats-output=${CMAKE_BINARY_DIR}/showcase_route_mode_evacuation.stats.json
        --width=1280
        --height=720
        --frames=30
        --cwd=${CMAKE_SOURCE_DIR}
        --route-mode=evacuation
    )
    set_tests_properties(ShowcaseDemo_RouteModeEvacuation
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 90
    )

    add_test(
      NAME ShowcaseDemo_ProcessModeIsolation
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
        --demo=$<TARGET_FILE:showcase_demo>
        --preset=process
        --output=${CMAKE_BINARY_DIR}/showcase_process_mode_isolation.png
        --stats-output=${CMAKE_BINARY_DIR}/showcase_process_mode_isolation.stats.json
        --width=1280
        --height=720
        --frames=30
        --cwd=${CMAKE_SOURCE_DIR}
        --process-mode=isolation
    )
    set_tests_properties(ShowcaseDemo_ProcessModeIsolation
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 90
    )

    add_test(
      NAME ShowcaseDemo_SprinklerModeMonitors
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
        --demo=$<TARGET_FILE:showcase_demo>
        --preset=sprinkler
        --output=${CMAKE_BINARY_DIR}/showcase_sprinkler_mode_monitors.png
        --stats-output=${CMAKE_BINARY_DIR}/showcase_sprinkler_mode_monitors.stats.json
        --width=1280
        --height=720
        --frames=30
        --cwd=${CMAKE_SOURCE_DIR}
        --sprinkler-mode=monitors
    )
    set_tests_properties(ShowcaseDemo_SprinklerModeMonitors
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 90
    )

    add_test(
      NAME ShowcaseDemo_FountainModeWind
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
        --demo=$<TARGET_FILE:showcase_demo>
        --preset=weather
        --output=${CMAKE_BINARY_DIR}/showcase_fountain_mode_wind.png
        --stats-output=${CMAKE_BINARY_DIR}/showcase_fountain_mode_wind.stats.json
        --width=1280
        --height=720
        --frames=30
        --cwd=${CMAKE_SOURCE_DIR}
        --fountain-mode=wind
    )
    set_tests_properties(ShowcaseDemo_FountainModeWind
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 90
    )

    add_test(
      NAME ShowcaseDemo_GasModeSensors
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
        --demo=$<TARGET_FILE:showcase_demo>
        --preset=particles
        --output=${CMAKE_BINARY_DIR}/showcase_gas_mode_sensors.png
        --stats-output=${CMAKE_BINARY_DIR}/showcase_gas_mode_sensors.stats.json
        --width=1280
        --height=720
        --frames=30
        --cwd=${CMAKE_SOURCE_DIR}
        --gas-mode=sensors
    )
    set_tests_properties(ShowcaseDemo_GasModeSensors
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 90
    )

    add_test(
      NAME ShowcaseDemo_GasWindFieldOverride
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
        --demo=$<TARGET_FILE:showcase_demo>
        --preset=particles
        --output=${CMAKE_BINARY_DIR}/showcase_gas_wind_field_override.png
        --stats-output=${CMAKE_BINARY_DIR}/showcase_gas_wind_field_override.stats.json
        --width=1280
        --height=720
        --frames=30
        --cwd=${CMAKE_SOURCE_DIR}
        --gas-mode=footprint
        --expected-wind-dir=132.0
        --expected-wind-speed=8.5
    )
    set_tests_properties(ShowcaseDemo_GasWindFieldOverride
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 90
    )

    add_test(
      NAME ShowcaseDemo_DisassemblyStepFasteners
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
        --demo=$<TARGET_FILE:showcase_demo>
        --preset=disassembly
        --output=${CMAKE_BINARY_DIR}/showcase_disassembly_step_fasteners.png
        --stats-output=${CMAKE_BINARY_DIR}/showcase_disassembly_step_fasteners.stats.json
        --width=1280
        --height=720
        --frames=30
        --cwd=${CMAKE_SOURCE_DIR}
        --disassembly-step=fasteners
    )
    set_tests_properties(ShowcaseDemo_DisassemblyStepFasteners
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 90
    )

    add_test(
      NAME ShowcaseDemo_MaterialModeDegradation
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
        --demo=$<TARGET_FILE:showcase_demo>
        --preset=materials
        --output=${CMAKE_BINARY_DIR}/showcase_material_mode_degradation.png
        --stats-output=${CMAKE_BINARY_DIR}/showcase_material_mode_degradation.stats.json
        --width=1280
        --height=720
        --frames=30
        --cwd=${CMAKE_SOURCE_DIR}
        --material-mode=degradation
    )
    set_tests_properties(ShowcaseDemo_MaterialModeDegradation
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 90
    )

    add_test(
      NAME ShowcaseDemo_ControlOverrides
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
        --demo=$<TARGET_FILE:showcase_demo>
        --preset=particles
        --output=${CMAKE_BINARY_DIR}/showcase_control_overrides.png
        --stats-output=${CMAKE_BINARY_DIR}/showcase_control_overrides.stats.json
        --width=1280
        --height=720
        --frames=45
        --cwd=${CMAKE_SOURCE_DIR}
        --expected-point-lights=6
        --expected-rain=0.35
        --expected-snow=0.20
        --expected-fog-density=0.42
        --expected-clouds=1.25
        --expected-wind-dir=132.0
        --expected-wind-speed=8.5
        --expected-particle-intensity=1.80
        --expected-sun-azimuth=118.0
        --expected-sun-elevation=34.0
    )
    set_tests_properties(ShowcaseDemo_ControlOverrides
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 90
    )

    add_test(
      NAME ShowcaseDemo_StressModels
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
        --demo=$<TARGET_FILE:showcase_demo>
        --preset=models
        --output=${CMAKE_BINARY_DIR}/showcase_stress_models.png
        --stats-output=${CMAKE_BINARY_DIR}/showcase_stress_models.stats.json
        --width=960
        --height=540
        --frames=20
        --cwd=${CMAKE_SOURCE_DIR}
        --stress=models
    )
    set_tests_properties(ShowcaseDemo_StressModels
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 120
    )

    add_test(
      NAME ShowcaseDemo_StressParticles
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_showcase_demo.py
        --demo=$<TARGET_FILE:showcase_demo>
        --preset=particles
        --output=${CMAKE_BINARY_DIR}/showcase_stress_particles.png
        --stats-output=${CMAKE_BINARY_DIR}/showcase_stress_particles.stats.json
        --width=960
        --height=540
        --frames=20
        --cwd=${CMAKE_SOURCE_DIR}
        --stress=particles
    )
    set_tests_properties(ShowcaseDemo_StressParticles
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 120
    )

    if(WIN32)
      add_test(
        NAME ShowcaseDemo_InteractiveSmoke
        COMMAND
          ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/run_showcase_demo.py
          --launcher-demo=$<TARGET_FILE:showcase_demo>
          --scene=industrial_park
          --preset=overview
          --frames=5
          --width=960
          --height=540
      )
      set_tests_properties(ShowcaseDemo_InteractiveSmoke
        PROPERTIES
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          TIMEOUT 45
      )

      add_test(
        NAME ShowcaseDemo_HuilianDomStreamingProbe
        COMMAND
          ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/run_showcase_demo.py
          --launcher-demo=$<TARGET_FILE:showcase_demo>
          --scene=industrial_park
          --preset=huilian
          --frames=12
          --width=960
          --height=540
          --huilian-stream-probe
      )
      set_tests_properties(ShowcaseDemo_HuilianDomStreamingProbe
        PROPERTIES
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          TIMEOUT 45
      )

      add_test(
        NAME ShowcaseDemo_HuilianDomStreamingStress
        COMMAND
          ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/run_showcase_demo.py
          --launcher-demo=$<TARGET_FILE:showcase_demo>
          --scene=industrial_park
          --preset=huilian
          --frames=12
          --width=960
          --height=540
          --huilian-stream-stress-probe
      )
      set_tests_properties(ShowcaseDemo_HuilianDomStreamingStress
        PROPERTIES
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          TIMEOUT 60
      )

      add_test(
        NAME ShowcaseDemo_HuilianDomStreamingEndurance
        COMMAND
          ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/run_showcase_demo.py
          --launcher-demo=$<TARGET_FILE:showcase_demo>
          --launcher-timeout=80
          --launcher-attempts=1
          --launcher-no-copy-fallback
          --scene=industrial_park
          --preset=huilian
          --huilian-view=overview
          --frames=1
          --width=960
          --height=540
          --huilian-stream-endurance-probe
      )
      set_tests_properties(ShowcaseDemo_HuilianDomStreamingEndurance
        PROPERTIES
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          TIMEOUT 100
      )
    endif()
  else()
    message(WARNING "Python3 interpreter not found; showcase screenshot validation tests are disabled")
  endif()
endif()
