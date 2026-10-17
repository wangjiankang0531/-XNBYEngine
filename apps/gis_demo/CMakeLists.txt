add_executable(gis_demo main.cpp)


target_link_libraries(gis_demo
  PRIVATE
    ht_gis
    ht_rhi
    ht_core
    ht_asset
    SDL2::SDL2
    nlohmann_json::nlohmann_json
    glm::glm
)

set_target_properties(gis_demo PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

if(NOT MSVC)
  target_compile_options(gis_demo PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

message(STATUS "gis_demo configured")
