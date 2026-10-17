add_library(ht_scene STATIC)

set_target_properties(ht_scene PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

file(GLOB_RECURSE HT_SCENE_SOURCES src/*.cpp)

target_sources(ht_scene PRIVATE ${HT_SCENE_SOURCES})

# Link external dependencies
target_link_libraries(ht_scene
  PUBLIC
    ht_core
    ht_renderer
    glm::glm
    ht_coordinate
    nlohmann_json::nlohmann_json
  PRIVATE
    fmt::fmt
    spdlog::spdlog
)

# Include directories
target_include_directories(ht_scene
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Compiler flags
if(MSVC)
  target_compile_options(ht_scene PRIVATE /W4)
  if(HT_WARNINGS_AS_ERRORS)
    target_compile_options(ht_scene PRIVATE /WX)
  endif()
else()
  target_compile_options(ht_scene PRIVATE -Wall -Wextra -Wpedantic)
endif()

message(STATUS "ht_scene configured")
