add_library(ht_window STATIC)

set_target_properties(ht_window PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

file(GLOB_RECURSE HT_WINDOW_SOURCES src/*.cpp)

target_sources(ht_window PRIVATE ${HT_WINDOW_SOURCES})

# SDL2 comes from vcpkg via find_package(SDL2) in cmake/Dependencies.cmake,
# exposing the cross-platform SDL2::SDL2 target (aliased to the static variant
# on this triplet). No hardcoded brew dylib — portable to Linux/Windows.
target_link_libraries(ht_window
  PUBLIC
    ht_core
    SDL2::SDL2
  PRIVATE
    fmt::fmt
    spdlog::spdlog
)

# Include directories
target_include_directories(ht_window
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Compiler flags
if(MSVC)
  target_compile_options(ht_window PRIVATE /W4)
  if(HT_WARNINGS_AS_ERRORS)
    target_compile_options(ht_window PRIVATE /WX)
  endif()
else()
  target_compile_options(ht_window PRIVATE -Wall -Wextra -Wpedantic)
endif()

message(STATUS "ht_window configured")
