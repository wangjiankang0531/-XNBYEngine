# cloud_render_server — P4 云渲染服务端(macOS only:VideoToolbox 硬编)。
# 园区场景渲染 → VTEncoder H.264 → libdatachannel WebRTC → 浏览器。
# 网络栈(HTTP 信令 + WebRTC)全在本 app,引擎库零网络依赖。
if(NOT APPLE)
  message(STATUS "cloud_render_server skipped (VideoToolbox is macOS-only)")
  return()
endif()

add_executable(cloud_render_server main.cpp)
set_target_properties(cloud_render_server PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

find_package(LibDataChannel CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image.h")
target_include_directories(cloud_render_server PRIVATE ${STB_INCLUDE_DIRS})

# player.html 直接从源码目录读,改页面无需重编。
# HT_CRS_ENGINE_ROOT:仓库根绝对路径,用于稳定定位引擎自带资产(如演练人物
# CesiumMan.glb)—— agent 经 posix_spawn 起实例不改 cwd,相对路径不可靠。
target_compile_definitions(cloud_render_server PRIVATE
  HT_CRS_WEB_DIR="${CMAKE_CURRENT_SOURCE_DIR}/web"
  HT_CRS_ENGINE_ROOT="${CMAKE_SOURCE_DIR}")

target_link_libraries(cloud_render_server
  PRIVATE
    ht_core ht_rhi ht_renderer ht_scene ht_asset ht_coordinate ht_cloud
    SDL2::SDL2 glm::glm spdlog::spdlog
    LibDataChannel::LibDataChannel nlohmann_json::nlohmann_json
)
