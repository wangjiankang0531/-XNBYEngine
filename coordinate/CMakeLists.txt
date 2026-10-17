add_library(ht_coordinate STATIC)

set_target_properties(ht_coordinate PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

file(GLOB_RECURSE HT_COORDINATE_SOURCES src/*.cpp)
file(GLOB_RECURSE HT_COORDINATE_HEADERS include/*.hpp)

target_sources(ht_coordinate PRIVATE ${HT_COORDINATE_SOURCES})

target_link_libraries(ht_coordinate
  PUBLIC
    ht_core
    glm::glm
  PRIVATE
    nlohmann_json::nlohmann_json
)

target_include_directories(ht_coordinate
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
  target_compile_options(ht_coordinate PRIVATE /W4)
  if(HT_WARNINGS_AS_ERRORS)
    target_compile_options(ht_coordinate PRIVATE /WX)
  endif()
else()
  target_compile_options(ht_coordinate PRIVATE -Wall -Wextra -Wpedantic)
endif()

message(STATUS "ht_coordinate configured")
