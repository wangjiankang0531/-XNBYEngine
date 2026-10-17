add_library(ht_gis STATIC)

set_target_properties(ht_gis PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

file(GLOB_RECURSE HT_GIS_SOURCES src/*.cpp)

target_sources(ht_gis PRIVATE ${HT_GIS_SOURCES})

target_link_libraries(ht_gis
  PUBLIC
    ht_core
    ht_coordinate
    glm::glm
  PRIVATE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    stb
)

target_include_directories(ht_gis
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
  target_compile_options(ht_gis PRIVATE /W4)
  if(HT_WARNINGS_AS_ERRORS)
    target_compile_options(ht_gis PRIVATE /WX)
  endif()
else()
  target_compile_options(ht_gis PRIVATE -Wall -Wextra -Wpedantic)
endif()

message(STATUS "ht_gis configured")
