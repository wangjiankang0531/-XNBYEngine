// cloud_render_server — P4 云渲染:服务端渲染园区场景,WebRTC 推 H.264 到浏览器,
// 浏览器鼠标输入经 datachannel 回流驱动相机。
//
//   cloud_render_server [--port=8080] [--width=1280] [--height=720]
//                       [--fps=30] [--bitrate=6000] [--show]
//
// 然后浏览器打开 http://localhost:8080/ 即可漫游。
//
// 管线:ForwardSceneRenderer 帧 → readbackSwapchainImage(RGBA)→ BGRA 转换 →
// VTEncoder(VideoToolbox 硬编 H.264, Annex-B)→ H264RtpPacketizer → rtc::Track。
// 信令是一次性 POST /offer(非 trickle)。网络代码全部在本 app,引擎库零网络依赖。
//
// 场景与 park_scene_demo 同源(成熟参数直接复用)。

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <hongtu/asset/GltfLoader.h>
#include <hongtu/cloud/mp4_writer.hpp>
#include <hongtu/cloud/vt_encoder.hpp>
#include <hongtu/renderer/camera/orbit_camera_controller.hpp>
#include <hongtu/renderer/pbr/forward_scene_renderer.hpp>
#include <hongtu/renderer/picking/ray_caster.hpp>
#include <hongtu/renderer/environment/time_of_day.hpp>
#include <hongtu/renderer/environment/environment_settings.hpp>
#include <hongtu/renderer/environment/apply_environment.hpp>
#include <hongtu/renderer/pbr/mesh_vertex.hpp>
#include <hongtu/renderer/pbr/path_ribbon.hpp>
#include <hongtu/renderer/vegetation/vegetation_layer.hpp>
#include <hongtu/rhi/RHIDevice.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "dynamic_scene.hpp"
#include "http_server.hpp"
#include "project_loader.hpp"
#include "viewer_hub.hpp"
#include <rtc/rtc.hpp>

#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

using namespace HongTu::RHI;
using hongtu::asset::GltfLoader;
using hongtu::cloud::VTEncoder;
using hongtu::renderer::ForwardSceneRenderer;
using hongtu::renderer::MeshVertex;
using hongtu::renderer::PbrMaterial;
using hongtu::renderer::VegetationInstance;
using hongtu::renderer::VegetationLayer;
using hongtu::renderer::VegetationPipeline;

namespace {

struct Cli {
  uint16_t port    = 8080;
  std::string listen = "0.0.0.0";  ///< --listen=;agent 托管时传 127.0.0.1,信令不出本机
  int         gpu    = -1;         ///< --gpu=;macOS 单卡占位,多卡平台接设备选择
  std::string stun;        ///< --stun=host:port
  std::string turn;        ///< --turn=host:port
  std::string turn_user;   ///< --turn-user=
  std::string turn_pass;   ///< --turn-pass=
  bool        relay_only = false;  ///< --relay-only;强制走 TURN 中继(诊断)
  std::string project;  ///< --open=<编辑器项目目录>;空 = 内置 demo 场景
  // 离线产出:给了任意一个就渲完即退,不起 HTTP/WebRTC。
  std::string screenshot;      ///< --screenshot=out.png(--frames 帧后截)
  int         shot_frames = 60;
  std::string orbit_video;     ///< --orbit-video=out.mp4(绕场 360°)
  float    tod_hours   = -1.0f; ///< --tod=19.5:昼夜时刻(<0 = 关,用手调参数)
  float    tod_speed   = 0.0f;  ///< --tod-speed=4:播放倍速(1x=1 分钟播完 24h)
  float       video_seconds = 24.0f;
  int      width   = 1280, height = 720;
  float    fps     = 30.0f;
  uint32_t bitrate = 6000;  // kbps
  bool     show    = false;
};

// 浏览器输入驱动的相机状态(HTTP/datachannel 线程写,渲染线程读)。
//
// 轨道控制核心下沉到引擎共享组件 `hongtu::renderer::OrbitCameraController`,
// 与编辑器视口完全一致(鼠标拖拽语义 / 灵敏度逐字节相同)。本结构只做
// datachannel → controller 的命令映射 + 互斥保护(datachannel 线程写、
// 渲染线程读矩阵)。
struct CameraControl {
  std::mutex                              m;
  hongtu::renderer::OrbitCameraController ctrl;

  // 复位锚点:双击 reset 应回到「工程默认机位」,而非 resetView() 的世界原点+8m
  // (城市/geo 工程内容不在原点 → 旧 reset 会把画面甩飞丢场景)。
  bool      has_home    = false;
  glm::vec3 home_target{0.0f};
  float     home_distance = 8.0f, home_yaw = 0.0f, home_pitch = 0.0f;

  void setHome(const glm::vec3& target, float distance, float yaw, float pitch) {
    std::lock_guard<std::mutex> lock(m);
    has_home = true;
    home_target = target; home_distance = distance; home_yaw = yaw; home_pitch = pitch;
  }

  void apply(const nlohmann::json& j) {
    std::lock_guard<std::mutex> lock(m);
    const std::string t = j.value("t", "");
    if (t == "orbit") {
      // 浏览器 dx/dy(像素)→ 与编辑器右键拖拽相同的旋转。
      ctrl.onRotate(j.value("dx", 0.0f), j.value("dy", 0.0f));
    } else if (t == "zoom") {
      // 浏览器 wheel `d` 是原始 deltaY(一格 ~±100),而 onZoom 期望「滚轮格数」
      // (编辑器传 ±1)且内部钳到 [-3,3] —— 直接喂 deltaY 会每格都饱和到 ±3,
      // 单格 ~2.4× 距离暴量(用户报「滚轴范围过大」)。这里 ×0.01 把 deltaY 折成
      // 格数,使云端缩放手感与编辑器一致(−25%/+33% 每格)。d 正值=滚轮下滚=拉远,
      // onZoom 正值=拉近 → 取负。所有云端客户端(演练/播放页)统一受益。
      ctrl.onZoom(-j.value("d", 0.0f) * 0.01f);
    } else if (t == "pan") {
      // 浏览器 dx/dy(像素)→ 与编辑器中键拖拽相同的平移。
      ctrl.onPan(j.value("dx", 0.0f), j.value("dy", 0.0f));
    } else if (t == "reset") {
      // 回工程默认机位(没有则退回 resetView 的通用默认)。
      if (has_home) {
        ctrl.setTarget(home_target);
        ctrl.setDistance(home_distance);
        ctrl.setYawPitch(home_yaw, home_pitch);
      } else {
        ctrl.resetView();
      }
    }
  }
};

// 环境控制:datachannel ns:"environment" 命令增量更新,帧循环读取覆盖 FrameParams。
// 只动 ForwardSceneRenderer 已有的环境/后处理项;天气与高级调色不在此渲染路径。
struct EnvControl {
  std::mutex m;
  float tod_hours = -1.0f;     // <0 手动太阳;>=0 昼夜曲线(随时间推进)
  float tod_speed = 0.0f;      // 昼夜倍速(1x = 1 分钟走完 24h)
  float sun_az = 2.0f, sun_el = 0.95f;  // 手动太阳方位/仰角(弧度,tod<0 时用)
  float sun_intensity = 4.2f;
  float shadow = 0.65f;
  float exposure = 1.0f;
  float bloom = 0.0f, bloom_threshold = 1.2f;
  float ssao = 0.6f;
  bool  fxaa = true;
  int   skybox = 1;            // 0 星空 / 1 白天渐变 / 2 昼夜混合
  float night_factor = 0.0f;
  int   render_mode = 1;
  glm::vec3 clear_color{0.45f, 0.65f, 1.0f};

  void apply(const nlohmann::json& p) {
    std::lock_guard<std::mutex> lock(m);
    auto f = [&](const char* k, float& v) { if (p.contains(k) && p[k].is_number()) v = p[k].get<float>(); };
    auto i = [&](const char* k, int& v)   { if (p.contains(k) && p[k].is_number()) v = p[k].get<int>(); };
    f("time_of_day", tod_hours); f("time_speed", tod_speed);
    if (p.contains("sun_azimuth"))   sun_az = glm::radians(p["sun_azimuth"].get<float>());
    if (p.contains("sun_elevation")) sun_el = glm::radians(p["sun_elevation"].get<float>());
    f("sun_intensity", sun_intensity); f("shadow", shadow); f("exposure", exposure);
    f("bloom", bloom); f("bloom_threshold", bloom_threshold); f("ssao", ssao);
    if (p.contains("fxaa")) fxaa = p["fxaa"].get<bool>();
    i("skybox", skybox); f("night_factor", night_factor); i("render_mode", render_mode);
    if (p.contains("clear_color") && p["clear_color"].is_array() && p["clear_color"].size() >= 3)
      clear_color = {p["clear_color"][0].get<float>(), p["clear_color"][1].get<float>(),
                     p["clear_color"][2].get<float>()};
  }

  struct Snap {
    float tod_hours, tod_speed, sun_az, sun_el, sun_intensity, shadow, exposure,
          bloom, bloom_threshold, ssao;
    bool  fxaa; int skybox; float night_factor; int render_mode; glm::vec3 clear_color;
  };
  Snap read() {
    std::lock_guard<std::mutex> lock(m);
    return {tod_hours, tod_speed, sun_az, sun_el, sun_intensity, shadow, exposure,
            bloom, bloom_threshold, ssao, fxaa, skybox, night_factor, render_mode, clear_color};
  }
};

// ---- 场景几何(与 park_scene_demo 同一约定) --------------------------------
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed) {}
  float next() { s = s * 1664525u + 1013904223u; return static_cast<float>(s >> 8) / 16777216.0f; }
  float range(float a, float b) { return a + (b - a) * next(); }
};

void makePath(std::vector<MeshVertex>& verts, std::vector<uint32_t>& idx,
              float half_len = 95.0f, float half_w = 3.2f, float z = 0.02f) {
  const uint32_t base = static_cast<uint32_t>(verts.size());
  const int segs = 24;
  for (int i = 0; i <= segs; ++i) {
    float x = -half_len + 2.0f * half_len * i / segs;
    for (int sy = -1; sy <= 1; sy += 2) {
      MeshVertex v;
      v.position = {x, half_w * sy, z};
      v.normal   = {0, 0, 1};
      v.uv       = {x * 0.18f, (sy + 1) * 0.5f};
      verts.push_back(v);
    }
  }
  for (int i = 0; i < segs; ++i) {
    uint32_t a = base + i * 2, b = a + 1, c = a + 2, d = a + 3;
    idx.insert(idx.end(), {a, b, c, b, d, c});  // 与 makeGround 同绕序
  }
}

void makeGround(std::vector<MeshVertex>& verts, std::vector<uint32_t>& idx,
                float half = 600.0f, int divs = 24, float uv_per_m = 0.25f) {
  const uint32_t base = static_cast<uint32_t>(verts.size());
  for (int j = 0; j <= divs; ++j) {
    for (int i = 0; i <= divs; ++i) {
      float x = -half + 2.0f * half * i / divs;
      float y = -half + 2.0f * half * j / divs;
      MeshVertex v;
      v.position = {x, y, 0.0f};
      v.normal   = {0, 0, 1};
      v.uv       = {(x + half) * uv_per_m, (y + half) * uv_per_m};
      verts.push_back(v);
    }
  }
  for (int j = 0; j < divs; ++j) {
    for (int i = 0; i < divs; ++i) {
      uint32_t a = base + j * (divs + 1) + i;
      uint32_t b = a + 1, c = a + (divs + 1), d = c + 1;
      idx.insert(idx.end(), {a, c, b, b, c, d});
    }
  }
}

std::vector<uint8_t> loadRgba(const char* path, int& w, int& h) {
  int n = 0;
  unsigned char* p = stbi_load(path, &w, &h, &n, 4);
  if (!p) return {};
  std::vector<uint8_t> out(p, p + static_cast<size_t>(w) * h * 4);
  stbi_image_free(p);
  return out;
}

std::string loadTextFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

}  // namespace

int main(int argc, char** argv) {
#ifdef __APPLE__
  if (!std::getenv("VK_ICD_FILENAMES")) {
    const char* icd = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json";
    if (std::ifstream(icd).good()) {
      setenv("VK_ICD_FILENAMES", icd, 0);
      setenv("VK_DRIVER_FILES", icd, 0);
    }
  }
#endif

  Cli cli;
  bool rtc_verbose = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto starts = [&](const char* p) { return a.rfind(p, 0) == 0; };
    if (a == "--rtc-verbose")      rtc_verbose = true;
    else if (starts("--open="))    cli.project = a.substr(7);
    else if (starts("--screenshot="))   cli.screenshot    = a.substr(13);
    else if (starts("--frames="))       cli.shot_frames   = std::atoi(a.c_str() + 9);
    else if (starts("--orbit-video="))  cli.orbit_video   = a.substr(14);
    else if (starts("--video-seconds=")) cli.video_seconds = std::atof(a.c_str() + 16);
    else if (starts("--tod="))          cli.tod_hours     = std::atof(a.c_str() + 6);
    else if (starts("--tod-speed="))    cli.tod_speed     = std::atof(a.c_str() + 12);
    else if (starts("--port="))    cli.port    = static_cast<uint16_t>(std::atoi(a.c_str() + 7));
    else if (starts("--listen="))  cli.listen  = a.substr(9);
    else if (starts("--gpu="))     cli.gpu     = std::atoi(a.c_str() + 6);
    else if (starts("--stun="))      cli.stun      = a.substr(7);
    else if (starts("--turn="))      cli.turn      = a.substr(7);
    else if (starts("--turn-user=")) cli.turn_user = a.substr(12);
    else if (starts("--turn-pass=")) cli.turn_pass = a.substr(12);
    else if (a == "--relay-only")    cli.relay_only = true;
    else if (starts("--width="))   cli.width   = std::atoi(a.c_str() + 8);
    else if (starts("--height="))  cli.height  = std::atoi(a.c_str() + 9);
    else if (starts("--fps="))     cli.fps     = std::atof(a.c_str() + 6);
    else if (starts("--bitrate=")) cli.bitrate = std::atoi(a.c_str() + 10);
    else if (a == "--show")        cli.show    = true;
    else spdlog::warn("unknown arg {}", a);
  }

  if (rtc_verbose) {
    rtc::InitLogger(rtc::LogLevel::Debug, [](rtc::LogLevel lvl, std::string msg) {
      spdlog::info("[rtc:{}] {}", static_cast<int>(lvl), msg);
    });
  }

  // ---- window + device -------------------------------------------------------
  spdlog::info("cloud_render_server {}x{} @{}fps {}kbps, http {}:{} gpu {}",
               cli.width, cli.height, cli.fps, cli.bitrate, cli.listen, cli.port, cli.gpu);
  if (!cli.stun.empty() || !cli.turn.empty()) {
    spdlog::info("ICE configured: stun='{}' turn='{}' (user '{}')", cli.stun, cli.turn,
                 cli.turn_user);
  }
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    spdlog::error("SDL_Init failed: {}", SDL_GetError());
    return 1;
  }
  // 云渲染默认隐藏窗口(服务器无头跑);--show 调试时可见。
  SDL_Window* window = SDL_CreateWindow("XNBY — Cloud Render Server",
                                        SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED,
                                        cli.width,
                                        cli.height,
                                        SDL_WINDOW_VULKAN | (cli.show ? SDL_WINDOW_SHOWN : SDL_WINDOW_HIDDEN));
  if (!window) {
    spdlog::error("SDL_CreateWindow failed: {}", SDL_GetError());
    return 1;
  }

  auto* factory = Backend::createFactory(Backend::Type::Vulkan);
  unsigned int ec = 0;
  SDL_Vulkan_GetInstanceExtensions(window, &ec, nullptr);
  std::vector<const char*> exts(ec);
  SDL_Vulkan_GetInstanceExtensions(window, &ec, exts.data());
  SwapchainDesc sc;
  sc.extent     = {static_cast<uint32_t>(cli.width), static_cast<uint32_t>(cli.height)};
  sc.format     = Format::R8G8B8A8_SRGB;
  sc.imageCount = 3;
  sc.vsync      = false;  // 帧率由编码节拍控制,不用显示器节拍
  RHIDevice* device = factory->createDeviceWithSwapchain(window, sc, false, exts);
  Backend::destroyFactory(factory);
  if (!device) {
    spdlog::error("createDeviceWithSwapchain failed");
    return 1;
  }

  ForwardSceneRenderer renderer;
  if (!renderer.initialize(device)) {
    spdlog::error("ForwardSceneRenderer init failed");
    return 1;
  }

  // ---- 项目模式:--open 加载编辑器搭的场景;否则内置 demo 园区 -------------
  crs::ProjectScene project;
  const bool use_project = !cli.project.empty();
  if (use_project && !crs::loadProjectScene(cli.project, project)) {
    spdlog::error("--open {} failed", cli.project);
    return 1;
  }

  // ---- 工程环境设置:优先读 environment.environment.json,无则白天兜底 -----
  hongtu::renderer::EnvironmentSettings env_settings;  // 引擎默认值
  if (use_project) {
    auto envp = std::filesystem::path(cli.project) / "environment.environment.json";
    if (std::filesystem::exists(envp) &&
        hongtu::renderer::EnvironmentSettings::load(envp.string(), env_settings)) {
      spdlog::info("[env] loaded project environment: {}", envp.string());
    } else {
      // 无 authored 环境 → 白天兜底保证场景可见
      env_settings.tod_enabled = true;
      env_settings.tod_hours   = 12.0f;
      env_settings.tod_speed   = 0.0f;
      spdlog::info("[env] no project environment.json → daytime fallback (tod=12)");
    }
  } else {
    // demo 路径:沿用原有 EnvControl 默认值对应的设置(手动太阳, 昼天)
    env_settings.tod_enabled     = false;
    env_settings.sun_azimuth_deg = glm::degrees(2.0f);   // 原 sun_az=2.0
    env_settings.sun_elevation_deg = glm::degrees(0.95f);  // 原 sun_el=0.95
    env_settings.sun_intensity   = 4.2f;
    env_settings.shadow_strength = 0.65f;
    env_settings.ssao_enabled    = true;
    env_settings.ssao_intensity  = 0.6f;
    env_settings.fxaa_enabled    = true;
  }

  // ---- scene(与 park_scene_demo 同源)---------------------------------------
  std::vector<MeshVertex> verts;
  std::vector<uint32_t>   idx;
  makeGround(verts, idx, use_project ? std::max(project.ground_half, 50.0f) : 600.0f);
  const uint32_t ground_index_count = static_cast<uint32_t>(idx.size());
  struct PathBatch {  // 项目路径按贴图分组,材质阶段回填 mat_id
    std::string tex;
    uint32_t    index_offset = 0, index_count = 0;
    int         mat_id = -1;
  };
  std::vector<PathBatch> path_batches;
  if (!use_project) {
    makePath(verts, idx);  // 步道是 demo 布景
  } else {
    std::vector<std::string> order;
    for (const auto& pd : project.paths) {
      if (std::find(order.begin(), order.end(), pd.texture) == order.end()) {
        order.push_back(pd.texture);
      }
    }
    for (const auto& tset : order) {
      PathBatch b;
      b.tex          = tset;
      b.index_offset = static_cast<uint32_t>(idx.size());
      for (size_t pi = 0; pi < project.paths.size(); ++pi) {
        const auto& pd = project.paths[pi];
        if (pd.texture != tset) continue;
        const auto pts = hongtu::renderer::smoothOpenPolyline(pd.points, pd.smooth);
        // 与编辑器同款交叉分层(列表顺序,后者在上)。
        const float z = 0.018f + 0.004f * static_cast<float>(std::min<size_t>(pi, 16));
        hongtu::renderer::buildPathRibbon(pts, pd.half_width, verts, idx, 0.18f, z);
      }
      b.index_count = static_cast<uint32_t>(idx.size()) - b.index_offset;
      if (b.index_count > 0) path_batches.push_back(std::move(b));
    }
  }
  const uint32_t path_index_count = static_cast<uint32_t>(idx.size()) - ground_index_count;

  // ---- 工程模型:走 PBR 实体路径(与编辑器/geo_pbr_demo 同源)---------------
  // 模型几何并进共享 vbo/ibo(追加到 ground/path 之后),材质并进共享
  // materials 列表;每个模型一个 EntityDraw,带自己的 prim 范围 + 模型矩阵
  // (T·Rz·Ry·Rx·S,与编辑器逐字节一致,rotation_deg 已含导入 Rx+90 立正)。
  // 这样夜窗(env_params3.w=night_factor)/IBL/阴影全部随 FSR 的 PBR 管线生效,
  // 不再走 VegetationLayer(无窗灯、着色不同)。
  struct LoadedModel {
    hongtu::asset::ModelData data;
    glm::mat4 model_matrix{1.0f};
    uint32_t  prim_offset = 0;   // 进 prims 列表的起始下标(几何阶段填)
    uint32_t  prim_count  = 0;
    int       mat_slot_base = 0;  // 该模型材质在全局 materials 中的起始槽位(材质阶段填)
    // 动态控制(DynamicScene 注册句柄按 id 改这两项):
    std::uint64_t id = 0;          // 编辑器稳定 id(0=旧存档无 id,不可寻址)
    bool      visible = true;      // remove → false(几何已烘焙,隐藏代替删)
    glm::vec3 position{0.0f}, rotation_deg{0.0f}, scale{1.0f};  // 当前变换(增量更新缺省)
    uint32_t  draw_prim_base = 0;  // 本模型 prims 在全局 prims 列表的起始(每帧组 entities 用)
  };
  std::vector<LoadedModel> loaded_models;
  std::vector<std::pair<uint32_t, uint32_t>> model_prim_ranges;  // (index_offset,index_count) per primitive
  std::vector<int> model_prim_local_mat;  // 每个 primitive 的 glTF 局部材质索引(-1=fallback)
  if (use_project) {
    GltfLoader model_loader;
    for (const auto& m : project.models) {
      auto md = model_loader.load(m.glb.c_str());
      if (!md) {
        spdlog::error("[project] model load failed: {} — {}", m.glb, model_loader.lastError());
        continue;
      }
      LoadedModel lm;
      lm.prim_offset = static_cast<uint32_t>(model_prim_ranges.size());
      // 几何并进共享缓冲(顶点基偏移 = 当前 verts 大小)。
      uint32_t prim_n = 0;
      for (const auto& mesh : md->meshes) {
        const bool has_normals = mesh.normals.size() == mesh.positions.size();
        const bool has_uvs     = mesh.uvs.size()     == mesh.positions.size();
        const uint32_t vbase   = static_cast<uint32_t>(verts.size());
        for (size_t i = 0; i < mesh.positions.size(); ++i) {
          MeshVertex v;
          v.position = mesh.positions[i];
          v.normal   = has_normals ? mesh.normals[i] : glm::vec3(0.0f, 0.0f, 1.0f);
          v.uv       = has_uvs     ? mesh.uvs[i]     : glm::vec2(0.0f, 0.0f);
          verts.push_back(v);
        }
        const uint32_t ioff = static_cast<uint32_t>(idx.size());
        for (uint32_t k : mesh.indices) idx.push_back(vbase + k);
        model_prim_ranges.emplace_back(ioff, static_cast<uint32_t>(mesh.indices.size()));
        model_prim_local_mat.push_back(mesh.material_id);
        ++prim_n;
      }
      lm.prim_count = prim_n;
      // 模型矩阵:T·Rz·Ry·Rx·S(与 editor_app.cpp saveEditorScene→model_matrix 一致)。
      glm::mat4 M(1.0f);
      M = glm::translate(M, m.position);
      M = glm::rotate(M, glm::radians(m.rotation_deg.z), glm::vec3(0, 0, 1));
      M = glm::rotate(M, glm::radians(m.rotation_deg.y), glm::vec3(0, 1, 0));
      M = glm::rotate(M, glm::radians(m.rotation_deg.x), glm::vec3(1, 0, 0));
      M = glm::scale(M, m.scale);
      lm.model_matrix = M;
      lm.id           = m.id;        // 稳定 id → 动态接口寻址
      lm.position     = m.position;
      lm.rotation_deg = m.rotation_deg;
      lm.scale        = m.scale;
      lm.data = std::move(*md);
      loaded_models.push_back(std::move(lm));
    }
    if (!loaded_models.empty()) {
      spdlog::info("[project] {} model(s) → PBR entity path ({} primitives total)",
                   loaded_models.size(), model_prim_ranges.size());
    }
  }

  BufferDesc vd;
  vd.size             = verts.size() * sizeof(MeshVertex);
  vd.usage            = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
  vd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
  auto vbo            = device->createBuffer(vd);
  BufferDesc id_;
  id_.size             = idx.size() * sizeof(uint32_t);
  id_.usage            = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Index);
  id_.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
  auto ibo             = device->createBuffer(id_);
  std::memcpy(vbo->map(), verts.data(), vd.size);  vbo->unmap();
  std::memcpy(ibo->map(), idx.data(), id_.size);   ibo->unmap();

  auto& mf = renderer.getMaterialFactory();
  std::vector<PbrMaterial> materials;
  if (auto fb = mf.createFallback()) materials.push_back(std::move(*fb));
  else return 1;
  {
    int bw, bh, nw, nh, aw, ah;
    auto base = loadRgba("assets/park/ground/lawn_diff_2k.jpg", bw, bh);
    auto nor  = loadRgba("assets/park/ground/lawn_nor_gl_2k.jpg", nw, nh);
    auto arm  = loadRgba("assets/park/ground/lawn_arm_2k.jpg", aw, ah);
    if (base.empty()) {
      spdlog::error("ground textures missing — run tools/prep_park_assets.sh");
      return 1;
    }
    auto lawn = mf.create(base.data(), bw, bh, nor.data(), nw, nh,
                          arm.data(), aw, ah, glm::vec4(1.0f), 0.0f, 1.0f);
    if (!lawn) return 1;
    materials.push_back(std::move(*lawn));

    auto pb = loadRgba("assets/park/path/grass_path_2_diff_2k.jpg", bw, bh);
    auto pn = loadRgba("assets/park/path/grass_path_2_nor_gl_2k.jpg", nw, nh);
    auto pa = loadRgba("assets/park/path/grass_path_2_arm_2k.jpg", aw, ah);
    auto path = mf.create(pb.data(), bw, bh, pn.data(), nw, nh,
                          pa.data(), aw, ah, glm::vec4(1.0f), 0.0f, 1.0f);
    if (!path) return 1;
    materials.push_back(std::move(*path));

    // 项目路径的其他贴图集:逐集加载追加(mat_id = 槽位-1)。
    for (auto& b : path_batches) {
      if (b.tex == "grass_path_2") { b.mat_id = 1; continue; }
      int tw, th, tnw, tnh, taw, tah;
      auto tb = loadRgba(("assets/park/path/" + b.tex + "_diff_2k.jpg").c_str(), tw, th);
      auto tn = loadRgba(("assets/park/path/" + b.tex + "_nor_gl_2k.jpg").c_str(), tnw, tnh);
      auto ta = loadRgba(("assets/park/path/" + b.tex + "_arm_2k.jpg").c_str(), taw, tah);
      if (tb.empty()) {
        spdlog::warn("path texture set '{}' missing — falling back to grass_path_2", b.tex);
        b.mat_id = 1;
        continue;
      }
      auto m = mf.create(tb.data(), tw, th, tn.empty() ? nullptr : tn.data(), tnw, tnh,
                         ta.empty() ? nullptr : ta.data(), taw, tah,
                         glm::vec4(1.0f), 0.0f, 1.0f);
      if (!m) { b.mat_id = 1; continue; }
      materials.push_back(std::move(*m));
      b.mat_id = static_cast<int>(materials.size()) - 2;  // material_id+1 取槽位
    }
  }

  // ---- 工程模型材质:并进共享 materials(每模型记录起始槽位)-----------------
  // 与 geo_pbr_demo::createMaterialsFromModel 同款:fallback 已占 materials[0],
  // 模型材质追加到末尾;mesh.material_id(局部 0-based,-1=无)映射到全局槽位。
  for (auto& lm : loaded_models) {
    lm.mat_slot_base = static_cast<int>(materials.size());  // 该模型材质槽起点
    auto pickTex = [&](int ti) -> const hongtu::asset::TextureData* {
      if (ti < 0 || ti >= static_cast<int>(lm.data.textures.size())) return nullptr;
      const auto& t = lm.data.textures[ti];
      if (t.pixels.empty() || t.width <= 0 || t.height <= 0) return nullptr;
      return &t;
    };
    for (const auto& mat : lm.data.materials) {
      const auto* base   = pickTex(mat.base_color_texture);
      const auto* normal = pickTex(mat.normal_texture);
      const auto* mr     = pickTex(mat.metallic_roughness_texture);
      auto pm = mf.create(
          base   ? base->pixels.data()   : nullptr, base   ? base->width   : 0, base   ? base->height   : 0,
          normal ? normal->pixels.data() : nullptr, normal ? normal->width : 0, normal ? normal->height : 0,
          mr     ? mr->pixels.data()     : nullptr, mr     ? mr->width     : 0, mr     ? mr->height     : 0,
          mat.base_color_factor, mat.metallic_factor, mat.roughness_factor);
      if (!pm) { materials.push_back(materials[0]); continue; }  // 退化到 fallback 槽
      materials.push_back(std::move(*pm));
    }
  }

  VegetationLayer veg;
  // Vegetation shares the renderer's warm procedural IBL (set=2) so trees/props
  // get the same image-based lighting the editor uses — pass the IBL set layout
  // at init and bind the IBL descriptor set in record() below.
  if (!veg.initialize(device, &mf, renderer.getShadowSetLayout(),
                      renderer.getHdrRenderPass(),
                      renderer.getIblSetLayout())) {
    return 1;
  }
  if (!veg.initializeShadow(renderer.getShadowRenderPass())) {
    spdlog::warn("vegetation shadow pipeline failed — trees won't cast shadows");
  } else {
    renderer.setExternalShadowDraw(
        [&](const RHICommandBufferPtr& cmd, const glm::mat4& lvp) {
          veg.recordShadow(cmd, lvp);
        });
  }

  GltfLoader loader;
  auto addArch = [&](const char* path) -> int {
    auto res = loader.load(path);
    if (!res) { spdlog::error("load failed: {}", path); return -1; }
    return veg.addArchetype(*res);
  };

  if (use_project) {
    // ---- 项目场景:植被层走 VegetationLayer;模型已在上面走 PBR 实体路径 ----
    // (模型不再喂 VegetationLayer —— 那条路径无夜窗/着色与编辑器不一致。)
    for (const auto& l : project.vegetation) {
      const int id = addArch(l.glb.c_str());
      if (id < 0) continue;
      veg.setSwayAmplitude(id, l.sway);
      veg.setCastsShadow(id, l.casts_shadow);
      veg.setInstances(id, l.instances);
    }
  }

  const int arch_jacaranda = use_project ? -1 : addArch("assets/park/trees/jacaranda_tree.glb");
  const int arch_island    = use_project ? -1 : addArch("assets/park/trees/island_tree_02.glb");
  const int arch_shrub     = use_project ? -1 : addArch("assets/park/shrubs/shrub_02.glb");
  const int arch_grass     = use_project ? -1 : addArch("assets/park/grass/grass_medium_01.glb");
  if (!use_project && arch_island < 0) return 1;
  // 黑块取证隔离开关:CRS_NO_LOD 关远景档 / CRS_WIND 调风摆(见下)。
  if (!use_project && !std::getenv("CRS_NO_LOD")) {
    if (auto lod = loader.load("assets/park/trees/island_tree_02_lod1.glb"))
      veg.setArchetypeLod(arch_island, *lod, 60.0f);
    if (auto lod = loader.load("assets/park/trees/jacaranda_tree_lod1.glb"))
      veg.setArchetypeLod(arch_jacaranda, *lod, 55.0f);
  }
  if (arch_grass >= 0) { veg.setMaxDrawDistance(arch_grass, 90.0f); veg.setCastsShadow(arch_grass, false); }
  if (arch_shrub >= 0) veg.setMaxDrawDistance(arch_shrub, 220.0f);
  const int arch_lamp  = use_project ? -1 : addArch("assets/park/props/street_lamp_01.glb");
  const int arch_bench = use_project ? -1 : addArch("assets/park/props/bench.glb");
  if (arch_lamp  >= 0) veg.setSwayAmplitude(arch_lamp, 0.0f);
  if (arch_bench >= 0) veg.setSwayAmplitude(arch_bench, 0.0f);

  // 取证二分:CRS_NO_VEG 全关;CRS_ONLY=jac|island|shrub|grass|props 只留一种。
  const char* only = std::getenv("CRS_ONLY");
  auto want = [&](const char* k) {
    if (std::getenv("CRS_NO_VEG")) return false;
    return !only || std::string(only) == k;
  };
  if (!use_project) {  // plant(determinstic, 与 demo 同布局)
    Rng rng(20260610u);
    std::vector<VegetationInstance> jac;
    for (int i = 0; i < 12; ++i) {
      VegetationInstance v;
      float x = -66.0f + 12.0f * i;
      float y = (i % 2 == 0) ? -17.0f : 17.0f;
      v.pos   = {x + rng.range(-1.5f, 1.5f), y + rng.range(-1.5f, 1.5f), 0.0f};
      v.yaw   = rng.range(0.0f, 6.28f);
      v.scale = rng.range(0.7f, 0.92f);
      v.tint  = rng.next();
      jac.push_back(v);
    }
    for (int c = 0; c < 9; ++c) {
      float cx = rng.range(-175.0f, 175.0f);
      float cy = rng.range(-175.0f, 175.0f);
      if (std::abs(cy) < 28.0f) cy += (cy < 0 ? -55.0f : 55.0f);
      int n = 22 + static_cast<int>(rng.next() * 16);
      for (int i = 0; i < n; ++i) {
        VegetationInstance v;
        float ang = rng.range(0.0f, 6.28f);
        float rad = rng.range(5.0f, 42.0f);
        v.pos   = {cx + std::cos(ang) * rad, cy + std::sin(ang) * rad, 0.0f};
        v.yaw   = rng.range(0.0f, 6.28f);
        v.scale = rng.range(0.65f, 1.2f);
        v.tint  = rng.next();
        jac.push_back(v);
      }
    }
    if (arch_jacaranda >= 0 && want("jac")) veg.setInstances(arch_jacaranda, jac);

    std::vector<VegetationInstance> isl;
    for (int c = 0; c < 14; ++c) {
      float cx = rng.range(-170.0f, 170.0f);
      float cy = rng.range(-170.0f, 170.0f);
      if (std::abs(cy) < 25.0f) cy += (cy < 0 ? -40.0f : 40.0f);
      int n = 18 + static_cast<int>(rng.next() * 14);
      for (int i = 0; i < n; ++i) {
        VegetationInstance v;
        float ang = rng.range(0.0f, 6.28f);
        float rad = rng.range(4.0f, 34.0f);
        v.pos   = {cx + std::cos(ang) * rad, cy + std::sin(ang) * rad, 0.0f};
        v.yaw   = rng.range(0.0f, 6.28f);
        v.scale = rng.range(0.7f, 1.3f);
        v.tint  = rng.next();
        isl.push_back(v);
      }
    }
    if (want("island")) veg.setInstances(arch_island, isl);

    if (arch_lamp >= 0) {
      std::vector<VegetationInstance> lamps;
      for (float x = -84.0f; x <= 84.0f; x += 24.0f) {
        for (int sy = -1; sy <= 1; sy += 2) {
          VegetationInstance v;
          v.pos = {x, 4.6f * sy, 0.0f};
          v.yaw = (sy > 0) ? 3.14159f : 0.0f;
          lamps.push_back(v);
        }
      }
      if (want("props")) veg.setInstances(arch_lamp, lamps);
    }
    if (arch_bench >= 0) {
      std::vector<VegetationInstance> benches;
      for (float x = -72.0f; x <= 84.0f; x += 36.0f) {
        VegetationInstance v;
        v.pos = {x + 9.0f, -4.4f, 0.0f};
        v.yaw = 0.0f;
        benches.push_back(v);
      }
      if (want("props")) veg.setInstances(arch_bench, benches);
    }

    std::vector<VegetationInstance> shrubs;
    for (int i = 0; i < 160; ++i) {
      VegetationInstance v;
      v.pos   = {rng.range(-180.0f, 180.0f), rng.range(-180.0f, 180.0f), 0.0f};
      v.yaw   = rng.range(0.0f, 6.28f);
      v.scale = rng.range(0.8f, 1.6f);
      v.tint  = rng.next();
      shrubs.push_back(v);
    }
    if (arch_shrub >= 0 && want("shrub")) veg.setInstances(arch_shrub, shrubs);

    std::vector<VegetationInstance> grass;
    for (int i = 0; i < 2200; ++i) {
      VegetationInstance v;
      v.pos   = {rng.range(-120.0f, 120.0f), rng.range(-120.0f, 120.0f), 0.0f};
      v.yaw   = rng.range(0.0f, 6.28f);
      v.scale = rng.range(0.9f, 1.8f);
      v.tint  = rng.next();
      grass.push_back(v);
    }
    if (arch_grass >= 0 && want("grass")) veg.setInstances(arch_grass, grass);
  }

  // 离线产出模式:渲完写文件即退,不起流式管线。
  const bool offline = !cli.screenshot.empty() || !cli.orbit_video.empty();
  hongtu::cloud::Mp4Writer mp4;
  int orbit_total_frames = 0;
  if (!cli.orbit_video.empty()) {
    hongtu::cloud::Mp4Writer::Config mc;
    mc.width  = static_cast<uint32_t>(cli.width);
    mc.height = static_cast<uint32_t>(cli.height);
    mc.fps    = cli.fps;
    if (!mp4.open(cli.orbit_video, mc)) return 1;
    orbit_total_frames = std::max(1, static_cast<int>(cli.video_seconds * cli.fps));
  }

  // ---- encoder + hub + http ---------------------------------------------------
  crs::ViewerHub hub;
  VTEncoder      encoder;
  VTEncoder::Config ec_cfg;
  ec_cfg.width             = static_cast<uint32_t>(cli.width);
  ec_cfg.height            = static_cast<uint32_t>(cli.height);
  ec_cfg.fps               = cli.fps;
  ec_cfg.bitrate_kbps      = cli.bitrate;
  ec_cfg.keyframe_interval = static_cast<uint32_t>(cli.fps * 2);
  // /stats 指标:agent 轮询两次差分 frames_encoded 即得实际推流 fps。
  static std::atomic<int64_t>  last_input_ms{0};
  static std::atomic<uint64_t> frames_encoded{0};
  const auto                   stats_start = std::chrono::steady_clock::now();

  if (!offline &&
      !encoder.initialize(ec_cfg, [&](const std::vector<uint8_t>& annexb, bool key, uint64_t pts) {
        ++frames_encoded;
        hub.sendFrame(annexb, key, pts);
      })) {
    spdlog::error("VTEncoder init failed");
    return 1;
  }

  CameraControl cam;
  if (use_project) {
    // 用工程保存的相机(target/distance/yaw/pitch)初始化 —— 云端初始视角与
    // 编辑器保存时的视角一致。
    cam.ctrl.setTarget(project.cam_target);
    cam.ctrl.setDistance(project.cam_distance);
    cam.ctrl.setYawPitch(project.cam_yaw, project.cam_pitch);
  } else {
    // 内置 demo:沿用历史默认视角。旧手搓相机 eye = look + dir(az,el)*dist,
    // 控制器 eye = target - dir(yaw,pitch)*dist → 等价换算 yaw=az+π, pitch=-el。
    // (原 az=2.35/el=0.22/dist=90/look=(0,0,2))
    cam.ctrl.setTarget({0.0f, 0.0f, 2.0f});
    cam.ctrl.setDistance(90.0f);
    cam.ctrl.setYawPitch(2.35f + 3.14159265f, -0.22f);
  }
  // 记住初始机位作为 reset 锚点(双击复位回这里,不再甩到世界原点)。
  cam.setHome(cam.ctrl.getTarget(), cam.ctrl.getDistance(),
              cam.ctrl.getYaw(), cam.ctrl.getPitch());

  // 动态场景(运行时标绘:datachannel 命令增删折线/区域/标注/箭头 + 图层)。
  crs::DynamicScene dyn;
  {
    int aw = 0, ah = 0;
    auto atlas_px = loadRgba("texture/icons/park.png", aw, ah);
    if (!dyn.initialize(device, renderer.getHdrRenderPass(),
                        atlas_px.empty() ? nullptr : atlas_px.data(), aw, ah,
                        "texture/icons/park.json",
                        &mf, renderer.getShadowSetLayout(),
                        renderer.getIblSetLayout())) {
      spdlog::warn("[scene] DynamicScene init failed — 动态标绘不可用");
    }
    // 要素模型 glb 随工程打包,按工程根(--open)解析其 assets 相对路径。
    if (use_project) dyn.setProjectRoot(cli.project);
  }

  // 命令行 --tod 覆盖:若显式传 --tod=<h>,用它覆盖工程/demo 默认。
  if (cli.tod_hours >= 0.0f) {
    env_settings.tod_enabled = true;
    env_settings.tod_hours   = cli.tod_hours;
    env_settings.tod_speed   = cli.tod_speed;
    // 显式给了 --tod-speed(>0)才自动播放;否则静止在 --tod 时刻。
    env_settings.tod_playing = cli.tod_speed > 0.0f;
    env_settings.bloom_enabled    = true;
    env_settings.bloom_intensity  = 0.35f;  // 昼夜启动默认开夜灯光晕
  }
  if (std::getenv("CRS_MODE")) {
    env_settings.render_mode = std::atoi(std::getenv("CRS_MODE"));
  }

  // 运行时 datachannel 环境调参(ns:"environment")用此锁保护 env_settings 写。
  std::mutex env_settings_mutex;

  hub.setInputHandler([&](const nlohmann::json& j) {
    last_input_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    // 路由:environment → 环境控制;其余 ns → 场景命令(标绘/图层/模型/火苗);
    // 无 ns → 相机指令。
    const std::string ns = j.value("ns", "");
    if (ns == "environment") {
      // 将 datachannel 的增量 JSON 直接写入 env_settings。
      // 已接线字段:time_of_day, time_speed, sun_azimuth(deg), sun_elevation(deg),
      //             sun_intensity, shadow, exposure, bloom, bloom_threshold, ssao,
      //             fxaa, render_mode。
      // 未接线(较复杂):clear_color, night_factor, skybox, grade_*, godray_*, dof_*。
      std::lock_guard<std::mutex> lk(env_settings_mutex);
      auto f = [&](const char* k, float& v) {
        if (j.contains(k) && j[k].is_number()) v = j[k].get<float>();
      };
      auto i = [&](const char* k, int& v) {
        if (j.contains(k) && j[k].is_number()) v = j[k].get<int>();
      };
      if (j.contains("time_of_day") && j["time_of_day"].is_number()) {
        env_settings.tod_enabled = true;
        env_settings.tod_hours   = j["time_of_day"].get<float>();
      }
      f("time_speed",     env_settings.tod_speed);
      // 浏览器播放/暂停昼夜:显式 tod_playing,或 time_speed 隐含(>0 播放)。
      if (j.contains("tod_playing") && j["tod_playing"].is_boolean())
        env_settings.tod_playing = j["tod_playing"].get<bool>();
      else if (j.contains("time_speed") && j["time_speed"].is_number())
        env_settings.tod_playing = j["time_speed"].get<float>() > 0.0f;
      if (j.contains("sun_azimuth")   && j["sun_azimuth"].is_number())
        env_settings.sun_azimuth_deg   = j["sun_azimuth"].get<float>();
      if (j.contains("sun_elevation") && j["sun_elevation"].is_number())
        env_settings.sun_elevation_deg = j["sun_elevation"].get<float>();
      f("sun_intensity",  env_settings.sun_intensity);
      f("shadow",         env_settings.shadow_strength);
      f("exposure",       env_settings.ibl_intensity);  // legacy 'exposure' → ibl_intensity 亮度旋钮
      f("bloom",          env_settings.bloom_intensity);
      if (j.contains("bloom")) env_settings.bloom_enabled = (env_settings.bloom_intensity > 0.0f);
      f("bloom_threshold", env_settings.bloom_threshold);
      if (j.contains("ssao") && j["ssao"].is_number()) {
        env_settings.ssao_intensity = j["ssao"].get<float>();
        env_settings.ssao_enabled   = (env_settings.ssao_intensity > 0.0f);
      }
      if (j.contains("fxaa")) env_settings.fxaa_enabled = j["fxaa"].get<bool>();
      i("render_mode", env_settings.render_mode);
      return;
    }
    if (!ns.empty()) { dyn.handleCommand(j); return; }
    // 屏幕拾取(无 ns,t:"pick"):反投影 (u,v) 成世界射线,**优先打中真实场景
    // 几何**(地形/地面模型/建筑等可作地皮的网格),退而求 z=0 平面;经 input
    // datachannel 回发 {t:"pick_result", id, ok, pos:[x,y,z]}。反投影+射线求交数学
    // 全在引擎(ray_caster + OrbitCameraController),浏览器只发请求收结果。
    if (j.value("t", std::string()) == "pick") {
      const float u  = j.value("u", 0.5f);
      const float vv = j.value("v", 0.5f);
      hongtu::renderer::WorldRay ray;
      {
        std::lock_guard<std::mutex> lock(cam.m);
        const float aspect = static_cast<float>(cli.width) / static_cast<float>(cli.height);
        const glm::mat4 view = cam.ctrl.getViewMatrix();
        // ⚠️ 复用编辑器同款拾取函数 screenToWorldRay,它**假设 GL 风格 Y-up NDC**
        // (内部自带 sy 翻转),所以这里必须用**未翻转的 GL proj**(不能用
        // getProjMatrix —— 那个带 Vulkan p[1][1]*=-1,会让拾取竖直镜像、布放位置
        // 与点击不符)。sx/sy = 视频内归一化 (u,v) × 渲染分辨率。
        const glm::mat4 proj_gl = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 200000.0f);
        const glm::dmat4 inv_vp = glm::inverse(glm::dmat4(proj_gl) * glm::dmat4(view));
        ray = hongtu::renderer::screenToWorldRay(
            static_cast<double>(u) * cli.width, static_cast<double>(vv) * cli.height,
            cli.width, cli.height, inv_vp);
      }
      // 先打项目网格(地形/地面/建筑),取沿射线最近命中;再与 z=0 地面比较取更近者。
      double best_t = std::numeric_limits<double>::max();
      bool   hit    = false;
      for (const auto& lm : loaded_models) {
        const glm::dmat4 mw(lm.model_matrix);
        for (const auto& mesh : lm.data.meshes) {
          if (mesh.positions.empty() || mesh.indices.size() < 3) continue;
          auto th = hongtu::renderer::intersectRayMesh(ray, mesh.positions, mesh.indices, mw);
          if (th && *th > 0.0 && *th < best_t) { best_t = *th; hit = true; }
        }
      }
      if (auto tp = hongtu::renderer::intersectRayPlane(ray, glm::dvec3(0.0),
                                                        glm::dvec3(0.0, 0.0, 1.0))) {
        if (*tp > 0.0 && *tp < best_t) { best_t = *tp; hit = true; }
      }
      nlohmann::json res;
      res["t"]  = "pick_result";
      res["id"] = j.value("id", 0);
      if (hit) {
        const glm::dvec3 p = ray.origin + ray.direction * best_t;
        res["ok"]  = true;
        res["pos"] = {p.x, p.y, p.z};
      } else {
        res["ok"] = false;  // 射线既未命中几何也未命中地面平面(指向天空)
      }
      hub.sendInput(res.dump());
      return;
    }
    cam.apply(j);
  });
  hub.setKeyframeRequest([&] { encoder.requestKeyframe(); });

  // ICE 服务器:host:port 拆分后构造 STUN/TURN,跨网段时实例侧才能出 relay 候选。
  {
    std::vector<rtc::IceServer> ice;
    auto split = [](const std::string& hp, std::string& host, uint16_t& port) {
      const size_t c = hp.rfind(':');
      host = hp.substr(0, c);
      port = static_cast<uint16_t>(std::atoi(hp.c_str() + c + 1));
    };
    if (!cli.stun.empty()) {
      std::string h; uint16_t p;
      split(cli.stun, h, p);
      ice.emplace_back(h, p);
    }
    if (!cli.turn.empty()) {
      std::string h; uint16_t p;
      split(cli.turn, h, p);
      ice.emplace_back(h, p, cli.turn_user, cli.turn_pass, rtc::IceServer::RelayType::TurnUdp);
    }
    if (!ice.empty()) {
      hub.setIceServers(std::move(ice));
      hub.setRelayOnly(cli.relay_only);
      spdlog::info("ICE servers: stun='{}' turn='{}'{}", cli.stun, cli.turn,
                   cli.relay_only ? " [relay-only]" : "");
    }
  }

  // player.html 从源码目录读(开发流);找不到给出兜底提示页。
  const std::string web_dir = HT_CRS_WEB_DIR;
  crs::HttpServer http;
  if (!offline && !http.start(cli.port, [&](const std::string& method, const std::string& path,
                                const std::string& body) -> crs::HttpServer::Response {
        if (method == "GET" && (path == "/" || path == "/index.html")) {
          std::string page = loadTextFile(web_dir + "/player.html");
          if (page.empty()) page = "<h1>player.html not found</h1>";
          return {200, "text/html; charset=utf-8", page};
        }
        // SDK 在线调试台(可编辑代码 + 实时效果 + 命令日志)。
        if (method == "GET" && (path == "/playground" || path == "/playground.html")) {
          std::string page = loadTextFile(web_dir + "/playground.html");
          if (page.empty()) return {404, "text/plain", "playground.html not found"};
          return {200, "text/html; charset=utf-8", page};
        }
        // JS SDK 模块(player.html <script src> + 外部开发者可直接拉取)。
        // htscene.js(传输层)依赖 htscene-protocol.js(协议层),两个都暴露。
        if (method == "GET" && (path == "/htscene.js" || path == "/htscene-protocol.js")) {
          std::string js = loadTextFile(web_dir + path);
          if (js.empty()) return {404, "text/plain", "not found"};
          return {200, "application/javascript; charset=utf-8", js};
        }
        // 服务端造 offer(见 viewer_hub.hpp 注释):页面先 POST /session 拿
        // {id, sdp},应答后 POST /answer 回 {id, sdp}。
        // agent 轮询用:观看数/最近输入/累计帧数,fps 由调用方差分计算。
        if (method == "GET" && path == "/stats") {
          nlohmann::json out = {
              {"viewer_count", hub.viewerCount()},
              {"open_viewer_count", hub.openViewerCount()},
              {"last_input_ms", last_input_ms.load()},
              {"frames_encoded", frames_encoded.load()},
              {"uptime_s", std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::steady_clock::now() - stats_start)
                               .count()},
              {"width", cli.width},
              {"height", cli.height},
              {"fps_target", cli.fps},
              {"bitrate_kbps", cli.bitrate},
              {"project", cli.project},
              {"gpu", cli.gpu},
              {"pid", getpid()}};
          return {200, "application/json", out.dump()};
        }
        if (method == "POST" && path == "/session") {
          bool can_control = true;  // body {"control": bool};缺省可控制
          if (!body.empty()) {
            auto j = nlohmann::json::parse(body, nullptr, false);
            if (!j.is_discarded()) can_control = j.value("control", true);
          }
          auto [id, offer] = hub.createSession(can_control);
          if (id == 0) return {500, "text/plain", "session failed"};
          nlohmann::json out = {{"id", id}, {"type", "offer"}, {"sdp", offer}};
          return {200, "application/json", out.dump()};
        }
        if (method == "POST" && path == "/answer") {
          try {
            auto j = nlohmann::json::parse(body);
            if (!hub.acceptAnswer(j.at("id").get<uint64_t>(), j.at("sdp").get<std::string>()))
              return {500, "text/plain", "answer rejected"};
            return {200, "application/json", "{\"ok\":true}"};
          } catch (const std::exception& e) {
            return {500, "text/plain", std::string("bad answer: ") + e.what()};
          }
        }
        return {404, "text/plain", "not found"};
      }, cli.listen)) {
    spdlog::error("HTTP server failed to bind {}:{}", cli.listen, cli.port);
    return 1;
  }
  if (!offline) {
    spdlog::info("ready — open http://localhost:{}/", cli.port);
  } else {
    spdlog::info("offline render: {}{}",
                 cli.screenshot.empty() ? "" : "screenshot " + cli.screenshot,
                 cli.orbit_video.empty()
                     ? ""
                     : "orbit video " + cli.orbit_video + " (" +
                           std::to_string(orbit_total_frames) + " frames)");
  }

  // ---- frame loop ---------------------------------------------------------------
  ForwardSceneRenderer::EntityDraw ground_entity;
  ground_entity.model      = glm::mat4(1.0f);
  ground_entity.color_tint = glm::vec4(1.0f);
  std::vector<ForwardSceneRenderer::EntityDraw> entities = {ground_entity};
  std::vector<ForwardSceneRenderer::PrimitiveDraw> prims;
  if (!use_project || project.ground_enabled) {
    prims.push_back({0u, ground_index_count, 0 /*lawn — material_id+1 取槽位*/});
  }
  if (!use_project && path_index_count > 0) {
    prims.push_back({ground_index_count, path_index_count, 1 /*path*/});
  }
  for (const auto& b : path_batches) {
    prims.push_back({b.index_offset, b.index_count, b.mat_id});
  }
  // ground/path 占 prims 的前缀;模型 prims 追加在后,各模型 EntityDraw 只画
  // 自己的范围(prim_count!=0),ground_entity 则显式限定到这段前缀,否则它会
  // 把模型几何也画一遍(renderFrame:prim_count==0 = 画全部)。
  const uint32_t ground_prim_count = static_cast<uint32_t>(prims.size());
  // ground/path 实体单独保留(模型可显隐 + 动态改变换 → entities 每帧重组)。
  bool has_ground_entity = false;
  if (!loaded_models.empty()) {
    if (ground_prim_count > 0) {
      ground_entity.prim_offset = 0;
      ground_entity.prim_count  = ground_prim_count;  // ground/path 不越界到模型 prims
      has_ground_entity = true;
    }
    // else: 无 ground 几何 → 不要 ground 实体(prim_count=0 会被当作『画全部』)。
    // 模型 prims 一次性追加进 prims(几何静态);base 记进 lm 供每帧组 entities。
    for (auto& lm : loaded_models) {
      lm.draw_prim_base = static_cast<uint32_t>(prims.size());
      for (uint32_t p = 0; p < lm.prim_count; ++p) {
        const auto& range = model_prim_ranges[lm.prim_offset + p];
        const int local_mat = model_prim_local_mat[lm.prim_offset + p];
        // 局部材质索引→全局 materials 槽位→renderFrame 的 material_id(=槽位-1)。
        // local_mat<0(无材质)→ -1(fallback,槽位 0)。
        const int mat_id = local_mat < 0 ? -1 : (lm.mat_slot_base + local_mat) - 1;
        prims.push_back({range.first, range.second, mat_id});
      }
    }
  } else {
    has_ground_entity = true;  // 纯地面场景(无模型):保留 ground 实体画全部
  }
  // 动态接口注册:让 model:update/remove 能按编辑器 id 改 loaded_models 的变换/显隐。
  if (use_project) {
    for (auto& lm : loaded_models) {
      if (lm.id == 0) continue;
      LoadedModel* p = &lm;  // loaded_models 此后不再增长 → 指针稳定
      dyn.registerStaticModel(
          lm.id, lm.position, lm.rotation_deg, lm.scale,
          [p](const glm::mat4& M) { p->model_matrix = M; },
          [p](bool v) { p->visible = v; });
    }
    // 工程火苗预建进动态层(同一渲染/寻址路径,可按 id 控制)。
    for (const auto& f : project.fires) {
      if (f.enabled) dyn.seedFire(f.id, f.type, f.position, f.scale);
    }
    if (!loaded_models.empty() || !project.fires.empty())
      spdlog::info("[project] 动态接口已注册 {} 模型 + {} 火苗(按编辑器 id 可控)",
                   loaded_models.size(), project.fires.size());
  }

  bool     running = true;
  uint64_t frame   = 0;
  float    t       = 0.0f;
  const double frame_dt   = 1.0 / cli.fps;
  const uint64_t perf_hz  = SDL_GetPerformanceFrequency();
  uint64_t t_prev         = SDL_GetPerformanceCounter();
  double   next_deadline  = static_cast<double>(t_prev) / perf_hz;
  std::vector<uint8_t> rgba, bgra;

  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
    }

    const uint64_t t_now = SDL_GetPerformanceCounter();
    const float dt = static_cast<float>(t_now - t_prev) / perf_hz;
    t_prev = t_now;
    t += dt;

    // 读相机:渲染线程取一份控制器快照(datachannel 线程可能在写)。
    glm::vec3 eye, look;
    glm::mat4 view, proj;
    {
      std::lock_guard<std::mutex> lock(cam.m);
      if (offline && orbit_total_frames > 0) {
        // 绕场一周:在当前 yaw 基础上匀速叠加 360°,拷一份控制器只动 yaw。
        hongtu::renderer::OrbitCameraController spin = cam.ctrl;
        const float dyaw = 6.2831853f * static_cast<float>(frame) / orbit_total_frames;
        spin.setYawPitch(cam.ctrl.getYaw() + dyaw, cam.ctrl.getPitch());
        eye  = spin.getEye();
        look = spin.getTarget();
        view = spin.getViewMatrix();
      } else {
        eye  = cam.ctrl.getEye();
        look = cam.ctrl.getTarget();
        view = cam.ctrl.getViewMatrix();
      }
      // 与编辑器投影一致(fov 45 / near 0.1 / far 200000),保证同一相机下
      // 取景一致、远处楼宇不被近裁面/远裁面切掉。
      proj = cam.ctrl.getProjMatrix(static_cast<float>(cli.width) / cli.height,
                                    45.0f, 0.1f, 200000.0f);
    }
    if (offline) {
      t = static_cast<float>(frame) / cli.fps;  // 确定性时间轴(风/动画)
    }

    // 快照 env_settings(线程安全读;datachannel 线程可能在写)。
    hongtu::renderer::EnvironmentSettings es_snap;
    {
      std::lock_guard<std::mutex> lk(env_settings_mutex);
      es_snap = env_settings;
    }
    // tod 时刻推进:仅在『播放中』推进(tod_playing)。编辑器暂停昼夜时
    // tod_playing=false → 云端静止在 tod_hours,不再编辑器暂停而云端照动。
    if (es_snap.tod_enabled && es_snap.tod_playing && es_snap.tod_speed > 0.0f) {
      std::lock_guard<std::mutex> lk(env_settings_mutex);
      env_settings.tod_hours = std::fmod(
          env_settings.tod_hours + dt * es_snap.tod_speed * (24.0f / 60.0f), 24.0f);
      es_snap.tod_hours = env_settings.tod_hours;
    }

    ForwardSceneRenderer::FrameParams fp{};
    fp.view        = view;
    fp.proj        = proj;
    fp.eye_world   = eye;
    fp.bbox_center = {0.0f, 0.0f, 8.0f};
    fp.bbox_extent = 230.0f;
    fp.skybox_display_scale = 1.0f;

    // 统一用 applyEnvironment 从 env_settings 填所有光照/后处理参数。
    hongtu::renderer::applyEnvironment(es_snap, t, fp, fp.compose);

    // CRS 调试环境变量覆盖(取证/测试用)。
    if (std::getenv("CRS_NO_SHADOW")) fp.shadow_strength = 0.0f;
    if (std::getenv("CRS_NO_SSAO"))   { fp.ssao_strength = 0.0f; fp.compose.ao_strength = 0.0f; }
    if (std::getenv("CRS_NO_FXAA"))   { fp.fxaa_enabled = false; fp.compose.fxaa = false; }

    // CRS_NO_CULL:只首帧剔除(实例缓冲只写一次)—— 取证逐帧重写竞争。
    static const bool no_cull = std::getenv("CRS_NO_CULL") != nullptr;
    if (!no_cull || frame == 0) veg.update(proj * view, eye);
    VegetationPipeline::PushConstants vpc{};
    vpc.view_proj     = proj * view;
    vpc.light_vp      = renderer.getLightViewProj();
    vpc.camera_pos_ws = glm::vec4(eye, 0.0f);
    vpc.light_dir_ws  = glm::vec4(fp.sun_dir_world, fp.sun_intensity);
    vpc.env_params    = glm::vec4(fp.sky_scale, fp.ibl_diffuse_scale, fp.exposure, fp.shadow_strength);
    static const float lod_bias =
        std::getenv("CRS_LODBIAS") ? std::atof(std::getenv("CRS_LODBIAS")) : 0.0f;
    static const float veg_dbg = std::getenv("CRS_VEGDBG") ? 2.0f : 1.0f;
    // .w = night_factor:驱动自发光(路灯/演练要素夜间发亮);.z 仍是 lod_bias。
    vpc.env_params2   = glm::vec4(fp.skylight_bleed, veg_dbg, lod_bias, fp.night_factor);
    static const float wind_scale =
        std::getenv("CRS_WIND") ? std::atof(std::getenv("CRS_WIND")) : 0.5f;
    vpc.wind          = glm::vec4(glm::vec2(0.9f, 0.42f) * wind_scale, t, 1.7f);
    // billboard 朝向:从 view 矩阵取相机右/上向量(行向量)。
    {
      const glm::mat3 R = glm::transpose(glm::mat3(view));
      dyn.setCameraBasis(R[0], R[1]);
    }
    const glm::mat4 view_proj = proj * view;
    dyn.tickAvatars(std::min(dt, 0.05f));  // 人员行走动画推进(夹 dt)
    // 每帧重组 entities:ground(若有)+ 可见模型(读当前 model_matrix,动态接口
    // 可改)。几何 prims 静态,只 EntityDraw 列表随显隐/变换变。
    entities.clear();
    if (has_ground_entity) entities.push_back(ground_entity);
    for (const auto& lm : loaded_models) {
      if (!lm.visible) continue;
      ForwardSceneRenderer::EntityDraw ed;
      ed.model       = lm.model_matrix;
      ed.color_tint  = glm::vec4(1.0f);
      ed.prim_offset = lm.draw_prim_base;
      ed.prim_count  = lm.prim_count;
      entities.push_back(ed);
    }
    renderer.setExternalHdrDraw([&](const RHICommandBufferPtr& cmd) {
      veg.record(cmd, vpc, renderer.getShadowDescSet(), renderer.getIblDescriptorSet());
      dyn.record(cmd, view_proj, eye, t);  // 运行时标绘
      dyn.recordModels(cmd, vpc, renderer.getShadowDescSet(), view_proj, eye,
                       renderer.getIblDescriptorSet());  // 动态模型(共享程序化 IBL)
      dyn.recordFires(cmd, view_proj, std::min(dt, 0.05f));  // 动态火苗(夹 dt 防大步长)
      dyn.recordAvatars(cmd, view_proj, fp.sun_dir_world, glm::vec3(1.0f),
                        fp.sun_intensity);  // 人员骨骼(CesiumMan walk)
    });

    const bool capturing = offline || hub.hasViewer();
    renderer.setCaptureEnabled(capturing);  // 本帧 compose 后、present 前拷贝
    renderer.renderFrame(fp, vbo, ibo, materials, entities, prims);
    ++frame;

    // 有观众才走 捕获+编码(无人观看时纯渲染,CPU 占用低)。
    // 注意:捕获用 FSR 的帧内 capture(present 前拷贝),不是事后
    // readbackSwapchainImage —— 后者在 MoltenVK 上间歇读到压缩 tile
    // 垃圾,流里表现为闪烁黑块(实测取证排查到这里)。
    if (capturing) {
      device->waitIdle();
      uint32_t W = 0, H = 0;
      if (device->readCapturedFrame(rgba, W, H) &&
          W == static_cast<uint32_t>(cli.width) && H == static_cast<uint32_t>(cli.height)) {
        // 取证:CRS_DUMP_BLACK=1 时统计源帧纯黑像素,异常帧落盘 ——
        // 用来区分"渲染端黑块"还是"编码/传输损坏"。
        static const bool dump_black = std::getenv("CRS_DUMP_BLACK") != nullptr;
        if (dump_black) {
          size_t black = 0;
          for (size_t i = 0; i < rgba.size(); i += 4) {
            if (rgba[i] < 12 && rgba[i + 1] < 12 && rgba[i + 2] < 12) ++black;
          }
          static size_t dumped = 0;
          if (black > 800 && dumped < 8) {
            char name[64];
            std::snprintf(name, sizeof(name), "/tmp/crs_black_f%llu_%zu.png",
                          static_cast<unsigned long long>(frame), black);
            stbi_write_png(name, static_cast<int>(W), static_cast<int>(H), 4,
                           rgba.data(), static_cast<int>(W) * 4);
            spdlog::warn("[dump] source frame has {} black px → {}", black, name);
            ++dumped;
          }
        }
        bgra.resize(rgba.size());
        const size_t px = rgba.size() / 4;
        for (size_t i = 0; i < px; ++i) {  // RGBA → BGRA
          bgra[i * 4 + 0] = rgba[i * 4 + 2];
          bgra[i * 4 + 1] = rgba[i * 4 + 1];
          bgra[i * 4 + 2] = rgba[i * 4 + 0];
          bgra[i * 4 + 3] = rgba[i * 4 + 3];
        }
        if (offline) {
          if (orbit_total_frames > 0) {
            mp4.appendBGRA(bgra.data(), static_cast<double>(frame - 1) / cli.fps);
            if (frame % (static_cast<uint64_t>(cli.fps) * 4) == 0) {
              spdlog::info("[orbit] {}/{} frames", frame, orbit_total_frames);
            }
            if (static_cast<int>(frame) >= orbit_total_frames) {
              running = false;
              if (mp4.finalize()) {
                spdlog::info("[orbit] wrote {}", cli.orbit_video);
              }
            }
          }
          if (!cli.screenshot.empty() &&
              static_cast<int>(frame) >= cli.shot_frames) {
            if (stbi_write_png(cli.screenshot.c_str(), static_cast<int>(W),
                               static_cast<int>(H), 4, rgba.data(),
                               static_cast<int>(W) * 4)) {
              spdlog::info("[screenshot] wrote {} ({}x{})", cli.screenshot, W, H);
            }
            if (cli.orbit_video.empty()) running = false;
          }
        } else {
          encoder.encodeBGRA(bgra.data(), static_cast<uint64_t>(t * 90000.0));
        }
      }
    }

    if (frame % (static_cast<uint64_t>(cli.fps) * 10) == 0) {
      float yaw, pitch, dist;
      {
        std::lock_guard<std::mutex> lock(cam.m);
        yaw = cam.ctrl.getYaw(); pitch = cam.ctrl.getPitch(); dist = cam.ctrl.getDistance();
      }
      spdlog::info("[loop] frame={} viewers={} cam yaw={:.2f} pitch={:.2f} dist={:.1f}",
                   frame, hub.viewerCount(), yaw, pitch, dist);
    }

    // 帧率节拍(无 vsync,睡到下个编码节拍);离线全速渲。
    if (offline) continue;
    next_deadline += frame_dt;
    const double now_s = static_cast<double>(SDL_GetPerformanceCounter()) / perf_hz;
    const double sleep_s = next_deadline - now_s;
    if (sleep_s > 0.001) SDL_Delay(static_cast<uint32_t>(sleep_s * 1000.0));
    else if (sleep_s < -0.25) next_deadline = now_s;  // 掉帧太多就重置节拍
  }

  http.stop();
  encoder.shutdown();
  device->waitIdle();
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
