add_executable(terrain_demo main.cpp)


target_link_libraries(terrain_demo
  PRIVATE
    ht_renderer
    ht_rhi
    ht_core
    ht_asset
    ht_coordinate
    SDL2::SDL2
    glm::glm
)

set_target_properties(terrain_demo PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

if(NOT MSVC)
  target_compile_options(terrain_demo PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

message(STATUS "terrain_demo configured")
