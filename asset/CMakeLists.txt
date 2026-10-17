set(HT_VCPKG_SHADERC_TOOL_DIRS "")
if(DEFINED VCPKG_TARGET_TRIPLET)
  set(HT_VCPKG_INSTALLED_ROOTS "${CMAKE_BINARY_DIR}/vcpkg_installed")
  if(DEFINED VCPKG_INSTALLED_DIR)
    list(PREPEND HT_VCPKG_INSTALLED_ROOTS "${VCPKG_INSTALLED_DIR}")
  endif()
  if(DEFINED _VCPKG_INSTALLED_DIR)
    list(PREPEND HT_VCPKG_INSTALLED_ROOTS "${_VCPKG_INSTALLED_DIR}")
  endif()
  list(REMOVE_DUPLICATES HT_VCPKG_INSTALLED_ROOTS)
  foreach(HT_VCPKG_INSTALLED_ROOT IN LISTS HT_VCPKG_INSTALLED_ROOTS)
    list(APPEND HT_VCPKG_SHADERC_TOOL_DIRS
      "${HT_VCPKG_INSTALLED_ROOT}/${VCPKG_TARGET_TRIPLET}/tools/shaderc"
      "${HT_VCPKG_INSTALLED_ROOT}/vcpkg/pkgs/shaderc_${VCPKG_TARGET_TRIPLET}/tools/shaderc"
      "${HT_VCPKG_INSTALLED_ROOT}/vcpkg/blds/shaderc/${VCPKG_TARGET_TRIPLET}-rel/glslc"
    )
  endforeach()
endif()

if(GLSLC_EXECUTABLE AND NOT EXISTS "${GLSLC_EXECUTABLE}")
  unset(GLSLC_EXECUTABLE CACHE)
endif()

find_program(GLSLC_EXECUTABLE
  NAMES glslc glslc.exe
  HINTS ${HT_VCPKG_SHADERC_TOOL_DIRS} $ENV{VULKAN_SDK}/bin /opt/homebrew/bin /usr/local/bin
)
if(GLSLC_EXECUTABLE)
  message(STATUS "Found glslc: ${GLSLC_EXECUTABLE}")
else()
  message(WARNING "glslc not found - runtime shader compilation will fail")
endif()

add_library(ht_asset STATIC)

set_target_properties(ht_asset PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

file(GLOB_RECURSE HT_ASSET_SOURCES src/*.cpp)

target_sources(ht_asset PRIVATE ${HT_ASSET_SOURCES})

# Link external dependencies
target_link_libraries(ht_asset
  PUBLIC
    ht_core
    ht_rhi
    ht_renderer
    assimp::assimp
    tinygltf
    nlohmann_json::nlohmann_json
    stb
  PRIVATE
    fmt::fmt
    spdlog::spdlog
)

# Include directories
target_include_directories(ht_asset
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Pass glslc path as a compile definition
if(GLSLC_EXECUTABLE)
  target_compile_definitions(ht_asset PRIVATE
    HT_GLSLC_PATH="${GLSLC_EXECUTABLE}"
  )
endif()

# Compiler flags
if(MSVC)
  target_compile_options(ht_asset PRIVATE /W4)
  if(HT_WARNINGS_AS_ERRORS)
    target_compile_options(ht_asset PRIVATE /WX)
  endif()
else()
  target_compile_options(ht_asset PRIVATE -Wall -Wextra -Wpedantic)
endif()

message(STATUS "ht_asset configured")
