// disaster_demo — 二期 2.5 集成 demo
//
// 演示二期事故灾害可视化的 6 种效果。
//   1 = 池火(fire 粒子源,大半径)
//   2 = 蒸汽云爆炸(火焰 + 烟雾 + 冲击波 — 冲击波待 2.4 land)
//   3 = 喷射火(jet 粒子,圆锥定向)
//   4 = 毒气扩散(高斯羽流 volumetric — 待 2.3 land)
//   5 = 室内烟雾(smoke 粒子源 + 风偏)
//   6 = 组合演示(1 + 2 + 5 叠加)
//
// CLI:
//   disaster_demo --mode=local|global --effect=1..6 [--frames=N --screenshot=PATH]
//
// 当前 build:effects 1/3/5(particle-only)已 working;2/4/6 用 fire 粒子
// 占位,等 2.3 体积渲染 + 2.4 冲击波 land 后替换。

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <SDL.h>
#include <SDL_vulkan.h>

#include <hongtu/core/common.hpp>
#include <hongtu/coordinate/dispersion/gaussian_plume.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/renderer/camera/local_camera.hpp>
#include <hongtu/renderer/effects/particle_renderer.hpp>
#include <hongtu/renderer/effects/particle_system.hpp>
#include <hongtu/renderer/effects/volumetric_pass.hpp>
#include <hongtu/renderer/globe/globe_camera.hpp>
#include <hongtu/renderer/pbr/hdr_compose_target.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <hongtu/rhi/RHITypes.h>
#include <hongtu/renderer/effects/shockwave_pass.hpp>
#include <hongtu/scene/emitter_entity.hpp>
#include <hongtu/scene/shockwave_entity.hpp>
#include <hongtu/scene/volumetric_entity.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace HongTu::RHI;
using hongtu::coordinate::Geodetic;
using hongtu::coordinate::PrecisionOrigin;
using hongtu::renderer::CameraBase;
using hongtu::renderer::EmitterDesc;
using hongtu::renderer::EmitterPresets::makeFire;
using hongtu::renderer::EmitterPresets::makeJet;
using hongtu::renderer::EmitterPresets::makeSmoke;
using hongtu::renderer::EmitterPresets::makeSparks;
using hongtu::renderer::GlobeCamera;
using hongtu::renderer::LocalCamera;
using hongtu::renderer::ParticleInstance;
using hongtu::renderer::ParticleRenderer;
using hongtu::renderer::ParticleSystem;
using hongtu::renderer::HdrComposeTarget;
using hongtu::renderer::ShockwavePass;
using hongtu::renderer::VolumetricPass;
using hongtu::scene::EmitterEntity;
using hongtu::scene::GeoEmitter;
using hongtu::scene::GeoShockwave;
using hongtu::scene::GeoVolumetric;
using hongtu::scene::LocalEmitter;
using hongtu::scene::LocalShockwave;
using hongtu::scene::LocalVolumetric;
using hongtu::scene::ShockwaveEntity;
using hongtu::scene::VolumetricEntity;
namespace dispersion = hongtu::coordinate::dispersion;

namespace {

struct Cli {
  std::string mode       = "local";
  int         effect     = 1;
  int         frames     = 0;
  int         stress     = 0;
  bool        use_gpu    = false;     // --gpu opt-in: 走 ParticleSystemGPU compute
  std::string screenshot;
};

bool parseCli(int argc, char** argv, Cli& cli) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if      (a.rfind("--mode=", 0)       == 0) cli.mode       = a.substr(7);
    else if (a.rfind("--effect=", 0)     == 0) cli.effect     = std::stoi(a.substr(9));
    else if (a.rfind("--frames=", 0)     == 0) cli.frames     = std::stoi(a.substr(9));
    else if (a.rfind("--stress=", 0)     == 0) cli.stress     = std::stoi(a.substr(9));
    else if (a == "--gpu")                     cli.use_gpu    = true;
    else if (a.rfind("--screenshot=", 0) == 0) cli.screenshot = a.substr(13);
    else if (a == "--help" || a == "-h") {
      std::cout << "disaster_demo --mode=local|global --effect=1..6\n"
                << "              [--frames=N --screenshot=PATH] [--stress=N]\n"
                << "  1 = pool fire, 2 = vapor cloud explosion (TODO),\n"
                << "  3 = jet fire,  4 = toxic dispersion (TODO),\n"
                << "  5 = indoor smoke, 6 = composite\n";
      return false;
    } else {
      std::cerr << "Unknown arg: " << a << "\n";
      return false;
    }
  }
  if (cli.mode != "local" && cli.mode != "global") {
    std::cerr << "--mode must be local or global\n";
    return false;
  }
  if (cli.effect < 1 || cli.effect > 6) {
    std::cerr << "--effect must be 1..6\n";
    return false;
  }
  return true;
}

// Tian'anmen for global mode.
constexpr double kAnchorLatDeg = 39.9042;
constexpr double kAnchorLonDeg = 116.4074;

// Per-effect emitter spec: which preset(s) + offset relative to scene origin.
struct EmitterSpec {
  enum class Kind { Fire, Smoke, Sparks, Jet };
  Kind        kind;
  glm::dvec3  offset {0, 0, 0};      // world-space offset
  float       emit_rate = 100.0f;
  glm::vec3   jet_dir {0, 0, 1};     // jet only
  glm::vec3   wind   {0.5f, 0.2f, 0}; // smoke only
};

std::vector<EmitterSpec> emittersForEffect(int effect, int stress) {
  // Stress mode bumps emit_rate × 100 for one fire spec.
  auto bump = [stress](float r) -> float {
    return stress > 0 ? std::min(50000.0f, r * static_cast<float>(stress) / 100.0f) : r;
  };
  switch (effect) {
    case 1:   // 池火 — 大半径稳定燃烧
      return {{EmitterSpec::Kind::Fire,  { 0,  0, 0}, bump(150.0f)}};
    case 2: { // 蒸汽云爆炸 — 火 + 大量火星(冲击波 TODO,待 2.4)
      std::vector<EmitterSpec> v;
      v.push_back({EmitterSpec::Kind::Fire,   { 0,  0, 0}, bump(200.0f)});
      EmitterSpec sparks{EmitterSpec::Kind::Sparks, { 0,  0, 0.5}, bump(300.0f)};
      v.push_back(sparks);
      return v;
    }
    case 3:   // 喷射火 — 水平方向喷射
      return {{EmitterSpec::Kind::Jet, { 0, 0, 1.0}, bump(200.0f), {1, 0, 0.2f}}};
    case 4:   // 毒气扩散 — 临时用稀疏 smoke 占位(待 2.3 高斯羽流 land)
      return {{EmitterSpec::Kind::Smoke, { 0, 0, 1.0}, bump(60.0f), {0, 0, 0}, {2.0f, 0, 0}}};
    case 5:   // 室内烟雾
      return {{EmitterSpec::Kind::Smoke, { 0, 0, 0.5}, bump(80.0f), {0, 0, 0}, {0.5f, 0.3f, 0}}};
    case 6: { // 组合 — 火 + 烟 + 火星
      std::vector<EmitterSpec> v;
      v.push_back({EmitterSpec::Kind::Fire,   {-2,  0, 0}, bump(120.0f)});
      EmitterSpec smoke{EmitterSpec::Kind::Smoke,  { 0,  0, 1.0}, bump(50.0f), {0, 0, 0}, {0.5f, 0.5f, 0}};
      v.push_back(smoke);
      v.push_back({EmitterSpec::Kind::Sparks, { 2,  0, 0}, bump(180.0f)});
      return v;
    }
  }
  return {};
}

EmitterDesc descForSpec(const EmitterSpec& s) {
  glm::vec3 src{static_cast<float>(s.offset.x),
                static_cast<float>(s.offset.y),
                static_cast<float>(s.offset.z)};
  switch (s.kind) {
    case EmitterSpec::Kind::Fire:   return makeFire(src, s.emit_rate);
    case EmitterSpec::Kind::Smoke:  return makeSmoke(src, s.wind, s.emit_rate);
    case EmitterSpec::Kind::Sparks: return makeSparks(src, s.emit_rate);
    case EmitterSpec::Kind::Jet:    return makeJet(src, s.jet_dir, s.emit_rate);
  }
  return makeFire(src, s.emit_rate);
}

// ============================================================================
// DisasterDemo — owns SDL window + RHI + per-effect particle systems.
// ============================================================================

class DisasterDemo {
 public:
  explicit DisasterDemo(Cli cli) : cli_(std::move(cli)) {}

  bool initialize();
  void run();
  void shutdown();

 private:
  bool initWindow();
  bool initVulkan();
  bool createRenderPass();
  bool createDepthAndFramebuffers();
  bool initRenderers();
  void buildScene();
  void renderFrame();

  Cli              cli_;
  int              window_w_ = 1280, window_h_ = 720;
  SDL_Window*      window_ = nullptr;
  RHIDevice*       device_ = nullptr;
  // HDR scene pass — particles + volume + shockwave 都画进这一个 HDR
  // target,完成后由 hdr_compose_ tonemap + 可选 distortion 到 swapchain
  RHIRenderPassPtr scene_render_pass_;
  RHIFramebufferPtr scene_fb_;
  RHITexturePtr    hdr_color_tex_;
  RHITextureViewPtr hdr_color_view_;
  RHITexturePtr    depth_tex_;
  RHITextureViewPtr depth_view_;
  RHICommandBufferPtr cmd_;
  RHISemaphorePtr  image_avail_, render_done_;
  RHIFencePtr      fence_;
  uint32_t         image_index_ = 0;

  std::unique_ptr<CameraBase> camera_;
  PrecisionOrigin  origin_;

  // 每个 EmitterSpec 对应一个 ParticleSystem(CPU) 或 ParticleSystemGPU(--gpu)
  // 加一个 EmitterEntity。两路只走一路:gpu_system 非空时走 compute,否则
  // 走 CPU。emitter spec 完全共享(initial_color/lifetime/size_growth 等
  // 已经在 compute shader 里硬编码做 fire-ramp,只读 emit_rate/initial_velocity
  // /spawn shape)。
  struct Effect {
    std::unique_ptr<ParticleSystem>            system;       // CPU 路径
    std::unique_ptr<hongtu::renderer::ParticleSystemGPU> gpu_system;  // GPU 路径
    std::unique_ptr<EmitterEntity>             entity;
    EmitterSpec                                spec;
  };
  std::vector<Effect> effects_;

  // 体积场:每个 effect 可以加 0 或 1 个 — 用 GaussianPlume(2/4)或
  // 室内 smoke(5 备选)。entity 持几何中心(Geo/Local 一对),render-side
  // 持 3D 纹理 view 直接喂 VolumetricPass。
  struct VolumeEffect {
    std::unique_ptr<VolumetricEntity>     entity;
    VolumetricPass::VolumeTexture          tex;
    glm::vec3                              color {0.55f, 0.65f, 0.4f};
    float                                  sigma = 0.6f;
    float                                  light_intensity = 0.0f;  // HG 散射强度
    float                                  hg_g            = 0.4f;  // HG 各向异性
  };
  std::vector<VolumeEffect> volumes_;

  // 冲击波 — 时间驱动的 expanding sphere shell
  std::vector<std::unique_ptr<ShockwaveEntity>> shockwaves_;
  float                              now_sec_ = 0.0f;

  std::unique_ptr<ParticleRenderer> particle_renderer_;
  std::unique_ptr<VolumetricPass>   volumetric_pass_;
  std::unique_ptr<ShockwavePass>    shockwave_pass_;
  std::unique_ptr<HdrComposeTarget> hdr_compose_;

  std::chrono::steady_clock::time_point last_tick_;
};

bool DisasterDemo::initialize() {
  if (!initWindow())                 { std::cerr << "[disaster] initWindow failed\n"; return false; }
  if (!initVulkan())                 { std::cerr << "[disaster] initVulkan failed\n"; return false; }
  if (!createRenderPass())           { std::cerr << "[disaster] createRenderPass failed\n"; return false; }
  if (!createDepthAndFramebuffers()) { std::cerr << "[disaster] createDepth+fb failed\n"; return false; }
  if (!initRenderers())              { std::cerr << "[disaster] initRenderers failed\n"; return false; }
  buildScene();
  return true;
}

bool DisasterDemo::initWindow() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::cerr << "SDL_Init: " << SDL_GetError() << "\n";
    return false;
  }
  std::string title = "XNBY Disaster Demo (--mode=" + cli_.mode + " --effect=" + std::to_string(cli_.effect) + ")";
  window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, window_w_, window_h_,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
  return window_ != nullptr;
}

bool DisasterDemo::initVulkan() {
  auto factory = Backend::createFactory(Backend::Type::Vulkan);
  if (!factory) return false;
  unsigned int ec = 0;
  SDL_Vulkan_GetInstanceExtensions(window_, &ec, nullptr);
  std::vector<const char*> exts(ec);
  SDL_Vulkan_GetInstanceExtensions(window_, &ec, exts.data());

  SwapchainDesc sc;
  sc.extent     = {static_cast<uint32_t>(window_w_), static_cast<uint32_t>(window_h_)};
  sc.format     = Format::R8G8B8A8_SRGB;
  sc.imageCount = 3;
  sc.vsync      = true;

  device_ = factory->createDeviceWithSwapchain(window_, sc, false, exts);
  Backend::destroyFactory(factory);
  if (!device_) return false;

  cmd_         = device_->createCommandBuffer();
  image_avail_ = device_->createSemaphore();
  render_done_ = device_->createSemaphore();
  fence_       = device_->createFence(true);
  return cmd_ && image_avail_ && render_done_ && fence_;
}

bool DisasterDemo::createRenderPass() {
  // HDR 中间通路 — 色彩走 R16G16B16A16_SFLOAT,final layout 是
  // ShaderReadOnlyOptimal,让后面 HdrComposeTarget 把它当 sampler2D 用。
  RenderPassDesc rpd;

  AttachmentDescription ca;
  ca.format        = Format::R16G16B16A16_SFLOAT;       // HdrComposeTarget::getHdrFormat()
  ca.sampleCount   = 1;
  ca.initialLayout = ImageLayout::Undefined;
  ca.finalLayout   = ImageLayout::ShaderReadOnlyOptimal;
  ca.loadOpClear   = true;
  ca.storeOpStore  = true;
  rpd.attachments.push_back(ca);

  AttachmentDescription da;
  da.format        = Format::D32_SFLOAT;
  da.sampleCount   = 1;
  da.initialLayout = ImageLayout::Undefined;
  da.finalLayout   = ImageLayout::DepthStencilAttachmentOptimal;
  da.loadOpClear   = true;
  da.storeOpStore  = false;
  rpd.attachments.push_back(da);

  SubpassDescription sp;
  AttachmentReference cr;
  cr.attachment = 0;
  cr.layout     = ImageLayout::ColorAttachmentOptimal;
  sp.colorAttachments.push_back(cr);
  sp.depthStencilAttachment.attachment = 1;
  sp.depthStencilAttachment.layout     = ImageLayout::DepthStencilAttachmentOptimal;
  sp.hasDepthStencil = true;
  rpd.subpasses.push_back(sp);

  scene_render_pass_ = device_->createRenderPass(rpd);
  return scene_render_pass_ != nullptr;
}

bool DisasterDemo::createDepthAndFramebuffers() {
  // HDR color attachment(R16F)— 整帧只一个,因为我们画到 HDR target 而不是
  // per-swapchain image。HdrComposeTarget 之后再 sample 到 swapchain。
  TextureDesc hc;
  hc.type        = TextureType::Texture2D;
  hc.extent      = {static_cast<uint32_t>(window_w_),
                    static_cast<uint32_t>(window_h_), 1u};
  hc.mipLevels   = 1u;
  hc.arrayLayers = 1u;
  hc.format      = Format::R16G16B16A16_SFLOAT;
  hc.usage       = TextureUsageFlagBits::ColorAttachment
                 | TextureUsageFlagBits::Sampled;
  hc.sampleCount = 1u;
  hdr_color_tex_  = device_->createTexture(hc);
  if (!hdr_color_tex_) return false;
  hdr_color_view_ = hdr_color_tex_->createView();
  if (!hdr_color_view_) return false;

  TextureDesc dd;
  dd.type        = TextureType::Texture2D;
  dd.extent      = {static_cast<uint32_t>(window_w_),
                    static_cast<uint32_t>(window_h_), 1u};
  dd.mipLevels   = 1u;
  dd.arrayLayers = 1u;
  dd.format      = Format::D32_SFLOAT;
  dd.usage       = static_cast<TextureUsageFlags>(
                       TextureUsageFlagBits::DepthStencilAttachment);
  dd.sampleCount = 1u;
  depth_tex_  = device_->createTexture(dd);
  if (!depth_tex_) return false;
  depth_view_ = depth_tex_->createView();
  if (!depth_view_) return false;

  scene_fb_ = device_->createFramebuffer(
      scene_render_pass_, {hdr_color_view_, depth_view_},
      device_->getSwapchainExtent(), 1);
  return scene_fb_ != nullptr;
}

bool DisasterDemo::initRenderers() {
  // HdrComposeTarget 内部建自己的 HDR target + compose render pass +
  // 每 swapchain image 一个 compose framebuffer。我们这里只是 sample
  // 它内部的 HDR view 来当 source — 不,反过来,我们自己持 HDR target,
  // 把 view 通过 SetHdrSource(...) 不,HdrComposeTarget API 把自己内部
  // HDR view 当 source。要让它 sample 我们的 HDR view,要么改 API,要么
  // 复用它内部的 HDR view 作为我们 scene_render_pass 的 color attachment。
  //
  // 走"复用内部 HDR view"路线更干净:HdrComposeTarget.initialize() 后
  // 用 getHdrView() 作为 scene_fb_ 的 color attachment。但这要求
  // createDepthAndFramebuffers 在 HdrComposeTarget 之后跑 — 调整顺序。
  hdr_compose_ = std::make_unique<HdrComposeTarget>();
  if (!hdr_compose_->initialize(device_)) return false;
  // 重建 scene framebuffer,用 HdrComposeTarget 内部的 HDR view 作为 attachment
  scene_fb_ = device_->createFramebuffer(
      scene_render_pass_, {hdr_compose_->getHdrView(), depth_view_},
      device_->getSwapchainExtent(), 1);
  if (!scene_fb_) return false;
  // 释放我们自己刚才建的(没用上)HDR target
  hdr_color_view_.reset();
  hdr_color_tex_.reset();

  particle_renderer_ = std::make_unique<ParticleRenderer>();
  if (!particle_renderer_->initialize(device_, scene_render_pass_)) return false;
  volumetric_pass_ = std::make_unique<VolumetricPass>();
  if (!volumetric_pass_->initialize(device_, scene_render_pass_)) return false;
  shockwave_pass_ = std::make_unique<ShockwavePass>();
  if (!shockwave_pass_->initialize(device_, scene_render_pass_)) return false;
  return true;
}

void DisasterDemo::buildScene() {
  // Camera + origin.
  if (cli_.mode == "local") {
    origin_.setOrigin({0, 0, 0});
    auto c = std::make_unique<LocalCamera>();
    c->setScreenSize(static_cast<float>(window_w_), static_cast<float>(window_h_));
    c->lookAtTarget({0, 0, 1.5}, /*distance=*/12.0,
                    /*yaw=*/0.5, /*pitch=*/-0.4);
    camera_ = std::move(c);
  } else {
    Geodetic anchor = Geodetic::fromDegrees(kAnchorLatDeg, kAnchorLonDeg, 0.0);
    origin_.setOriginGeodetic(anchor);
    auto c = std::make_unique<GlobeCamera>();
    c->setScreenSize(static_cast<float>(window_w_), static_cast<float>(window_h_));
    c->setMinDistance(5.0);
    c->lookAt(anchor, /*distance=*/15.0, /*heading=*/30.0, /*pitch=*/-25.0);
    camera_ = std::move(c);
  }

  // Build particle systems for the selected effect.
  effects_.clear();
  for (auto& spec : emittersForEffect(cli_.effect, cli_.stress)) {
    Effect e;
    e.spec = spec;
    const uint32_t cap = (cli_.stress > 0) ? 50000u : 2048u;
    if (cli_.use_gpu) {
      e.gpu_system = std::make_unique<hongtu::renderer::ParticleSystemGPU>();
      if (!e.gpu_system->initialize(device_, cap)) {
        std::cerr << "ParticleSystemGPU init failed; fallback to CPU\n";
        e.gpu_system.reset();
        e.system = std::make_unique<ParticleSystem>(cap);
      }
    } else {
      e.system = std::make_unique<ParticleSystem>(cap);
    }

    // entity 持地图位置;每帧 lower 到 RTC 后塞进 desc.center。
    if (cli_.mode == "local") {
      auto le = std::make_unique<LocalEmitter>();
      le->setPosition(spec.offset);
      e.entity = std::move(le);
    } else {
      Geodetic anchor = Geodetic::fromDegrees(kAnchorLatDeg, kAnchorLonDeg, 0.0);
      // 把 local offset(meters)按 ENU 平面铺到地理坐标:
      // 每米经度 ≈ 1 / (111320 * cos(lat)) 度,每米纬度 ≈ 1 / 111320 度。
      double cos_lat = std::cos(anchor.lat);
      double dlat_deg = spec.offset.y / 111320.0;
      double dlon_deg = spec.offset.x / (111320.0 * std::max(1e-4, cos_lat));
      double lat_deg, lon_deg;
      anchor.toDegrees(lat_deg, lon_deg);
      Geodetic g = Geodetic::fromDegrees(lat_deg + dlat_deg,
                                         lon_deg + dlon_deg,
                                         anchor.alt + spec.offset.z);
      auto ge = std::make_unique<GeoEmitter>();
      ge->setPosition(g);
      e.entity = std::move(ge);
    }
    e.entity->setDesc(descForSpec(spec));
    effects_.push_back(std::move(e));
  }

  // Effects 2 / 4 / 6 加 volumetric:
  //   effect 4 = 毒气扩散 — Gaussian Plume(下风向羽流)
  //   effect 2 = 蒸汽云爆炸 — 室内 smoke + 火粒子(粗 vapor cloud)
  //   effect 6 = 组合演示 — 也叠一片烟雾增加视觉层次
  auto buildVolume = [&](glm::vec3 color, float sigma, glm::vec3 ext_m,
                         dispersion::DensityGrid&& grid,
                         glm::dvec3 center_offset,
                         float light_intensity = 0.0f,
                         float hg_g = 0.4f) {
    VolumeEffect ve;
    ve.color = color;
    ve.sigma = sigma;
    ve.light_intensity = light_intensity;
    ve.hg_g            = hg_g;
    auto shared = std::make_shared<dispersion::DensityGrid>(std::move(grid));
    if (cli_.mode == "local") {
      auto lv = std::make_unique<LocalVolumetric>();
      lv->setCenter(center_offset);
      lv->setExtentMeters(ext_m);
      lv->setDensityGrid(shared);
      lv->setColor(color);
      ve.entity = std::move(lv);
    } else {
      Geodetic anchor = Geodetic::fromDegrees(kAnchorLatDeg, kAnchorLonDeg, 0.0);
      double cos_lat = std::cos(anchor.lat);
      double dlat_deg = center_offset.y / 111320.0;
      double dlon_deg = center_offset.x / (111320.0 * std::max(1e-4, cos_lat));
      double lat_deg, lon_deg;
      anchor.toDegrees(lat_deg, lon_deg);
      Geodetic g = Geodetic::fromDegrees(lat_deg + dlat_deg, lon_deg + dlon_deg,
                                         anchor.alt + center_offset.z);
      auto gv = std::make_unique<GeoVolumetric>();
      gv->setCenter(g);
      gv->setExtentMeters(ext_m);
      gv->setDensityGrid(shared);
      gv->setColor(color);
      ve.entity = std::move(gv);
    }
    ve.tex = volumetric_pass_->uploadDensity(*shared);
    volumes_.push_back(std::move(ve));
  };

  if (cli_.effect == 4) {
    // 毒气扩散:下风向 Gaussian Plume,AABB 框住下风 50m / 横向 ±12m。
    // 中心在源点下风 25m 处,半 extent 25/12/3。HG 强前向散射 g=0.6,
    // 让羽流"透"过太阳光 — 朝向光源的一侧明显更亮。
    dispersion::GaussianPlumeConfig cfg;
    cfg.source        = {0, 0, 1.0};
    cfg.wind          = {2.0, 0, 0};
    cfg.stability     = dispersion::PasquillStability::D;
    cfg.emission_rate = 50.0;
    auto grid = dispersion::buildGaussianPlumeGrid(
        cfg, /*aabb_min=*/{0, -12, 0}, /*aabb_max=*/{50, 12, 6},
        /*w=*/64, /*h=*/32, /*d=*/16);
    buildVolume(/*color=*/{0.45f, 0.65f, 0.30f}, /*sigma=*/0.5f,
                /*ext_m=*/{25.0f, 12.0f, 3.0f},
                std::move(grid),
                /*center_offset=*/{25.0, 0.0, 3.0},
                /*light_intensity=*/1.5f,
                /*hg_g=*/0.6f);
  } else if (cli_.effect == 2 || cli_.effect == 6) {
    // 蒸汽云 / 组合:头顶一团 smoke volume。HG 中等前向散射 g=0.35,
    // 加 0.8 intensity — 让烟云有"被太阳照到"的方向感而不是均匀灰球。
    auto grid = dispersion::buildRoomSmokeGrid(
        /*source=*/{4, 4, 4},
        /*aabb_min=*/{0, 0, 0}, /*aabb_max=*/{8, 8, 6},
        /*w=*/32, /*h=*/32, /*d=*/24,
        /*iterations=*/40);
    buildVolume(/*color=*/{0.5f, 0.45f, 0.42f}, /*sigma=*/0.8f,
                /*ext_m=*/{4.0f, 4.0f, 3.0f},
                std::move(grid),
                /*center_offset=*/{0.0, 0.0, 3.0},
                /*light_intensity=*/0.8f,
                /*hg_g=*/0.35f);
  }

  // Effect 2 蒸汽云爆炸 — 加一个 expanding shockwave shell
  if (cli_.effect == 2) {
    std::unique_ptr<ShockwaveEntity> sw;
    if (cli_.mode == "local") {
      auto ls = std::make_unique<LocalShockwave>();
      ls->setCenter({0, 0, 1.0});
      sw = std::move(ls);
    } else {
      Geodetic anchor = Geodetic::fromDegrees(kAnchorLatDeg, kAnchorLonDeg, 0.0);
      auto gs = std::make_unique<GeoShockwave>();
      gs->setCenter(anchor);
      sw = std::move(gs);
    }
    // 触发时间略晚一点(让蒸汽云先积累),然后 3 m/s 扩到 4m;
    // 20 帧 ctest 截图能看到 ~0.7m 的初期环;120 帧 demo 能看到
    // 完全展开的 ~4m 环。
    sw->setStartTimeSec(0.1f);
    sw->setSpeedMps(3.0f);
    sw->setMaxRadiusMeters(4.0f);
    sw->setThicknessMeters(0.3f);
    sw->setColor({1.0f, 0.85f, 0.6f});
    sw->setIntensity(3.0f);
    sw->setChromaOffset(0.15f);   // 给点色散
    shockwaves_.push_back(std::move(sw));
  }

  last_tick_ = std::chrono::steady_clock::now();
  now_sec_   = 0.0f;
}

void DisasterDemo::renderFrame() {
  // 1. dt 计算 — screenshot regression 模式用固定 dt = 1/60s,确保多机
  //   一致(否则在快机器上 dt ≈ 0,粒子来不及累积成可见)。
  float dt;
  if (cli_.frames > 0) {
    dt = 1.0f / 60.0f;
  } else {
    auto now = std::chrono::steady_clock::now();
    dt = std::chrono::duration<float>(now - last_tick_).count();
    last_tick_ = now;
    if (dt > 0.1f) dt = 0.1f;
  }

  // 2. acquire + sync
  fence_->wait();
  fence_->reset();
  image_index_ = device_->acquireNextImage(image_avail_);

  // 3. 推进所有粒子系统(CPU 拓 instance buffer / GPU 留给 step 7 compute)
  std::vector<ParticleInstance> all_instances;
  for (auto& e : effects_) {
    glm::vec3 src = e.entity->getRTCPosition(origin_);
    EmitterDesc d = e.entity->getDesc();
    d.center = src;
    if (e.gpu_system) {
      e.gpu_system->setEmitter(d);
      // GPU compute 在 step 7 record pass 之前 dispatch — 这里只更新 spec
    } else {
      e.system->setEmitter(d);
      e.system->update(dt);
      std::vector<ParticleInstance> insts;
      e.system->buildInstanceBuffer(insts);
      all_instances.insert(all_instances.end(), insts.begin(), insts.end());
    }
  }

  // 4. camera matrices
  float aspect = static_cast<float>(window_w_) / static_cast<float>(window_h_);
  glm::mat4 proj = camera_->getProjectionMatrix(aspect);
  glm::mat4 view = camera_->getRTCViewMatrix(origin_);
  glm::mat4 view_proj = proj * view;
  glm::mat4 view32(camera_->getViewMatrix());
  glm::vec3 cam_right(view32[0][0], view32[1][0], view32[2][0]);
  glm::vec3 cam_up   (view32[0][1], view32[1][1], view32[2][1]);

  // 5. record frame
  cmd_->reset();
  cmd_->begin();

  // 3b. GPU 粒子 compute dispatch — 必须在 render pass 之前(compute 不能
  // 在 graphics render pass 里跑)。barrier 写在 ParticleSystemGPU::recordUpdate
  // 内部:compute write → vertex read,scope=render_buffer。
  for (auto& e : effects_) {
    if (e.gpu_system) e.gpu_system->recordUpdate(cmd_, dt);
  }

  Rect2D area;
  area.offset = {0, 0};
  area.extent = device_->getSwapchainExtent();
  std::vector<std::array<float, 4>> clears = {
      {0.02f, 0.02f, 0.04f, 1.0f},   // near-black 背景突出粒子
      {1.0f, 0.0f, 0.0f, 0.0f}};
  cmd_->beginRenderPass(scene_render_pass_, scene_fb_, area, clears);

  Viewport vp;
  vp.x = 0; vp.y = 0;
  vp.width    = static_cast<float>(area.extent.width);
  vp.height   = static_cast<float>(area.extent.height);
  vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
  cmd_->setViewport(vp);
  cmd_->setScissor(area);

  ParticleRenderer::Params pp;
  pp.view_proj    = view_proj;
  pp.camera_right = cam_right;
  pp.camera_up    = cam_up;
  if (!all_instances.empty()) {
    particle_renderer_->record(cmd_, pp, all_instances);
  }
  // GPU emitter:每个 emitter 自己一份 32B/inst 的 render buffer。
  for (auto& e : effects_) {
    if (!e.gpu_system) continue;
    particle_renderer_->recordFromBuffer(cmd_, pp,
                                         e.gpu_system->getInstanceBuffer(),
                                         e.gpu_system->getCapacity(),
                                         sizeof(ParticleInstance));
  }

  // 体积场:粒子之后画(volumetric 不写深度,粒子 alpha-blend 在前
  // 看起来仍然透过 volumetric)。VolumetricPass 是 fullscreen-tri,
  // discard 在 AABB 之外的像素 → 多体积间互不干扰。
  glm::vec3 cam_pos_rtc = origin_.worldToLocal(camera_->getEyeWorld());
  for (auto& ve : volumes_) {
    if (!ve.tex.view) continue;
    glm::vec3 c   = ve.entity->getRTCCenter(origin_);
    glm::vec3 ext = ve.entity->getExtentMeters();
    VolumetricPass::Params vp;
    vp.view_proj       = view_proj;
    vp.camera_pos_rtc  = cam_pos_rtc;
    vp.aabb_min        = c - ext;
    vp.aabb_max        = c + ext;
    vp.color           = ve.color;
    vp.density_scale   = ve.entity->getDensityScale() * 3.0f;  // bump 显得更明显
    vp.sigma           = ve.sigma;
    vp.num_steps       = 64;
    volumetric_pass_->record(cmd_, vp, ve.tex.view);
  }

  // 冲击波 — additive blend,fullscreen-tri,在 volumetric 之后画(画最上面)
  now_sec_ += dt;
  for (auto& sw : shockwaves_) {
    float r = sw->getCurrentRadius(now_sec_);
    if (r <= 0.0f) continue;
    ShockwavePass::Params sp;
    sp.view_proj      = view_proj;
    sp.center_rtc     = sw->getRTCCenter(origin_);
    sp.radius_m       = r;
    sp.color          = sw->getColor();
    sp.intensity      = sw->getIntensity();
    sp.thickness_m    = sw->getThicknessMeters();
    sp.chroma_offset  = sw->getChromaOffset();
    shockwave_pass_->record(cmd_, sp);
  }

  cmd_->endRenderPass();

  // 把第一个活跃 shockwave 投到屏幕空间,作为 HdrComposeTarget 的
  // distortion 输入(简单 v1 — 只取一个);compose shader 会在 shell
  // 内沿 radial 把 HDR 采样 UV 推开 → 真"折射"效果
  float distortion = 0.0f;
  glm::vec2 sw_center_uv{0.5f};
  float sw_radius_uv = 0.0f, sw_thick_uv = 0.0f;
  for (auto& sw : shockwaves_) {
    float r = sw->getCurrentRadius(now_sec_);
    if (r <= 0.0f) continue;
    glm::vec3 c_rtc = sw->getRTCCenter(origin_);
    // project center + 2 个 thickness point 投到 NDC,差值给 radius_uv / thick
    glm::vec4 c_ndc = view_proj * glm::vec4(c_rtc, 1.0f);
    if (c_ndc.w <= 0.0f) continue;
    c_ndc /= c_ndc.w;
    glm::vec3 right_rtc = c_rtc
                        + glm::vec3(cam_right) * r;
    glm::vec4 r_ndc = view_proj * glm::vec4(right_rtc, 1.0f);
    if (r_ndc.w <= 0.0f) continue;
    r_ndc /= r_ndc.w;
    glm::vec2 c_uv(c_ndc.x * 0.5f + 0.5f, 1.0f - (c_ndc.y * 0.5f + 0.5f));
    glm::vec2 r_uv(r_ndc.x * 0.5f + 0.5f, 1.0f - (r_ndc.y * 0.5f + 0.5f));
    sw_center_uv = c_uv;
    sw_radius_uv = glm::length(r_uv - c_uv);
    // shell 厚度 = 3D thickness 转屏幕 — 用 thickness/radius 比例近似
    sw_thick_uv  = sw_radius_uv * (sw->getThicknessMeters() / std::max(0.1f, r));
    distortion   = 0.02f;   // 屏幕空间 UV 偏移强度(2% 屏幕宽度)
    break;
  }
  hdr_compose_->setShockwaveDistortion(sw_center_uv, sw_radius_uv, sw_thick_uv, distortion);

  // Heat haze:火 / 喷射火源 投到屏幕,作为 heat haze sources。粒子的
  // 烟雾 / 火星不算热源(emit_rate 大也不需要 shimmer)。简单按 kind 过滤。
  std::vector<glm::vec4> heat_sources;
  for (auto& e : effects_) {
    if (e.spec.kind != EmitterSpec::Kind::Fire &&
        e.spec.kind != EmitterSpec::Kind::Jet) continue;
    glm::vec3 src_rtc = e.entity->getRTCPosition(origin_);
    glm::vec4 src_ndc = view_proj * glm::vec4(src_rtc, 1.0f);
    if (src_ndc.w <= 0.0f) continue;
    src_ndc /= src_ndc.w;
    glm::vec2 src_uv(src_ndc.x * 0.5f + 0.5f, 1.0f - (src_ndc.y * 0.5f + 0.5f));
    // 屏幕空间 radius 用一个粗糙投影 — 火源附近 2m 半径的标准
    glm::vec4 r_ndc = view_proj * glm::vec4(src_rtc + glm::vec3(cam_right) * 2.0f, 1.0f);
    if (r_ndc.w <= 0.0f) continue;
    r_ndc /= r_ndc.w;
    glm::vec2 r_uv(r_ndc.x * 0.5f + 0.5f, 1.0f - (r_ndc.y * 0.5f + 0.5f));
    float radius_uv = glm::length(r_uv - src_uv);
    heat_sources.push_back({src_uv.x, src_uv.y, radius_uv, 1.0f});
    if (heat_sources.size() >= 4) break;
  }
  hdr_compose_->setHeatHazeSources(heat_sources, now_sec_,
                                    /*global_strength=*/heat_sources.empty() ? 0.0f : 1.0f);

  // Compose:HDR → tonemap → swapchain。FXAA / AO / Bloom 都关
  // (disaster_demo 用简单深色背景,这些 polish 暂时不需要)。
  hdr_compose_->recordCompose(cmd_, image_index_,
      /*tonemap_enabled=*/true,
      /*ao_strength=*/0.0f,
      /*bloom_intensity=*/0.0f,
      /*fxaa_enabled=*/false);

  cmd_->end();
  device_->submit({cmd_}, {image_avail_}, {render_done_}, fence_);
  device_->present(render_done_);
}

void DisasterDemo::run() {
  int frame_idx = 0;
  bool running = true;
  SDL_Event ev;
  std::cout << "[disaster_demo] mode=" << cli_.mode
            << " effect=" << cli_.effect
            << " stress=" << cli_.stress
            << " effects=" << effects_.size() << "\n";

  auto t0 = std::chrono::steady_clock::now();
  uint64_t total_alive = 0;
  while (running) {
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) running = false;
      else if (ev.type == SDL_KEYDOWN) {
        auto k = ev.key.keysym.sym;
        if (k == SDLK_ESCAPE) running = false;
        // 数字键 1..6 — 运行时切 effect:清掉当前 systems + volumes +
        // shockwaves,重新 buildScene 当前 effect。粒子池 / pipeline /
        // 3D 纹理 上传依然 GPU device 拥有,只是 entity 引用换一拨。
        else if (k >= SDLK_1 && k <= SDLK_6) {
          int new_effect = static_cast<int>(k - SDLK_1) + 1;
          if (new_effect != cli_.effect) {
            cli_.effect = new_effect;
            effects_.clear();
            volumes_.clear();
            shockwaves_.clear();
            now_sec_ = 0.0f;
            buildScene();
            std::cout << "[disaster] switched to effect=" << cli_.effect << "\n";
          }
        }
      }
    }
    renderFrame();
    ++frame_idx;
    for (auto& e : effects_) {
      if (e.system) total_alive += e.system->getAliveCount();
      // GPU 路径 alive count 在 GPU 里 — CPU 侧只跟踪 capacity 作为近似上限。
      else if (e.gpu_system) total_alive += e.gpu_system->getCapacity();
    }

    if (cli_.frames > 0 && frame_idx >= cli_.frames) {
      if (!cli_.screenshot.empty()) {
        fence_->wait();
        std::vector<uint8_t> rgba;
        uint32_t W = 0, H = 0;
        if (device_->readbackSwapchainImage(image_index_, rgba, W, H)) {
          if (stbi_write_png(cli_.screenshot.c_str(), W, H, 4, rgba.data(), W * 4) != 0) {
            std::cout << "[disaster] wrote " << cli_.screenshot << " (" << W << "x" << H << ")\n";
          } else {
            std::cerr << "[disaster] stbi_write_png failed\n";
          }
        } else {
          std::cerr << "[disaster] swapchain readback failed\n";
        }
      }
      running = false;
    }
  }

  auto t1 = std::chrono::steady_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
  std::cout << "[disaster] rendered " << frame_idx << " frames in " << ms << "ms"
            << " avg alive=" << (frame_idx > 0 ? total_alive / frame_idx : 0)
            << "\n";
  device_->waitIdle();
}

void DisasterDemo::shutdown() {
  if (device_) device_->waitIdle();
  effects_.clear();
  volumes_.clear();
  shockwaves_.clear();
  particle_renderer_.reset();
  volumetric_pass_.reset();
  shockwave_pass_.reset();
  cmd_.reset();
  image_avail_.reset();
  render_done_.reset();
  fence_.reset();
  hdr_compose_.reset();
  scene_fb_.reset();
  hdr_color_view_.reset();
  hdr_color_tex_.reset();
  depth_view_.reset();
  depth_tex_.reset();
  scene_render_pass_.reset();
  delete device_;
  device_ = nullptr;
  if (window_) SDL_DestroyWindow(window_);
  SDL_Quit();
}

}  // namespace

int main(int argc, char** argv) {
#ifdef __APPLE__
  if (!std::getenv("VK_ICD_FILENAMES")) {
    const char* icd = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json";
    if (std::ifstream(icd).good()) {
      setenv("VK_ICD_FILENAMES", icd, 0);
      setenv("VK_DRIVER_FILES",  icd, 0);
    }
  }
#endif

  Cli cli;
  if (!parseCli(argc, argv, cli)) return 1;

  DisasterDemo app(std::move(cli));
  if (!app.initialize()) {
    std::cerr << "[disaster_demo] initialize failed\n";
    return 1;
  }
  app.run();
  app.shutdown();
  return 0;
}
