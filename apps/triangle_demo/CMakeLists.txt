add_executable(triangle_demo main.cpp)

set_target_properties(triangle_demo PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)


target_link_libraries(triangle_demo
  PRIVATE
    ht_core
    ht_rhi
    ht_asset
    SDL2::SDL2
)

target_include_directories(triangle_demo
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
  target_compile_options(triangle_demo PRIVATE /W4)
else()
  target_compile_options(triangle_demo PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

message(STATUS "triangle_demo executable configured")
