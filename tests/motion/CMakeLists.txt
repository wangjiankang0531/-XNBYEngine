cmake_minimum_required(VERSION 3.24)
project(XNBYMotionMath LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
get_filename_component(ENGINE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
find_package(glm CONFIG REQUIRED)
find_package(GTest CONFIG REQUIRED)
add_executable(ht_motion_math_tests
  "${ENGINE_ROOT}/tests/src/renderer/test_pose_smoother.cpp"
  "${ENGINE_ROOT}/tests/src/renderer/test_follow_camera.cpp"
  "${ENGINE_ROOT}/coordinate/src/geodetic.cpp"
  "${ENGINE_ROOT}/coordinate/src/enu_frame.cpp")
target_include_directories(ht_motion_math_tests PRIVATE
  "${ENGINE_ROOT}/renderer/include" "${ENGINE_ROOT}/coordinate/include" "${ENGINE_ROOT}/core/include")
target_link_libraries(ht_motion_math_tests PRIVATE glm::glm GTest::gtest_main)
if(MSVC)
  target_compile_options(ht_motion_math_tests PRIVATE /utf-8 /W4)
endif()
enable_testing()
include(GoogleTest)
gtest_discover_tests(ht_motion_math_tests)
