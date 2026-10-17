add_executable(park_demo main.cpp)

set_target_properties(park_demo PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)


target_link_libraries(park_demo
  PRIVATE
    ht_core
    ht_rhi
    ht_asset
    ht_coordinate
    ht_renderer
    ht_scene
    SDL2::SDL2
    nlohmann_json::nlohmann_json
    stb
)

target_include_directories(park_demo
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
  target_compile_options(park_demo PRIVATE /W4)
else()
  target_compile_options(park_demo PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

message(STATUS "park_demo executable configured")

# Headless screenshot regression — runs both modes for a few frames and
# writes a PNG via swapchain readback. Failure (non-zero exit) breaks ctest.
# Working directory is the engine root so the relative atlas path resolves.
if(HT_BUILD_TESTS)
  add_test(NAME ParkDemoLocalScreenshot
    COMMAND park_demo --mode=local --frames=10 --screenshot=park_demo_local.png
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(ParkDemoLocalScreenshot PROPERTIES TIMEOUT 30)

  add_test(NAME ParkDemoGlobalScreenshot
    COMMAND park_demo --mode=global --frames=10 --screenshot=park_demo_global.png
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(ParkDemoGlobalScreenshot PROPERTIES TIMEOUT 30)
endif()
