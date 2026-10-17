set(BADALING_DEMO_SOURCES main.cpp)
if(WIN32)
  list(APPEND BADALING_DEMO_SOURCES badaling_demo.rc)
endif()

add_executable(badaling_demo ${BADALING_DEMO_SOURCES})

target_link_libraries(badaling_demo
  PRIVATE
    ht_renderer
    ht_rhi
    ht_core
    ht_asset
    glm::glm
    stb
)

if(WIN32)
  target_link_libraries(badaling_demo PRIVATE user32)
  target_compile_definitions(badaling_demo PRIVATE
    VK_USE_PLATFORM_WIN32_KHR
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
else()
  target_link_libraries(badaling_demo PRIVATE SDL2::SDL2)
endif()

set_target_properties(badaling_demo PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

if(NOT MSVC)
  target_compile_options(badaling_demo PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
else()
  target_link_options(badaling_demo PRIVATE
    "/MANIFEST:EMBED"
    "/MANIFESTINPUT:${CMAKE_CURRENT_SOURCE_DIR}/badaling_demo.manifest"
  )
endif()

if(HT_BUILD_RENDER_REGRESSION_TESTS)
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(
      NAME BadalingAssetPackage
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_badaling_assets.py
        --asset=${CMAKE_SOURCE_DIR}/assets/badaling/badaling_terrain.bin
        --attribution=${CMAKE_SOURCE_DIR}/assets/badaling/SOURCE_ATTRIBUTION.txt
    )
    set_tests_properties(BadalingAssetPackage
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 30
    )

    add_test(
      NAME BadalingLightingSunAngles
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_badaling_lighting.py
        --demo=$<TARGET_FILE:badaling_demo>
        --output-dir=${CMAKE_BINARY_DIR}
        --width=1280
        --height=720
        --cwd=${CMAKE_SOURCE_DIR}
    )
    set_tests_properties(BadalingLightingSunAngles
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 120
    )

    add_test(
      NAME BadalingWeatherControls
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_SOURCE_DIR}/tools/verify_badaling_weather_controls.py
        --demo=$<TARGET_FILE:badaling_demo>
        --output-dir=${CMAKE_BINARY_DIR}
        --width=1280
        --height=720
        --cwd=${CMAKE_SOURCE_DIR}
    )
    set_tests_properties(BadalingWeatherControls
      PROPERTIES
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        TIMEOUT 120
    )

    foreach(_badaling_preset clear cloudy rain snow fog night)
      add_test(
        NAME BadalingDemo_${_badaling_preset}
        COMMAND
          ${Python3_EXECUTABLE}
          ${CMAKE_SOURCE_DIR}/tools/verify_badaling_demo.py
          --demo=$<TARGET_FILE:badaling_demo>
          --preset=${_badaling_preset}
          --output=${CMAKE_BINARY_DIR}/badaling_${_badaling_preset}.png
          --stats-output=${CMAKE_BINARY_DIR}/badaling_${_badaling_preset}.stats.json
          --width=1280
          --height=720
          --cwd=${CMAKE_SOURCE_DIR}
      )
      set_tests_properties(BadalingDemo_${_badaling_preset}
        PROPERTIES
          WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
          TIMEOUT 90
      )
    endforeach()
  else()
    message(WARNING "Python3 interpreter not found; Badaling screenshot validation tests are disabled")
  endif()
endif()

message(STATUS "badaling_demo configured")
