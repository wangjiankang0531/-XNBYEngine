add_executable(scenario_demo main.cpp)

set_target_properties(scenario_demo PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)


target_link_libraries(scenario_demo
  PRIVATE
    ht_core
    ht_rhi
    ht_asset
    ht_coordinate
    ht_renderer
    ht_scene
    SDL2::SDL2
    stb
    nlohmann_json::nlohmann_json
)

target_include_directories(scenario_demo
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
  target_compile_options(scenario_demo PRIVATE /W4)
else()
  target_compile_options(scenario_demo PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

message(STATUS "scenario_demo executable configured")

# ctest 截图回归 — 5 时间点 × 2 mode = 10 个 case
if(HT_BUILD_TESTS)
  foreach(mode local global)
    foreach(t 0 5 15 25 40)
      add_test(NAME ScenarioDemo_${mode}_t${t}
        COMMAND scenario_demo --mode=${mode} --scenario=leak_to_fire
                --time=${t} --frames=2
                --screenshot=scenario_${mode}_t${t}.png
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
      set_tests_properties(ScenarioDemo_${mode}_t${t} PROPERTIES TIMEOUT 30)
    endforeach()
  endforeach()
endif()
