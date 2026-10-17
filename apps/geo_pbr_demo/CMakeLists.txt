add_executable(geo_pbr_demo main.cpp)

set_target_properties(geo_pbr_demo PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)


target_link_libraries(geo_pbr_demo
  PRIVATE
    ht_core
    ht_rhi
    ht_asset
    ht_coordinate
    ht_renderer
    ht_scene
    SDL2::SDL2
)

target_include_directories(geo_pbr_demo
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
  target_compile_options(geo_pbr_demo PRIVATE /W4)
else()
  target_compile_options(geo_pbr_demo PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

message(STATUS "geo_pbr_demo executable configured")
