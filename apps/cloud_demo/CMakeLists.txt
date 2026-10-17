add_executable(cloud_demo main.cpp)

target_link_libraries(cloud_demo
  PRIVATE
    ht_core
    ht_cloud
    glm::glm
)

# stb is header-only, just need include path
target_include_directories(cloud_demo PRIVATE
  ${CMAKE_SOURCE_DIR}/vcpkg_installed/arm64-osx/include
)

set_target_properties(cloud_demo PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

if(NOT MSVC)
  target_compile_options(cloud_demo PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

message(STATUS "cloud_demo configured")
