find_package(CURL REQUIRED)

add_library(ht_core STATIC)

set_target_properties(ht_core PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

file(GLOB_RECURSE HT_CORE_SOURCES src/*.cpp)
file(GLOB_RECURSE HT_CORE_HEADERS include/*.hpp)

target_sources(ht_core PRIVATE ${HT_CORE_SOURCES})

# Link external dependencies
target_link_libraries(ht_core
  PUBLIC
    spdlog::spdlog
    fmt::fmt
    glm::glm
    CURL::libcurl
)

# Include directories
target_include_directories(ht_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Platform-specific linking
if(HT_PLATFORM_WINDOWS)
  target_link_libraries(ht_core PRIVATE ws2_32)
elseif(HT_PLATFORM_LINUX)
  target_link_libraries(ht_core PRIVATE pthread)
endif()

# Compiler flags specific to this target
if(MSVC)
  target_compile_options(ht_core PRIVATE /W4)
  if(HT_WARNINGS_AS_ERRORS)
    target_compile_options(ht_core PRIVATE /WX)
  endif()
else()
  target_compile_options(ht_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Precompiled header
target_precompile_headers(ht_core PRIVATE include/hongtu/core/common.hpp)

message(STATUS "ht_core configured")
