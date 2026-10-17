add_executable(disaster_demo main.cpp)

set_target_properties(disaster_demo PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)


target_link_libraries(disaster_demo
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

target_include_directories(disaster_demo
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

if(MSVC)
  target_compile_options(disaster_demo PRIVATE /W4)
else()
  target_compile_options(disaster_demo PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

message(STATUS "disaster_demo executable configured")

# ctest 截图回归 — 6 个 effect × 2 mode = 12 个 case。
# 工作目录是 engine root,让相对路径如 texture/icons/* 能找到。
if(HT_BUILD_TESTS)
  foreach(mode local global)
    foreach(effect 1 2 3 4 5 6)
      add_test(NAME DisasterDemo_${mode}_${effect}
        COMMAND disaster_demo --mode=${mode} --effect=${effect} --frames=20
                --screenshot=disaster_${mode}_${effect}.png
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
      set_tests_properties(DisasterDemo_${mode}_${effect} PROPERTIES TIMEOUT 30)
    endforeach()
  endforeach()
endif()
