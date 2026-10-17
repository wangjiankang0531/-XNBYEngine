cmake_minimum_required(VERSION 3.20 FATAL_ERROR)

project(
  XNBYEngine
  VERSION 0.1.0
  DESCRIPTION "A high-performance 3D rendering engine using Vulkan"
  LANGUAGES C CXX
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CMake options
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" ${CMAKE_MODULE_PATH})
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type defaults
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "Profile" "RelWithDebInfo" "MinSizeRel")
endif()

message(STATUS "XNBY Engine - Build type: ${CMAKE_BUILD_TYPE}")

# vcpkg integration
if(DEFINED CMAKE_TOOLCHAIN_FILE)
  message(STATUS "Using CMake toolchain: ${CMAKE_TOOLCHAIN_FILE}")
endif()

# Platform and architecture detection
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Platform.cmake)

# Compiler options
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerOptions.cmake)

# Dependencies
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Dependencies.cmake)

# Options for features
option(HT_BUILD_TESTS "Build unit tests" ON)
option(HT_BUILD_RENDER_REGRESSION_TESTS "Register executable screenshot/render regression tests" ${HT_BUILD_TESTS})
option(HT_BUILD_TRIANGLE_DEMO "Build triangle demo" ON)
option(HT_BUILD_EARTH_DEMO "Build earth globe demo" ON)
option(HT_BUILD_LOCAL_DEMO "Build flat-world local-coordinate demo" ON)
option(HT_BUILD_GEO_PBR_DEMO "Build global-mode PBR demo (GlobeCamera + GeoEntity + PBR)" ON)
option(HT_ENABLE_PROFILING "Enable profiling support" OFF)
option(HT_ENABLE_VALIDATION "Enable Vulkan validation layers" ON)

message(STATUS "XNBY Engine Options:")
message(STATUS "  Build Tests: ${HT_BUILD_TESTS}")
message(STATUS "  Build Render Regression Tests: ${HT_BUILD_RENDER_REGRESSION_TESTS}")
message(STATUS "  Build Triangle Demo: ${HT_BUILD_TRIANGLE_DEMO}")
message(STATUS "  Build Earth Demo: ${HT_BUILD_EARTH_DEMO}")
message(STATUS "  Build Local Demo: ${HT_BUILD_LOCAL_DEMO}")
message(STATUS "  Build Geo-PBR Demo: ${HT_BUILD_GEO_PBR_DEMO}")
message(STATUS "  Enable Profiling: ${HT_ENABLE_PROFILING}")
message(STATUS "  Enable Validation: ${HT_ENABLE_VALIDATION}")

# Engine library subdirectories
add_subdirectory(core)
add_subdirectory(rhi)
add_subdirectory(renderer)
add_subdirectory(scene)
add_subdirectory(asset)
add_subdirectory(window)
add_subdirectory(coordinate)
add_subdirectory(measure)
add_subdirectory(gis)
add_subdirectory(cloud)
add_subdirectory(shaders)

# Enable testing early so subdirs (apps + tests) can call add_test() before
# `add_subdirectory(tests)` runs. Without this, demo-level ctest hooks
# (e.g. park_demo screenshot regression) silently no-op.
if(HT_BUILD_TESTS OR HT_BUILD_RENDER_REGRESSION_TESTS)
  enable_testing()
endif()

option(HT_ENGINE_LIBRARIES_ONLY "Build engine libraries without bundled demo applications" OFF)
if(NOT HT_ENGINE_LIBRARIES_ONLY)
# Applications
if(HT_BUILD_TRIANGLE_DEMO)
  add_subdirectory(apps/triangle_demo)
endif()

add_subdirectory(apps/gis_demo)
add_subdirectory(apps/terrain_demo)
add_subdirectory(apps/cloud_demo)
add_subdirectory(apps/gis_test)
option(HT_BUILD_BADALING_DEMO "Build Badaling Great Wall roaming demo" ON)
if(HT_BUILD_BADALING_DEMO AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/badaling_demo/CMakeLists.txt")
  add_subdirectory(apps/badaling_demo)
endif()

option(HT_BUILD_SHOWCASE_DEMO "Build Xineng Benyuan integrated showcase demo" ON)
if(HT_BUILD_SHOWCASE_DEMO AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/showcase_demo/CMakeLists.txt")
  add_subdirectory(apps/showcase_demo)
endif()
# (apps/model_test + apps/rendering_demo removed: they were demos built entirely
#  against the legacy secondary engine/ reference tree, which was deleted in the
#  repo cleanup. They never used the production top-level engine modules.)

if(HT_BUILD_EARTH_DEMO AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/earth_demo/CMakeLists.txt")
  add_subdirectory(apps/earth_demo)
endif()

if(HT_BUILD_LOCAL_DEMO AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/local_demo/CMakeLists.txt")
  add_subdirectory(apps/local_demo)
endif()

# park_demo — phase 1.5 integration: PolylineRenderer + BillboardRenderer
# + Geo*/Local* entities with --mode=local|global toggle.
option(HT_BUILD_PARK_DEMO "Build park_demo" ON)
if(HT_BUILD_PARK_DEMO AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/park_demo/CMakeLists.txt")
  add_subdirectory(apps/park_demo)
endif()

# disaster_demo — phase 2.5 integration: ParticleSystem + EmitterEntity
# + 6 灾害效果切换(部分待 2.3 / 2.4 完整 land)
option(HT_BUILD_DISASTER_DEMO "Build disaster_demo" ON)
if(HT_BUILD_DISASTER_DEMO AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/disaster_demo/CMakeLists.txt")
  add_subdirectory(apps/disaster_demo)
endif()

# scenario_demo — phase 3.8 integration: AnnotationManager + DrawingTool +
# Timeline driving particle emitters; F5/F9 save+load JSON annotations
option(HT_BUILD_SCENARIO_DEMO "Build scenario_demo" ON)
if(HT_BUILD_SCENARIO_DEMO AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/scenario_demo/CMakeLists.txt")
  add_subdirectory(apps/scenario_demo)
endif()

# evacuation_demo — phase 5.6 integration: 100 agents + CrowdSimulator
# (boid + repulsive) + Timeline-triggered fire obstacle
option(HT_BUILD_EVACUATION_DEMO "Build evacuation_demo" ON)
if(HT_BUILD_EVACUATION_DEMO AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/evacuation_demo/CMakeLists.txt")
  add_subdirectory(apps/evacuation_demo)
endif()

# offscreen_demo — A4 cloud-readiness smoke: RHIOffscreenTarget render →
# readback → PNG (no SDL window required)
option(HT_BUILD_OFFSCREEN_DEMO "Build offscreen_demo" ON)
if(HT_BUILD_OFFSCREEN_DEMO AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/offscreen_demo/CMakeLists.txt")
  add_subdirectory(apps/offscreen_demo)
endif()

if(HT_BUILD_GEO_PBR_DEMO AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/geo_pbr_demo/CMakeLists.txt")
  add_subdirectory(apps/geo_pbr_demo)
endif()

# Offline asset tools + park showcase — only when building the engine tree
# directly. The editor imports this file via add_subdirectory(..) and its
# vcpkg manifest doesn't carry meshoptimizer.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  add_subdirectory(tools/assimp_asset_converter)
  add_subdirectory(tools/mesh_decimator)
  add_subdirectory(apps/park_scene_demo)
  add_subdirectory(apps/cloud_render_server)  # P4 云渲染(macOS;libdatachannel 不进编辑器清单)
endif()

endif() # HT_ENGINE_LIBRARIES_ONLY

# Editor moved to standalone hongtu-editor repo. Legacy Qt6 scaffolding
# archived under archive_legacy_{apps,engine}_editor/ — kept in tree for
# reference only, not built.

# Tests
if(HT_BUILD_TESTS)
  add_subdirectory(tests)
endif()

# Summary
message(STATUS "")
message(STATUS "=== XNBY Engine Build Summary ===")
message(STATUS "Platform: ${HT_PLATFORM}")
message(STATUS "Architecture: ${HT_ARCHITECTURE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "===================================")
