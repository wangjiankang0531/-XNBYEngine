# assimp_asset_converter — offline authoring/interchange mesh -> glTF 2.0.
#
# The converter bakes the complete source-node transform hierarchy before
# export.  This is particularly important for refinery FBX catalogues, which
# commonly use FBX PreRotation in addition to ordinary local transforms.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.20)
  project(ht_assimp_asset_converter LANGUAGES CXX)
endif()

find_package(assimp CONFIG REQUIRED)

add_executable(assimp_asset_converter main.cpp)
set_target_properties(assimp_asset_converter PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)
target_link_libraries(assimp_asset_converter PRIVATE assimp::assimp)

if(MSVC)
  target_compile_options(assimp_asset_converter PRIVATE /W4)
else()
  target_compile_options(assimp_asset_converter PRIVATE -Wall -Wextra -Wpedantic)
endif()
