add_library(ht_measure STATIC)

set_target_properties(ht_measure PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

file(GLOB_RECURSE HT_MEASURE_SOURCES src/*.cpp)
target_sources(ht_measure PRIVATE ${HT_MEASURE_SOURCES})

target_link_libraries(ht_measure
  PUBLIC
    ht_coordinate
    glm::glm
)

target_include_directories(ht_measure
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
  target_compile_options(ht_measure PRIVATE /W4)
else()
  target_compile_options(ht_measure PRIVATE -Wall -Wextra)
endif()

message(STATUS "ht_measure configured")
