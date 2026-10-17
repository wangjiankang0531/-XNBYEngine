add_library(ht_rhi STATIC)

set_target_properties(ht_rhi PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_sources(ht_rhi PRIVATE
  src/vulkan/VulkanDevice.cpp
  src/vulkan/VulkanCommandBuffer.cpp
  src/vulkan/VulkanOffscreenTarget.cpp
)

target_link_libraries(ht_rhi
  PUBLIC
    ht_core
    Vulkan::Vulkan
    volk::volk
    GPUOpen::VulkanMemoryAllocator
  PRIVATE
    fmt::fmt
    SDL2::SDL2  # VulkanDevice.cpp 用 SDL_vulkan.h;PRIVATE 不向消费者传播
)

if(WIN32)
  target_compile_definitions(ht_rhi PRIVATE
    VK_USE_PLATFORM_WIN32_KHR
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
endif()

target_include_directories(ht_rhi
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
  target_compile_options(ht_rhi PRIVATE /W4)
else()
  target_compile_options(ht_rhi PRIVATE -Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

message(STATUS "ht_rhi configured")
