add_library(ht_cloud STATIC)

set_target_properties(ht_cloud PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
)

file(GLOB_RECURSE HT_CLOUD_SOURCES src/*.cpp)

target_sources(ht_cloud PRIVATE ${HT_CLOUD_SOURCES})

if(APPLE)
  # Mp4Writer(AVAssetWriter,ObjC++)— 离线视频导出。
  target_sources(ht_cloud PRIVATE src/mp4_writer.mm)
  set_source_files_properties(src/mp4_writer.mm PROPERTIES
    COMPILE_FLAGS "-fobjc-arc")
endif()

target_link_libraries(ht_cloud
  PUBLIC
    ht_core
    glm::glm
    ht_window      # NetworkEventPump uses window::InputEvent + EventPump
)

if(APPLE)
  # VTEncoder — real H.264 via VideoToolbox (cloud render pipeline).
  target_link_libraries(ht_cloud PUBLIC
    "-framework VideoToolbox" "-framework CoreMedia"
    "-framework CoreVideo"    "-framework CoreFoundation"
    "-framework AVFoundation" "-framework Foundation")
endif()

target_include_directories(ht_cloud
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
  target_compile_options(ht_cloud PRIVATE /W4)
  if(HT_WARNINGS_AS_ERRORS)
    target_compile_options(ht_cloud PRIVATE /WX)
  endif()
else()
  target_compile_options(ht_cloud PRIVATE -Wall -Wextra -Wpedantic)
endif()

message(STATUS "ht_cloud configured")
