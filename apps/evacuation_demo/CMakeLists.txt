add_executable(evacuation_demo main.cpp)

set_target_properties(evacuation_demo PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)


target_link_libraries(evacuation_demo
  PRIVATE
    ht_core
    ht_rhi
    ht_asset
    ht_coordinate
    ht_renderer
    ht_scene
    SDL2::SDL2
    stb
)

target_include_directories(evacuation_demo
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
  target_compile_options(evacuation_demo PRIVATE /W4)
else()
  target_compile_options(evacuation_demo PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

message(STATUS "evacuation_demo executable configured")

# ctest:4 time points × 2 modes = 8 screenshots
if(HT_BUILD_TESTS)
  foreach(mode local global)
    foreach(t 0 5 15 30)
      add_test(NAME EvacuationDemo_${mode}_t${t}
        COMMAND evacuation_demo --mode=${mode} --time=${t} --frames=2
                --screenshot=evacuation_${mode}_t${t}.png
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
      set_tests_properties(EvacuationDemo_${mode}_t${t} PROPERTIES TIMEOUT 30)
    endforeach()
  endforeach()
endif()
