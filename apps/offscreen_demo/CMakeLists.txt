add_executable(offscreen_demo main.cpp)

set_target_properties(offscreen_demo PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)


target_link_libraries(offscreen_demo
  PRIVATE
    ht_core
    ht_rhi
    ht_asset
    stb
    SDL2::SDL2
)

target_include_directories(offscreen_demo
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
  target_compile_options(offscreen_demo PRIVATE /W4)
else()
  target_compile_options(offscreen_demo PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

message(STATUS "offscreen_demo executable configured")

# ctest — single screenshot regression.
if(HT_BUILD_TESTS)
  add_test(NAME OffscreenDemo_screenshot
    COMMAND offscreen_demo --frames=1 --screenshot=offscreen_triangle.png
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(OffscreenDemo_screenshot PROPERTIES TIMEOUT 30)
endif()
