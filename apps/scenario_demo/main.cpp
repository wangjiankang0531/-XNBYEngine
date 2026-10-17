// scenario_demo — phase 3.8 integration: AnnotationManager + DrawingTool +
// Timeline + CommandStack working together on a scripted scenario.
//
// Demonstrates dual-mode (local + global) parity: same JSON-driven scenario
// loaded from one file pair (leak_to_fire.json / leak_to_fire_local.json),
// rendered through PolylineRenderer + PolygonRenderer + BillboardRenderer.
// Timeline drives annotation visibility over a 45 s timeline.
//
// Usage:
//   scenario_demo --mode=local  --scenario=leak_to_fire
//   scenario_demo --mode=global --scenario=leak_to_fire
//   scenario_demo --mode=local  --scenario=leak_to_fire --time=15
//   scenario_demo --mode=global --scenario=leak_to_fire --time=25 \
//                  --frames=2 --screenshot=/tmp/s.png
//
// Hotkeys (interactive):
//   1..5 : switch DrawingTool (polyline / polygon / symbol / text / arrow)
//   Space: play/pause timeline
//   F5   : save annotations to /tmp/scenario_save.json
//   F9   : load annotations from /tmp/scenario_save.json
//   Ctrl+Z / Ctrl+Y : undo / redo
//   Esc  : quit

#include <SDL.h>
#include <SDL_vulkan.h>

#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/renderer/camera/local_camera.hpp>
#include <hongtu/renderer/effects/billboard_renderer.hpp>
#include <hongtu/renderer/effects/particle_renderer.hpp>
#include <hongtu/renderer/effects/particle_system.hpp>
#include <hongtu/renderer/effects/polygon_renderer.hpp>
#include <hongtu/renderer/effects/polyline_renderer.hpp>
#include <hongtu/renderer/effects/shockwave_pass.hpp>
#include <hongtu/renderer/effects/volumetric_pass.hpp>
#include <hongtu/coordinate/dispersion/gaussian_plume.hpp>
#include <hongtu/renderer/globe/globe_camera.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <hongtu/rhi/RHITypes.h>
#include <hongtu/scene/annotation/annotation_manager.hpp>
#include <hongtu/scene/annotation/arrow_annotation.hpp>
#include <hongtu/scene/annotation/command_stack.hpp>
#include <hongtu/scene/annotation/drawing_tool.hpp>
#include <hongtu/scene/picking/picking_service.hpp>
#include <hongtu/scene/annotation/polygon_annotation.hpp>
#include <hongtu/scene/annotation/polyline_annotation.hpp>
#include <hongtu/scene/annotation/symbol_annotation.hpp>
#include <hongtu/scene/annotation/text_annotation.hpp>
#include <hongtu/scene/emitter_entity.hpp>
#include <hongtu/scene/shockwave_entity.hpp>
#include <hongtu/scene/volumetric_entity.hpp>
#include <hongtu/scene/timeline/timeline.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace HongTu::RHI;
using hongtu::coordinate::Geodetic;
using hongtu::coordinate::PrecisionOrigin;
using hongtu::renderer::CameraBase;
using hongtu::renderer::GlobeCamera;
using hongtu::renderer::LocalCamera;
using hongtu::renderer::PolygonRenderer;
using hongtu::renderer::PolylineRenderer;
using hongtu::scene::AnnotationKind;
using hongtu::scene::AnnotationManager;
using hongtu::scene::AnnotationMode;
using hongtu::scene::AnnotationRenderPayload;
using hongtu::scene::ArrowTool;
using hongtu::scene::CommandStack;
using hongtu::scene::DrawingTool;
using hongtu::scene::PolygonTool;
using hongtu::scene::PolylineTool;
using hongtu::scene::SymbolTool;
using hongtu::scene::TextTool;
using hongtu::scene::Timeline;
using hongtu::scene::Track;
using hongtu::scene::CallbackDriver;

namespace {

struct Cli {
  std::string mode      = "local";
  std::string scenario  = "leak_to_fire";
  float       time      = -1.0f;   // -1 = play from t=0
  int         frames    = 0;
  std::string screenshot;
};

bool parseCli(int argc, char** argv, Cli& cli) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if      (a.rfind("--mode=",     0) == 0) cli.mode     = a.substr(7);
    else if (a.rfind("--scenario=", 0) == 0) cli.scenario = a.substr(11);
    else if (a.rfind("--time=",     0) == 0) cli.time     = std::stof(a.substr(7));
    else if (a.rfind("--frames=",   0) == 0) cli.frames   = std::stoi(a.substr(9));
    else if (a.rfind("--screenshot=", 0) == 0) cli.screenshot = a.substr(13);
    else if (a == "--help" || a == "-h") {
      std::cout << "scenario_demo --mode=local|global --scenario=NAME\n"
                << "              [--time=T] [--frames=N --screenshot=PATH]\n";
      return false;
    } else {
      std::cerr << "Unknown arg: " << a << "\n";
      return false;
    }
  }
  if (cli.mode != "local" && cli.mode != "global") {
    std::cerr << "--mode must be 'local' or 'global'\n";
    return false;
  }
  return true;
}

std::string scenarioPath(const std::string& name, const std::string& mode) {
  std::string suffix = (mode == "local") ? "_local.json" : ".json";
  return "data/scenarios/" + name + suffix;
}

bool readFile(const std::string& path, std::string& out) {
  std::ifstream f(path);
  if (!f.is_open()) return false;
  std::stringstream ss; ss << f.rdbuf();
  out = ss.str();
  return true;
}

class ScenarioDemo {
 public:
  explicit ScenarioDemo(Cli cli) : cli_(std::move(cli)) {}

  bool initialize();
  void run();
  void shutdown();

 private:
  bool initWindow();
  bool initVulkan();
  bool createRenderPass();
  bool createDepthAndFramebuffers();
  bool initRenderers();
  bool initEffects();           // 3 emitters + 1 shockwave
  bool loadScenario();
  void buildTimeline(const nlohmann::json& tracks_json);
  void buildPlaceholderAtlas();   // 8×8 white quad, single slot
  void renderFrame();
  void handleKey(SDL_Keycode k);
  void drawTimelineHud(float screen_w, float screen_h);

  /// HUD bar layout in screen pixels. y is from top (SDL convention).
  struct HudBar {
    float x0, x1;     // horizontal range
    float y0, y1;     // vertical range
  };
  HudBar hudBar(float screen_w, float screen_h) const {
    HudBar b;
    const float margin = 60.0f;
    const float bar_h  = 14.0f;
    b.x0 = margin;
    b.x1 = screen_w - margin;
    b.y0 = screen_h - margin - bar_h;
    b.y1 = screen_h - margin;
    return b;
  }

  Cli cli_;
  SDL_Window* window_ = nullptr;
  int window_w_ = 1280, window_h_ = 720;

  RHIDevice* device_ = nullptr;
  RHICommandBufferPtr cmd_;
  RHISemaphorePtr image_avail_, render_done_;
  RHIFencePtr fence_;
  RHIRenderPassPtr render_pass_;
  RHITexturePtr depth_tex_;
  RHITextureViewPtr depth_view_;
  std::vector<RHIFramebufferPtr> framebuffers_;
  uint32_t image_index_ = 0;

  std::unique_ptr<CameraBase> camera_;
  PrecisionOrigin origin_;

  AnnotationManager annotations_;
  CommandStack      command_stack_;
  Timeline          timeline_;
  hongtu::scene::PickingService picking_;
  AnnotationMode    mode_ = AnnotationMode::Local;

  std::unique_ptr<PolylineRenderer>     polyline_renderer_;
  std::unique_ptr<PolygonRenderer>      polygon_renderer_;
  std::unique_ptr<hongtu::renderer::BillboardRenderer> billboard_renderer_;
  std::unique_ptr<hongtu::renderer::ParticleRenderer>  particle_renderer_;
  std::unique_ptr<hongtu::renderer::ShockwavePass>     shockwave_pass_;

  // Three timeline-driven emitters per scenario:plume(toxic),fire,smoke.
  // Each is one ParticleSystem + EmitterEntity pair. Timeline tracks toggle
  // `enabled` via CallbackDriver<bool> — emitters that are off stop spawning
  // new particles but existing ones still age out.
  struct EffectSlot {
    std::unique_ptr<hongtu::renderer::ParticleSystem> system;
    std::unique_ptr<hongtu::scene::EmitterEntity>     entity;
    hongtu::renderer::EmitterDesc                     desc_template;  // wind / color etc.
    bool enabled = false;
  };
  EffectSlot plume_, fire_, smoke_;

  // One-shot shockwave triggered at `shockwave_trigger_time_`. Negative
  // means not triggered yet (track sets it to a positive seconds value).
  std::unique_ptr<hongtu::scene::ShockwaveEntity> shockwave_;
  float shockwave_trigger_time_ = -1.0f;
  float now_sec_ = 0.0f;     // wall clock (only used for shockwave radius)

  // Volumetric toxic gas plume — Gaussian plume density field driven by
  // VolumetricPass. Gated by `plume_.enabled` (same bool that drives the
  // green particle emitter — particles + volume render together for layered
  // visual). VolumetricPass init only when first needed (after device exists).
  std::unique_ptr<hongtu::renderer::VolumetricPass>           volumetric_pass_;
  std::unique_ptr<hongtu::scene::VolumetricEntity>            plume_volume_;
  hongtu::renderer::VolumetricPass::VolumeTexture             plume_tex_;
  hongtu::renderer::BillboardAtlas      atlas_;     // unused (P0 placeholder)
  RHITexturePtr     atlas_tex_;
  RHITextureViewPtr atlas_view_;
  RHISamplerPtr     atlas_sampler_;

  // 5 drawing tools — for interactive runs only (not used in headless ctest).
  std::unique_ptr<DrawingTool> tools_[5];
  int active_tool_ = 0;
};

bool ScenarioDemo::initialize() {
  if (!initWindow())                  return false;
  if (!initVulkan())                  return false;
  if (!createRenderPass())            return false;
  if (!createDepthAndFramebuffers())  return false;
  buildPlaceholderAtlas();
  if (!initRenderers())               return false;
  if (!initEffects())                 return false;
  if (!loadScenario())                return false;
  // Drawing tools — instantiated even in headless runs (lightweight).
  tools_[0] = std::make_unique<PolylineTool>();
  tools_[1] = std::make_unique<PolygonTool>();
  tools_[2] = std::make_unique<SymbolTool>();
  tools_[3] = std::make_unique<TextTool>();
  tools_[4] = std::make_unique<ArrowTool>();
  return true;
}

bool ScenarioDemo::initWindow() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) return false;
  std::string title = "XNBY Scenario Demo (--mode=" + cli_.mode + ")";
  window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, window_w_, window_h_,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
  return window_ != nullptr;
}

bool ScenarioDemo::initVulkan() {
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

bool ScenarioDemo::createRenderPass() {
  RenderPassDesc rpd;
  AttachmentDescription ca;
  ca.format = device_->getSwapchainFormat();
  ca.sampleCount = 1; ca.initialLayout = ImageLayout::Undefined;
  ca.finalLayout = ImageLayout::PresentSrc;
  ca.loadOpClear = true; ca.storeOpStore = true;
  rpd.attachments.push_back(ca);

  AttachmentDescription da;
  da.format = Format::D32_SFLOAT;
  da.sampleCount = 1; da.initialLayout = ImageLayout::Undefined;
  da.finalLayout = ImageLayout::DepthStencilAttachmentOptimal;
  da.loadOpClear = true; da.storeOpStore = false;
  rpd.attachments.push_back(da);

  SubpassDescription sp;
  AttachmentReference cr; cr.attachment = 0; cr.layout = ImageLayout::ColorAttachmentOptimal;
  sp.colorAttachments.push_back(cr);
  sp.depthStencilAttachment.attachment = 1;
  sp.depthStencilAttachment.layout = ImageLayout::DepthStencilAttachmentOptimal;
  sp.hasDepthStencil = true;
  rpd.subpasses.push_back(sp);

  render_pass_ = device_->createRenderPass(rpd);
  return render_pass_ != nullptr;
}

bool ScenarioDemo::createDepthAndFramebuffers() {
  TextureDesc dd;
  dd.type = TextureType::Texture2D;
  dd.extent = {static_cast<uint32_t>(window_w_), static_cast<uint32_t>(window_h_), 1u};
  dd.mipLevels = 1u; dd.arrayLayers = 1u;
  dd.format = Format::D32_SFLOAT;
  dd.usage = static_cast<TextureUsageFlags>(TextureUsageFlagBits::DepthStencilAttachment);
  dd.sampleCount = 1u;
  depth_tex_  = device_->createTexture(dd);
  if (!depth_tex_) return false;
  depth_view_ = depth_tex_->createView();
  if (!depth_view_) return false;

  framebuffers_.clear();
  for (uint32_t i = 0; i < device_->getSwapchainImageCount(); ++i) {
    auto img = device_->getSwapchainImage(i);
    auto color_view = img->createView();
    auto fb = device_->createFramebuffer(render_pass_, {color_view, depth_view_},
                                          device_->getSwapchainExtent(), 1);
    if (!fb) return false;
    framebuffers_.push_back(fb);
  }
  return true;
}

void ScenarioDemo::buildPlaceholderAtlas() {
  // 8×8 white opaque — annotation symbols just become solid colored quads
  // until a real atlas is loaded. P0 keeps it simple.
  const uint32_t W = 8, H = 8;
  std::vector<uint8_t> pixels(W * H * 4, 255);

  TextureDesc td;
  td.type = TextureType::Texture2D;
  td.extent = {W, H, 1u};
  td.mipLevels = 1u; td.arrayLayers = 1u;
  td.format = Format::R8G8B8A8_UNORM;
  td.usage = TextureUsageFlagBits::TransferDst | TextureUsageFlagBits::Sampled;
  td.sampleCount = 1u;
  atlas_tex_ = device_->createTexture(td);
  if (!atlas_tex_) return;

  RHIDevice::MipData mip;
  mip.width = W; mip.height = H;
  mip.data = pixels.data();
  mip.dataSize = pixels.size();
  device_->uploadTextureData(atlas_tex_, {mip}, 0u);

  atlas_view_ = atlas_tex_->createView();
  SamplerDesc sd;
  sd.magFilter = SamplerFilter::Linear;
  sd.minFilter = SamplerFilter::Linear;
  sd.mipmapMode = SamplerMipmapMode::Nearest;
  sd.addressModeU = SamplerAddressMode::ClampToEdge;
  sd.addressModeV = SamplerAddressMode::ClampToEdge;
  sd.addressModeW = SamplerAddressMode::ClampToEdge;
  atlas_sampler_ = device_->createSampler(sd);
}

bool ScenarioDemo::initRenderers() {
  polyline_renderer_  = std::make_unique<PolylineRenderer>();
  polygon_renderer_   = std::make_unique<PolygonRenderer>();
  billboard_renderer_ = std::make_unique<hongtu::renderer::BillboardRenderer>();
  particle_renderer_  = std::make_unique<hongtu::renderer::ParticleRenderer>();
  shockwave_pass_     = std::make_unique<hongtu::renderer::ShockwavePass>();
  if (!polyline_renderer_->initialize(device_, render_pass_)) return false;
  if (!polygon_renderer_->initialize(device_, render_pass_))  return false;
  if (!particle_renderer_->initialize(device_, render_pass_)) return false;
  if (!shockwave_pass_->initialize(device_, render_pass_))    return false;
  if (atlas_view_ && atlas_sampler_) {
    if (!billboard_renderer_->initialize(device_, render_pass_,
                                          atlas_view_, atlas_sampler_)) return false;
  }
  return true;
}

bool ScenarioDemo::initEffects() {
  namespace EmitterPresets = hongtu::renderer::EmitterPresets;
  using hongtu::renderer::ParticleSystem;
  using hongtu::scene::GeoEmitter;
  using hongtu::scene::LocalEmitter;
  using hongtu::scene::GeoShockwave;
  using hongtu::scene::LocalShockwave;

  // Three emitters spawn at scene origin (or close offsets for visual variety).
  // Each gets a fixed EmitterDesc template; per frame we copy + adjust center.
  auto build_emitter = [&](EffectSlot& slot, hongtu::renderer::EmitterDesc d,
                           const glm::dvec3& offset_local) {
    slot.system = std::make_unique<ParticleSystem>(4096);
    slot.desc_template = d;
    slot.enabled = false;
    if (cli_.mode == "local") {
      auto le = std::make_unique<LocalEmitter>();
      le->setPosition(offset_local);
      slot.entity = std::move(le);
    } else {
      // Convert local offset to geodetic offset via ENU around anchor
      Geodetic anchor = Geodetic::fromDegrees(39.9042, 116.4074, 0);
      glm::dvec3 ecef_anchor = hongtu::coordinate::geodeticToECEF(anchor);
      glm::dvec3 ecef =
          hongtu::coordinate::enuToECEF(offset_local, anchor, ecef_anchor);
      auto ge = std::make_unique<GeoEmitter>();
      ge->setPosition(hongtu::coordinate::ecefToGeodetic(ecef));
      slot.entity = std::move(ge);
    }
  };

  build_emitter(plume_,
      EmitterPresets::makeSmoke({0, 0, 0}, {0.6f, 0.0f, 0.3f}, 60.0f),
      {0.0, 0.0, 0.5});
  // Tint plume greenish (toxic gas).
  plume_.desc_template.color_ramp.colors[0] = {0.6f, 0.9f, 0.5f, 0.5f};
  plume_.desc_template.color_ramp.colors[1] = {0.4f, 0.8f, 0.4f, 0.7f};
  plume_.desc_template.color_ramp.colors[2] = {0.3f, 0.5f, 0.3f, 0.4f};
  plume_.desc_template.color_ramp.colors[3] = {0.1f, 0.2f, 0.1f, 0.0f};

  build_emitter(fire_,
      EmitterPresets::makeFire({0, 0, 0}, 180.0f),
      {0.0, 0.0, 0.0});
  build_emitter(smoke_,
      EmitterPresets::makeSmoke({0, 0, 0}, {0.5f, 0.5f, 0.0f}, 70.0f),
      {2.0, 0.0, 1.0});

  // One shockwave centered on fire emitter — start_time set when track fires.
  if (cli_.mode == "local") {
    auto sw = std::make_unique<LocalShockwave>();
    sw->setCenter({0, 0, 0.5});
    sw->setSpeedMps(4.0f);
    sw->setMaxRadiusMeters(8.0f);
    sw->setThicknessMeters(0.4f);
    sw->setColor({1.0f, 0.7f, 0.4f});
    sw->setIntensity(2.5f);
    shockwave_ = std::move(sw);
  } else {
    Geodetic anchor = Geodetic::fromDegrees(39.9042, 116.4074, 0);
    auto sw = std::make_unique<GeoShockwave>();
    sw->setCenter(anchor);
    sw->setSpeedMps(4.0f);
    sw->setMaxRadiusMeters(8.0f);
    sw->setThicknessMeters(0.4f);
    sw->setColor({1.0f, 0.7f, 0.4f});
    sw->setIntensity(2.5f);
    shockwave_ = std::move(sw);
  }

  return true;
}

bool ScenarioDemo::loadScenario() {
  std::string path = scenarioPath(cli_.scenario, cli_.mode);
  std::string body;
  if (!readFile(path, body)) {
    std::cerr << "[scenario_demo] cannot read " << path << "\n";
    return false;
  }
  auto j = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded()) {
    std::cerr << "[scenario_demo] invalid JSON in " << path << "\n";
    return false;
  }

  mode_ = (cli_.mode == "global") ? AnnotationMode::Geo : AnnotationMode::Local;

  // Camera + origin setup based on mode + scenario anchor.
  if (cli_.mode == "local") {
    origin_.setOrigin({0.0, 0.0, 0.0});
    auto cam = std::make_unique<LocalCamera>();
    cam->setScreenSize(static_cast<float>(window_w_), static_cast<float>(window_h_));
    cam->lookAtTarget({0, 0, 0}, /*distance=*/22.0,
                      /*yaw=*/0.4, /*pitch=*/-0.8);
    camera_ = std::move(cam);
  } else {
    Geodetic anchor = Geodetic::fromDegrees(39.9042, 116.4074, 0);
    if (j.contains("anchor")) {
      anchor = Geodetic::fromDegrees(j["anchor"]["lat"].get<double>(),
                                      j["anchor"]["lon"].get<double>(),
                                      j["anchor"].value("alt", 0.0));
    }
    origin_.setOriginGeodetic(anchor);
    auto cam = std::make_unique<GlobeCamera>();
    cam->setScreenSize(static_cast<float>(window_w_), static_cast<float>(window_h_));
    cam->setMinDistance(5.0);
    cam->lookAt(anchor, /*distance=*/40.0, /*heading=*/30.0, /*pitch=*/-45.0);
    camera_ = std::move(cam);
  }

  if (!annotations_.fromJson(j)) {
    std::cerr << "[scenario_demo] annotation parse warnings\n";
  }

  if (j.contains("duration_seconds")) {
    timeline_.setDuration(j["duration_seconds"].get<float>());
  }
  if (j.contains("tracks")) buildTimeline(j["tracks"]);

  if (cli_.time >= 0.0f) {
    timeline_.seek(cli_.time);
  } else {
    timeline_.play();
  }
  // ---------------------------------------------------------------------------
  // Volumetric toxic gas plume — built once, rendered when plume_.enabled.
  // ---------------------------------------------------------------------------
  volumetric_pass_ = std::make_unique<hongtu::renderer::VolumetricPass>();
  if (!volumetric_pass_->initialize(device_, render_pass_)) {
    std::cerr << "[scenario_demo] VolumetricPass init failed (continuing without)\n";
    volumetric_pass_.reset();
  } else {
    using hongtu::coordinate::dispersion::GaussianPlumeConfig;
    using hongtu::coordinate::dispersion::PasquillStability;
    using hongtu::coordinate::dispersion::buildGaussianPlumeGrid;

    GaussianPlumeConfig cfg;
    cfg.source        = {0, 0, 1.0};
    cfg.wind          = {2.0, 0, 0};
    cfg.stability     = PasquillStability::D;
    cfg.emission_rate = 50.0;
    auto grid = buildGaussianPlumeGrid(
        cfg, /*aabb_min=*/{0, -6, 0}, /*aabb_max=*/{20, 6, 4},
        /*w=*/48, /*h=*/24, /*d=*/12);
    auto shared = std::make_shared<hongtu::coordinate::dispersion::DensityGrid>(std::move(grid));

    glm::vec3 extent {10.0f, 6.0f, 2.0f};
    glm::dvec3 center_offset {10.0, 0.0, 2.0};      // downwind 10m,top of source
    if (cli_.mode == "local") {
      auto lv = std::make_unique<hongtu::scene::LocalVolumetric>();
      lv->setCenter(center_offset);
      lv->setExtentMeters(extent);
      lv->setDensityGrid(shared);
      lv->setColor({0.45f, 0.65f, 0.30f});
      plume_volume_ = std::move(lv);
    } else {
      Geodetic anchor = Geodetic::fromDegrees(39.9042, 116.4074, 0.0);
      double cos_lat = std::cos(anchor.lat);
      double dlat = center_offset.y / 111320.0;
      double dlon = center_offset.x / (111320.0 * std::max(1e-4, cos_lat));
      double lat_deg, lon_deg; anchor.toDegrees(lat_deg, lon_deg);
      auto gv = std::make_unique<hongtu::scene::GeoVolumetric>();
      gv->setCenter(Geodetic::fromDegrees(lat_deg + dlat, lon_deg + dlon,
                                           anchor.alt + center_offset.z));
      gv->setExtentMeters(extent);
      gv->setDensityGrid(shared);
      gv->setColor({0.45f, 0.65f, 0.30f});
      plume_volume_ = std::move(gv);
    }
    plume_tex_ = volumetric_pass_->uploadDensity(*shared);
  }

  return true;
}

void ScenarioDemo::buildTimeline(const nlohmann::json& tracks_json) {
  // Helper:build a bool Track from { keys: [{ t, v }, ...] }
  auto build_bool_track = [](const nlohmann::json& trk) {
    Track<bool> t;
    if (trk.contains("keys")) {
      for (const auto& k : trk["keys"]) {
        t.addKey(k["t"].get<float>(), k["v"].get<bool>());
      }
    }
    return t;
  };

  // Annotation visibility — id is the annotation id from the JSON.
  // Track name pattern: "annotation_visible_<id>".
  for (auto it = tracks_json.begin(); it != tracks_json.end(); ++it) {
    const std::string& name = it.key();
    if (name.rfind("annotation_visible_", 0) == 0) {
      uint64_t id = std::stoull(name.substr(19));
      timeline_.addDriver(std::make_unique<CallbackDriver<bool>>(
          build_bool_track(it.value()),
          [this, id](const bool& vis) {
            if (auto* a = annotations_.get(id)) a->setVisible(vis);
          }));
    }
  }

  // Emitter on/off — three named tracks driving the three EffectSlot booleans.
  if (tracks_json.contains("plume_enable")) {
    timeline_.addDriver(std::make_unique<CallbackDriver<bool>>(
        build_bool_track(tracks_json["plume_enable"]),
        [this](const bool& on) { plume_.enabled = on; }));
  }
  if (tracks_json.contains("fire_enable")) {
    timeline_.addDriver(std::make_unique<CallbackDriver<bool>>(
        build_bool_track(tracks_json["fire_enable"]),
        [this](const bool& on) { fire_.enabled = on; }));
  }
  if (tracks_json.contains("smoke_enable")) {
    timeline_.addDriver(std::make_unique<CallbackDriver<bool>>(
        build_bool_track(tracks_json["smoke_enable"]),
        [this](const bool& on) { smoke_.enabled = on; }));
  }

  // Shockwave — track exposes the trigger time as a float track. The
  // observed value at any t equals the configured `at` time; once we cross
  // it the shockwave starts expanding. Float track with a single key at
  // `at` set to value `at` means: before that t we'd clamp to it but
  // shockwave_trigger_time_ being >= now keeps it dormant.
  if (tracks_json.contains("shockwave_trigger")) {
    const auto& trk = tracks_json["shockwave_trigger"];
    if (trk.contains("at")) {
      float at = trk["at"].get<float>();
      timeline_.addDriver(std::make_unique<CallbackDriver<float>>(
          [at](){
            Track<float> t;
            // Pre-trigger: -1 (dormant), post-trigger: `at` itself.
            t.addKey(0.0f, -1.0f, hongtu::scene::Interp::Step);
            t.addKey(at,    at,   hongtu::scene::Interp::Step);
            return t;
          }(),
          [this](const float& trigger) { shockwave_trigger_time_ = trigger; }));
    }
  }
}

void ScenarioDemo::renderFrame() {
  fence_->wait();
  fence_->reset();
  image_index_ = device_->acquireNextImage(image_avail_);

  // Drive emitters — center their spawn at the entity RTC position each frame.
  // Disabled emitters call setEmitter with emit_rate=0 so existing particles
  // age out but no new ones spawn.
  const float emitter_dt = 1.0f / 60.0f;
  auto step_emitter = [&](EffectSlot& slot) {
    auto desc = slot.desc_template;
    desc.center = slot.entity->getRTCPosition(origin_);
    if (!slot.enabled) desc.emit_rate = 0.0f;
    slot.system->setEmitter(desc);
    slot.system->update(emitter_dt);
  };
  step_emitter(plume_);
  step_emitter(fire_);
  step_emitter(smoke_);

  std::vector<hongtu::renderer::ParticleInstance> all_particles;
  auto collect = [&](EffectSlot& slot) {
    std::vector<hongtu::renderer::ParticleInstance> insts;
    slot.system->buildInstanceBuffer(insts);
    all_particles.insert(all_particles.end(), insts.begin(), insts.end());
  };
  collect(plume_); collect(fire_); collect(smoke_);

  now_sec_ += emitter_dt;

  // Build render payloads from all visible annotations.
  auto payloads = annotations_.buildRenderPayloads(origin_);

  // Dispatch by kind into the matching renderer's draw list.
  std::vector<PolylineRenderer::PolylineDraw> poly_draws;
  std::vector<PolygonRenderer::PolygonDraw>   polygon_draws;
  std::vector<hongtu::renderer::BillboardRenderer::BillboardDraw> bb_draws;

  for (const auto& p : payloads) {
    switch (p.kind) {
      case AnnotationKind::Polyline: {
        PolylineRenderer::PolylineDraw d;
        d.points  = p.rtc_vertices;
        d.color   = p.style.color;
        d.width_m = p.style.stroke_width_m;
        d.dashed  = p.style.dash_period_m > 0.0f;
        d.dash_size_m = p.style.dash_period_m;
        poly_draws.push_back(std::move(d));
      } break;
      case AnnotationKind::Arrow: {
        PolylineRenderer::PolylineDraw d;
        d.points     = p.rtc_vertices;
        d.color      = p.style.color;
        d.width_m    = p.style.stroke_width_m;
        d.arrow_head = true;
        poly_draws.push_back(std::move(d));
      } break;
      case AnnotationKind::Polygon: {
        PolygonRenderer::PolygonDraw d;
        d.points = p.rtc_vertices;
        d.color  = p.style.color;
        polygon_draws.push_back(std::move(d));
      } break;
      case AnnotationKind::Symbol:
      case AnnotationKind::Text: {
        // P0:both are rendered as a single billboard at the anchor.
        // Text label glyph rendering is left for SDF-font follow-up.
        if (p.rtc_vertices.empty()) break;
        hongtu::renderer::BillboardRenderer::BillboardDraw d;
        d.world_pos = p.rtc_vertices[0];
        d.size_m    = {p.style.symbol_size_m + p.style.text_size_m,
                       p.style.symbol_size_m + p.style.text_size_m};
        d.atlas_uv  = {0.0f, 0.0f, 1.0f, 1.0f};
        d.color     = p.style.color;
        bb_draws.push_back(d);
      } break;
    }
  }

  const float aspect = static_cast<float>(window_w_) / static_cast<float>(window_h_);
  glm::mat4 view_proj = camera_->getProjectionMatrix(aspect)
                      * camera_->getRTCViewMatrix(origin_);
  glm::vec3 cam_pos_rtc = origin_.worldToLocal(camera_->getEyeWorld());

  glm::dmat4 view64 = camera_->getViewMatrix();
  glm::mat4  view32(view64);
  glm::vec3 cam_right(view32[0][0], view32[1][0], view32[2][0]);
  glm::vec3 cam_up   (view32[0][1], view32[1][1], view32[2][1]);

  cmd_->reset();
  cmd_->begin();
  Rect2D area;
  area.offset = {0, 0};
  area.extent = device_->getSwapchainExtent();
  std::vector<std::array<float, 4>> clears = {
      {0.10f, 0.12f, 0.18f, 1.0f},
      {1.0f, 0.0f, 0.0f, 0.0f}};
  cmd_->beginRenderPass(render_pass_, framebuffers_[image_index_], area, clears);

  Viewport vp;
  vp.x = 0; vp.y = 0;
  vp.width  = static_cast<float>(area.extent.width);
  vp.height = static_cast<float>(area.extent.height);
  vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
  cmd_->setViewport(vp);
  cmd_->setScissor(area);

  // Polygons first (filled fill behind everything else),
  PolygonRenderer::Params gp;
  gp.view_proj = view_proj;
  polygon_renderer_->record(cmd_, gp, polygon_draws);

  // Then polylines + arrows,
  PolylineRenderer::Params pp;
  pp.view_proj = view_proj;
  pp.camera_pos_rtc = cam_pos_rtc;
  polyline_renderer_->record(cmd_, pp, poly_draws);

  // Then symbols / text as billboards.
  if (atlas_view_ && atlas_sampler_) {
    hongtu::renderer::BillboardRenderer::Params bp;
    bp.view_proj = view_proj;
    bp.camera_right = cam_right;
    bp.camera_up    = cam_up;
    bp.camera_pos_rtc = cam_pos_rtc;
    bp.fade_near_m  = 0.0f;
    bp.fade_far_m   = 0.0f;
    billboard_renderer_->record(cmd_, bp, bb_draws);
  }

  // Particles — additive blend on top of solid geometry.
  if (!all_particles.empty()) {
    hongtu::renderer::ParticleRenderer::Params pp2;
    pp2.view_proj    = view_proj;
    pp2.camera_right = cam_right;
    pp2.camera_up    = cam_up;
    particle_renderer_->record(cmd_, pp2, all_particles);
  }

  // Volumetric Gaussian plume — render before shockwave so the latter can
  // overlay through it. Only when plume emitter is active.
  if (plume_.enabled && volumetric_pass_ && plume_volume_ && plume_tex_.view) {
    glm::vec3 c   = plume_volume_->getRTCCenter(origin_);
    glm::vec3 ext = plume_volume_->getExtentMeters();
    hongtu::renderer::VolumetricPass::Params vp;
    vp.view_proj      = view_proj;
    vp.camera_pos_rtc = cam_pos_rtc;
    vp.aabb_min       = c - ext;
    vp.aabb_max       = c + ext;
    vp.color          = {0.45f, 0.65f, 0.30f};
    vp.density_scale  = plume_volume_->getDensityScale() * 3.0f;
    vp.sigma          = 0.5f;
    vp.num_steps      = 64;
    volumetric_pass_->record(cmd_, vp, plume_tex_.view);
  }

  // Shockwave — fired when timeline crosses shockwave_trigger_time_.
  if (shockwave_ && shockwave_trigger_time_ >= 0.0f) {
    shockwave_->setStartTimeSec(shockwave_trigger_time_);
    float r = shockwave_->getCurrentRadius(timeline_.currentTime());
    if (r > 0.0f && !shockwave_->isComplete(timeline_.currentTime())) {
      hongtu::renderer::ShockwavePass::Params sp;
      sp.view_proj     = view_proj;
      sp.center_rtc    = shockwave_->getRTCCenter(origin_);
      sp.radius_m      = r;
      sp.color         = shockwave_->getColor();
      sp.intensity     = shockwave_->getIntensity();
      sp.thickness_m   = shockwave_->getThicknessMeters();
      sp.chroma_offset = shockwave_->getChromaOffset();
      shockwave_pass_->record(cmd_, sp);
    }
  }

  // HUD timeline progress bar — drawn last so it overlays the world.
  // Uses an orthographic view_proj in pixel coordinates so polygon points
  // are literally (px, py, 0). +Y down to match SDL screen convention.
  drawTimelineHud(static_cast<float>(area.extent.width),
                  static_cast<float>(area.extent.height));

  cmd_->endRenderPass();
  cmd_->end();
  device_->submit({cmd_}, {image_avail_}, {render_done_}, fence_);
  device_->present(render_done_);
}

void ScenarioDemo::drawTimelineHud(float screen_w, float screen_h) {
  if (timeline_.getDuration() <= 0.0f) return;
  HudBar b = hudBar(screen_w, screen_h);
  const float bar_w = b.x1 - b.x0;

  const float t = timeline_.currentTime();
  const float dur = timeline_.getDuration();
  const float frac = std::clamp(t / dur, 0.0f, 1.0f);
  const float head_x = b.x0 + frac * bar_w;

  // Screen-space orthographic view_proj — Vulkan NDC: x [-1,1] left→right,
  // y [-1,1] top→bottom (we flip Y so screen pixel (px,py) with py=0 at top
  // maps to NDC y = -1+2*py/H).
  glm::mat4 ortho = glm::mat4(1.0f);
  ortho[0][0] =  2.0f / screen_w;
  ortho[1][1] =  2.0f / screen_h;
  ortho[3][0] = -1.0f;
  ortho[3][1] = -1.0f;
  // No view matrix; ortho is the full projection.

  auto rect = [&](float x0, float y0, float x1, float y1) {
    return std::vector<glm::vec3>{
        {x0, y0, 0.0f}, {x1, y0, 0.0f}, {x1, y1, 0.0f}, {x0, y1, 0.0f}};
  };

  std::vector<PolygonRenderer::PolygonDraw> hud_polys;
  // Background bar(dark grey).
  hud_polys.push_back({ rect(b.x0, b.y0, b.x1, b.y1),
                         {0.15f, 0.15f, 0.18f, 0.85f} });
  // Filled portion (timeline progress).
  if (frac > 0.0f) {
    hud_polys.push_back({ rect(b.x0, b.y0, head_x, b.y1),
                           {0.95f, 0.6f, 0.2f, 0.95f} });
  }
  // Scrub head(slim vertical bar at current time).
  hud_polys.push_back({ rect(head_x - 2.0f, b.y0 - 6.0f,
                              head_x + 2.0f, b.y1 + 6.0f),
                         {1.0f, 1.0f, 1.0f, 0.95f} });

  PolygonRenderer::Params pp;
  pp.view_proj = ortho;
  polygon_renderer_->record(cmd_, pp, hud_polys);
}

void ScenarioDemo::handleKey(SDL_Keycode k) {
  if (k == SDLK_1) active_tool_ = 0;
  else if (k == SDLK_2) active_tool_ = 1;
  else if (k == SDLK_3) active_tool_ = 2;
  else if (k == SDLK_4) active_tool_ = 3;
  else if (k == SDLK_5) active_tool_ = 4;
  else if (k == SDLK_SPACE) {
    if (timeline_.isPlaying()) timeline_.pause(); else timeline_.play();
  } else if (k == SDLK_F5) {
    auto j = annotations_.toJson();
    std::ofstream f("/tmp/scenario_save.json");
    f << j.dump(2);
    std::cout << "[scenario_demo] saved /tmp/scenario_save.json\n";
  } else if (k == SDLK_F9) {
    std::string body;
    if (readFile("/tmp/scenario_save.json", body)) {
      auto j = nlohmann::json::parse(body, nullptr, false);
      if (!j.is_discarded()) {
        annotations_.fromJson(j);
        std::cout << "[scenario_demo] loaded /tmp/scenario_save.json\n";
      }
    }
  }
}

void ScenarioDemo::run() {
  using clock = std::chrono::steady_clock;
  auto t0 = clock::now();
  auto t_prev = t0;
  int frame_idx = 0;
  bool running = true;
  SDL_Event ev;

  std::cout << "[scenario_demo] mode=" << cli_.mode
            << " scenario=" << cli_.scenario
            << " time=" << cli_.time
            << " annotations=" << annotations_.size() << "\n";

  // Per-frame edge-triggered input state. Mouse position tracks every frame;
  // _down level-triggered; _clicked edge-triggered (down→up this frame).
  bool mouse_left_down_prev = false;
  bool mouse_right_down_prev = false;

  while (running) {
    hongtu::scene::DrawInputState input;
    input.viewport_w = window_w_;
    input.viewport_h = window_h_;

    bool ctrl_held = (SDL_GetModState() & KMOD_CTRL) != 0;
    bool mouse_left_down_now  = false;
    bool mouse_right_down_now = false;

    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) running = false;
      else if (ev.type == SDL_KEYDOWN) {
        SDL_Keycode k = ev.key.keysym.sym;
        if (k == SDLK_ESCAPE) {
          if (tools_[active_tool_]->isDrawing()) {
            tools_[active_tool_]->cancel();
            input.escape_pressed = true;
          } else {
            running = false;
          }
        } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
          input.enter_pressed = true;
        } else if (k == SDLK_z && ctrl_held) {
          command_stack_.undo(annotations_);
          std::cout << "[scenario_demo] undo (depth=" << command_stack_.undoDepth() << ")\n";
        } else if (k == SDLK_y && ctrl_held) {
          command_stack_.redo(annotations_);
          std::cout << "[scenario_demo] redo (depth=" << command_stack_.redoDepth() << ")\n";
        } else {
          handleKey(k);
        }
      }
    }

    // Sample current pointer state.
    int mx = 0, my = 0;
    Uint32 buttons = SDL_GetMouseState(&mx, &my);
    input.mouse_x = mx; input.mouse_y = my;
    mouse_left_down_now  = (buttons & SDL_BUTTON(SDL_BUTTON_LEFT))  != 0;
    mouse_right_down_now = (buttons & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    input.left_down      = mouse_left_down_now;
    input.left_clicked   = (mouse_left_down_prev && !mouse_left_down_now);
    input.right_clicked  = (mouse_right_down_prev && !mouse_right_down_now);
    mouse_left_down_prev  = mouse_left_down_now;
    mouse_right_down_prev = mouse_right_down_now;

    // Timeline scrub: when mouse is in the HUD bar y-band and left button
    // is held, treat it as a scrub gesture (suppresses DrawingTool input
    // so a scrub doesn't accidentally drop an annotation).
    HudBar b = hudBar(static_cast<float>(window_w_), static_cast<float>(window_h_));
    bool scrubbing = mouse_left_down_now
                     && my >= b.y0 - 6.0f && my <= b.y1 + 6.0f
                     && mx >= b.x0       && mx <= b.x1;
    if (scrubbing) {
      const float frac = std::clamp((mx - b.x0) / (b.x1 - b.x0), 0.0f, 1.0f);
      timeline_.seek(frac * timeline_.getDuration());
      // Suppress tool tick this frame
      input.left_down = false; input.left_clicked = false;
    }

    // Drive the active DrawingTool. Headless ctest runs never see mouse
    // events so this is a no-op there.
    tools_[active_tool_]->tick(input, *camera_, mode_, &picking_);
    if (auto a = tools_[active_tool_]->takeCompleted()) {
      command_stack_.doCommand(
          std::make_unique<hongtu::scene::AddAnnotationCommand>(std::move(a)),
          annotations_);
      std::cout << "[scenario_demo] drew annotation (total=" << annotations_.size() << ")\n";
    }

    // Use a fixed dt when in screenshot mode so seek/time results are
    // reproducible across machines (same idiom as disaster_demo).
    float dt = 0.0f;
    if (cli_.frames > 0) {
      dt = 1.0f / 60.0f;
    } else {
      auto t_now = clock::now();
      dt = std::chrono::duration<float>(t_now - t_prev).count();
      t_prev = t_now;
    }

    // If --time was explicit, freeze; otherwise let timeline advance.
    if (cli_.time < 0.0f) timeline_.update(dt);
    else                   timeline_.update(0.0f);

    renderFrame();
    ++frame_idx;

    if (cli_.frames > 0 && frame_idx >= cli_.frames) {
      if (!cli_.screenshot.empty()) {
        fence_->wait();
        std::vector<uint8_t> rgba;
        uint32_t W = 0, H = 0;
        if (device_->readbackSwapchainImage(image_index_, rgba, W, H)) {
          if (stbi_write_png(cli_.screenshot.c_str(),
                             static_cast<int>(W), static_cast<int>(H), 4,
                             rgba.data(), static_cast<int>(W) * 4) != 0) {
            std::cout << "[scenario_demo] wrote " << cli_.screenshot
                      << " (" << W << "x" << H << ")\n";
          }
        }
      }
      running = false;
    }
  }

  auto t1 = clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
  std::cout << "[scenario_demo] rendered " << frame_idx << " frames in "
            << ms << " ms\n";
  device_->waitIdle();
}

void ScenarioDemo::shutdown() {
  if (device_) device_->waitIdle();
  for (auto& t : tools_) t.reset();
  command_stack_.clear();
  annotations_.clear();
  timeline_.clearDrivers();
  plume_volume_.reset();
  plume_tex_ = {};
  volumetric_pass_.reset();
  shockwave_.reset();
  plume_.system.reset(); plume_.entity.reset();
  fire_.system.reset();  fire_.entity.reset();
  smoke_.system.reset(); smoke_.entity.reset();
  polyline_renderer_.reset();
  polygon_renderer_.reset();
  billboard_renderer_.reset();
  particle_renderer_.reset();
  shockwave_pass_.reset();
  atlas_sampler_.reset();
  atlas_view_.reset();
  atlas_tex_.reset();
  cmd_.reset();
  image_avail_.reset();
  render_done_.reset();
  fence_.reset();
  framebuffers_.clear();
  depth_view_.reset();
  depth_tex_.reset();
  render_pass_.reset();
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

  ScenarioDemo app(std::move(cli));
  if (!app.initialize()) {
    std::cerr << "[scenario_demo] initialize failed\n";
    return 1;
  }
  app.run();
  app.shutdown();
  return 0;
}
