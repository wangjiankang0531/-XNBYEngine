# Find or fetch all project dependencies
# Modern CMake practices: use find_package with fallback to FetchContent

include(FetchContent)

# Vulkan SDK
find_package(Vulkan REQUIRED)
message(STATUS "Found Vulkan: ${Vulkan_VERSION}")

# SDL2 for window management.
#
# Linux / Windows: use the vcpkg-provided SDL2::SDL2 (Vulkan-capable there).
# macOS: vcpkg's SDL2 is built WITHOUT SDL_VIDEO_VULKAN (features=core only),
#   so SDL_Vulkan_CreateSurface can't make a MoltenVK surface → black screen.
#   Fall back to the Homebrew SDL2 (Vulkan-capable). Defined ONCE here as the
#   SDL2::SDL2 target so all 16 consumer CMakeLists stay platform-agnostic.
if(APPLE)
  # NO_DEFAULT_PATH: the vcpkg toolchain injects its own libSDL2.a (no Vulkan)
  # into the default search — restrict strictly to the Homebrew prefixes.
  find_library(HT_SDL2_LIB NAMES SDL2 PATHS /opt/homebrew/lib /usr/local/lib NO_DEFAULT_PATH)
  find_path(HT_SDL2_INCDIR NAMES SDL.h PATHS /opt/homebrew/include/SDL2 /usr/local/include/SDL2 NO_DEFAULT_PATH)
  if(HT_SDL2_LIB AND HT_SDL2_INCDIR)
    get_filename_component(HT_SDL2_INCPARENT "${HT_SDL2_INCDIR}" DIRECTORY)
    if(NOT TARGET SDL2::SDL2)
      add_library(SDL2::SDL2 SHARED IMPORTED)
    endif()
    set_target_properties(SDL2::SDL2 PROPERTIES
      IMPORTED_LOCATION "${HT_SDL2_LIB}"
      INTERFACE_INCLUDE_DIRECTORIES "${HT_SDL2_INCPARENT};${HT_SDL2_INCDIR}")
    message(STATUS "SDL2 (macOS): Homebrew Vulkan-capable SDL2 → ${HT_SDL2_LIB}")
  endif()
endif()
if(NOT TARGET SDL2::SDL2)
  find_package(SDL2 REQUIRED)
  message(STATUS "Found SDL2 (vcpkg)")
endif()

# spdlog for logging
function(ht_strip_msvc_pthread_link_items target_name)
  if(NOT MSVC OR NOT TARGET "${target_name}")
    return()
  endif()

  foreach(prop IN ITEMS INTERFACE_LINK_LIBRARIES INTERFACE_LINK_OPTIONS)
    get_target_property(link_items "${target_name}" "${prop}")
    if(NOT link_items OR link_items STREQUAL "link_items-NOTFOUND")
      continue()
    endif()

    set(filtered_items "")
    foreach(item IN LISTS link_items)
      if(item STREQUAL "Threads::Threads" OR
          item STREQUAL "-lpthreads" OR
          item STREQUAL "pthread" OR
          item STREQUAL "$<LINK_ONLY:-lpthreads>" OR
          item STREQUAL "$<LINK_ONLY:pthread>" OR
          item STREQUAL "$<LINK_ONLY:Threads::Threads>")
        continue()
      endif()
      list(APPEND filtered_items "${item}")
    endforeach()
    set_property(TARGET "${target_name}" PROPERTY "${prop}" "${filtered_items}")
  endforeach()
endfunction()

find_package(spdlog REQUIRED)
if(MSVC AND TARGET Threads::Threads)
  set_target_properties(Threads::Threads PROPERTIES
    INTERFACE_LINK_LIBRARIES ""
    INTERFACE_LINK_OPTIONS ""
  )
endif()
ht_strip_msvc_pthread_link_items(Threads::Threads)
ht_strip_msvc_pthread_link_items(spdlog::spdlog)
ht_strip_msvc_pthread_link_items(spdlog::spdlog_header_only)
message(STATUS "Found spdlog")

# fmt for string formatting
find_package(fmt REQUIRED)
message(STATUS "Found fmt")

# GLM for mathematics
find_package(glm REQUIRED)
message(STATUS "Found GLM")

# stb (header-only image loading)
# Add vcpkg share dirs to module path for FindStb.cmake
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/share/stb")
find_path(STB_INCLUDE_DIRS "stb_image.h")
if(NOT STB_INCLUDE_DIRS)
  message(FATAL_ERROR "Could not find stb headers")
endif()
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${STB_INCLUDE_DIRS})
message(STATUS "Found stb: ${STB_INCLUDE_DIRS}")

# volk - Vulkan loader
find_package(volk REQUIRED)
message(STATUS "Found volk")

# Vulkan Memory Allocator
find_package(VulkanMemoryAllocator REQUIRED)
message(STATUS "Found VulkanMemoryAllocator")

# assimp for model loading
find_package(assimp REQUIRED)
message(STATUS "Found assimp")

# tinygltf for glTF loading (header-only)
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")
if(NOT TINYGLTF_INCLUDE_DIRS)
  message(FATAL_ERROR "Could not find tinygltf headers")
endif()
add_library(tinygltf INTERFACE)
target_include_directories(tinygltf INTERFACE ${TINYGLTF_INCLUDE_DIRS})
message(STATUS "Found tinygltf: ${TINYGLTF_INCLUDE_DIRS}")

# nlohmann_json for JSON parsing
find_package(nlohmann_json REQUIRED)
message(STATUS "Found nlohmann_json")

# imgui for UI
find_package(imgui REQUIRED)
message(STATUS "Found imgui")

# GoogleTest for unit tests
if(HT_BUILD_TESTS)
  find_package(GTest REQUIRED)
  message(STATUS "Found GoogleTest")
endif()

# Note: editor moved to standalone hongtu-editor project (ImGui-based).
# Legacy Qt6 scaffolding archived under archive_legacy_*_editor/ — Qt6
# is no longer a build dependency of the engine.

# Summary
message(STATUS "")
message(STATUS "=== Dependencies Configuration ===")
message(STATUS "Vulkan SDK: FOUND")
message(STATUS "Window Management: SDL2")
message(STATUS "Logging: spdlog")
message(STATUS "Math: GLM")
message(STATUS "Model Loading: assimp, tinygltf")
message(STATUS "Rendering: volk, VMA")
if(HT_BUILD_TESTS)
  message(STATUS "Testing: GoogleTest")
endif()
message(STATUS "===================================")
