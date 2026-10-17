add_executable(local_demo main.cpp)

set_target_properties(local_demo PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)


target_link_libraries(local_demo
  PRIVATE
    ht_core
    ht_rhi
    ht_asset
    ht_coordinate
    ht_renderer
    ht_scene
    SDL2::SDL2
)

target_include_directories(local_demo
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
  target_compile_options(local_demo PRIVATE /W4)
else()
  target_compile_options(local_demo PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

message(STATUS "local_demo executable configured")
