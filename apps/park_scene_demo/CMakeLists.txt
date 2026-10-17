# park_scene_demo — P1 vegetation system verification + the showcase park
# scene (trees/grass/ground/shadows). Headless screenshot driven:
#   park_scene_demo --frames=120 --screenshot=/tmp/park.png
add_executable(park_scene_demo main.cpp)
set_target_properties(park_scene_demo PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

find_path(STB_INCLUDE_DIRS "stb_image_write.h")
target_include_directories(park_scene_demo PRIVATE ${STB_INCLUDE_DIRS})
# SDL2::SDL2 comes from cmake/Dependencies.cmake — on macOS it resolves to the
# Vulkan-capable Homebrew SDL2 (vcpkg's macOS SDL2 lacks SDL_VIDEO_VULKAN).
target_link_libraries(park_scene_demo
  PRIVATE
    ht_core ht_rhi ht_renderer ht_scene ht_asset ht_coordinate
    SDL2::SDL2 glm::glm spdlog::spdlog
)
