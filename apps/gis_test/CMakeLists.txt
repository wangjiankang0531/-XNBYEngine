add_executable(gis_test main.cpp)

target_link_libraries(gis_test
  PRIVATE
    ht_gis
    ht_core
    nlohmann_json::nlohmann_json
)

set_target_properties(gis_test PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

message(STATUS "gis_test configured")
