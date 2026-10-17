add_library(ht_renderer STATIC)

set_target_properties(ht_renderer PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

file(GLOB_RECURSE HT_RENDERER_SOURCES src/*.cpp)

target_sources(ht_renderer PRIVATE ${HT_RENDERER_SOURCES})

# Link external dependencies
target_link_libraries(ht_renderer
  PUBLIC
    ht_core
    ht_rhi
    glm::glm
    ht_coordinate
    ht_gis
  PRIVATE
    fmt::fmt
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    stb
    ht_asset
)

# Include directories
target_include_directories(ht_renderer
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Compiler flags
if(MSVC)
  target_compile_options(ht_renderer PRIVATE /W4)
  if(HT_WARNINGS_AS_ERRORS)
    target_compile_options(ht_renderer PRIVATE /WX)
  endif()
else()
  target_compile_options(ht_renderer PRIVATE -Wall -Wextra -Wpedantic)
endif()

message(STATUS "ht_renderer configured")
