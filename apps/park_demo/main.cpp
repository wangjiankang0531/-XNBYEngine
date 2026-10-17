// park_demo — phase 1.5 integration demo for the 园区一张图 stack.
//
// Demonstrates dual-mode (local-flat + global-earth) parity for the new
// PolylineRenderer + BillboardRenderer + BillboardAtlas + Geo*/Local*
// entity abstractions. Same scene definition, same renderer code, two
// completely different coordinate systems — the only mode-conditional
// code is which entity subclass we instantiate.
//
// Usage:
//   park_demo --mode=local           # flat-world,  LocalCamera + LocalBillboard
//   park_demo --mode=global          # earth-ECEF,  GlobeCamera + GeoBillboard
//   park_demo --mode=global --frames=60 --screenshot=/tmp/park_g.png
//
// Hotkeys (interactive runs):
//   P : print Profiler / culling stats (not wired in this minimal demo)
//   Esc : quit

#include <SDL.h>
#include <SDL_vulkan.h>

#include <hongtu/core/common.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/renderer/camera/local_camera.hpp>
#include <hongtu/renderer/effects/billboard_renderer.hpp>
#include <hongtu/renderer/effects/polyline_renderer.hpp>
#include <hongtu/renderer/globe/globe_camera.hpp>
#include <hongtu/renderer/picking/ray_caster.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <hongtu/rhi/RHITypes.h>
#include <hongtu/scene/billboard_entity.hpp>
#include <hongtu/scene/picking/picking_service.hpp>
#include <hongtu/scene/polyline_entity.hpp>

// stb_image_write impl lives here so park_demo's screenshot path can call
// stbi_write_png. stb_image impl is owned by the renderer module already,
// so we only declare it via the header (no _IMPLEMENTATION macro).
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image.h>
#include <stb_image_write.h>

#include <glm/gtc/matrix_transform.hpp>

#include <array>
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
using hongtu::coordinate::WGS84;
using hongtu::renderer::BillboardAtlas;
using hongtu::renderer::BillboardRenderer;
using hongtu::renderer::CameraBase;
using hongtu::renderer::GlobeCamera;
using hongtu::renderer::LocalCamera;
using hongtu::renderer::PolylineRenderer;
using hongtu::scene::BillboardEntity;
using hongtu::scene::GeoBillboard;
using hongtu::scene::GeoPolyline;
using hongtu::scene::LocalBillboard;
using hongtu::scene::LocalPolyline;
using hongtu::scene::PolylineEntity;

namespace {

struct Cli {
  std::string mode       = "local";     // "local" or "global"
  int         frames     = 0;           // 0 = interactive
  std::string screenshot;               // PNG path
};

bool parseCli(int argc, char** argv, Cli& cli) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--mode=", 0) == 0)               cli.mode       = a.substr(7);
    else if (a.rfind("--frames=", 0) == 0)        cli.frames     = std::stoi(a.substr(9));
    else if (a.rfind("--screenshot=", 0) == 0)    cli.screenshot = a.substr(13);
    else if (a == "--help" || a == "-h") {
      std::cout << "park_demo --mode=local|global [--frames=N --screenshot=PATH]\n";
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

// ============================================================================
// Scene definitions — built once, queried each frame for RTC lowering.
// ============================================================================

// Tian'anmen square (Beijing) — origin for the global-mode scene.
static const Geodetic kTianAnMen = Geodetic::fromDegrees(39.9042, 116.4074, 0.0);

// 8 billboards in a ring around the center, each using a different atlas
// slot. The ring is 10 m radius in local mode and ~10 m geodetic offset in
// global mode (computed below in buildScene).
struct BillboardSpec {
  std::string slot_name;       // matches park.json slot
  glm::vec4   color {1, 1, 1, 1};
  glm::vec2   size_m{2.0f, 2.0f};
};
static const std::array<BillboardSpec, 8> kRing = {{
  {"warning",      {1.0f, 0.95f, 0.5f,  1.0f}, {2.0f, 2.0f}},
  {"danger",       {1.0f, 0.6f,  0.6f,  1.0f}, {2.0f, 2.0f}},
  {"hazard",       {1.0f, 0.8f,  0.5f,  1.0f}, {2.0f, 2.0f}},
  {"fire",         {1.0f, 0.6f,  0.4f,  1.0f}, {2.0f, 2.0f}},
  {"hydrant",      {0.9f, 0.5f,  0.5f,  1.0f}, {2.0f, 2.0f}},
  {"extinguisher", {0.85f,0.5f,  0.5f,  1.0f}, {2.0f, 2.0f}},
  {"camera",       {0.5f, 0.7f,  1.0f,  1.0f}, {2.0f, 2.0f}},
  {"sensor",       {0.5f, 0.9f,  1.0f,  1.0f}, {2.0f, 2.0f}},
}};

// Patrol route (closed loop) — radius 5m square in the XY plane.
static const std::vector<glm::dvec3> kPatrolPointsLocal = {
  { 5.0,  5.0, 0.5},
  {-5.0,  5.0, 0.5},
  {-5.0, -5.0, 0.5},
  { 5.0, -5.0, 0.5},
};

// Evacuation route (open) — from far west toward an exit east.
static const std::vector<glm::dvec3> kEvacPointsLocal = {
  {-12.0,  0.0, 0.3},
  { -4.0,  0.0, 0.3},
  {  0.0,  3.0, 0.3},
  {  8.0,  3.0, 0.3},
  { 14.0,  0.0, 0.3},
};

// Geodetic helpers — shift a base point by `east_m` east + `north_m` north
// at the surface tangent plane, using a tiny equirectangular approximation
// (sufficient for the few-meter scale of this demo).
inline Geodetic geoOffset(const Geodetic& base, double east_m, double north_m,
                          double alt_m = 0.0) {
  static const double kDegPerMeterLat = 1.0 / 111320.0;
  const double cos_lat = std::cos(base.lat);
  double lat_deg, lon_deg;
  base.toDegrees(lat_deg, lon_deg);
  lat_deg += north_m * kDegPerMeterLat;
  lon_deg += east_m  * kDegPerMeterLat / std::max(0.0001, cos_lat);
  return Geodetic::fromDegrees(lat_deg, lon_deg, base.alt + alt_m);
}

// ============================================================================
// ParkDemo — owns the SDL window, RHI device, renderers, and scene entities.
// ============================================================================

class ParkDemo {
 public:
  explicit ParkDemo(Cli cli) : cli_(std::move(cli)) {}

  bool initialize();
  void run();
  void shutdown();

 private:
  bool initWindow();
  bool initVulkan();
  bool createRenderPass();
  bool createDepthAndFramebuffers();
  bool loadAtlas();
  bool initRenderers();
  void buildScene();
  void renderFrame();
  uint32_t image_index_ = 0;

  Cli              cli_;
  int              window_w_ = 1280, window_h_ = 720;
  SDL_Window*      window_ = nullptr;
  RHIDevice*       device_ = nullptr;
  RHIRenderPassPtr render_pass_;
  std::vector<RHIFramebufferPtr> framebuffers_;
  RHITexturePtr    depth_tex_;
  RHITextureViewPtr depth_view_;
  RHICommandBufferPtr cmd_;
  RHISemaphorePtr  image_avail_;
  RHISemaphorePtr  render_done_;
  RHIFencePtr      fence_;

  // Mode-specific camera (held via base pointer so the frame loop is
  // dual-mode agnostic).
  std::unique_ptr<CameraBase>  camera_;
  // Same for the precision origin — set at startup, never moves in this
  // tiny demo.
  PrecisionOrigin              origin_;

  // Scene entities, polymorphic.
  std::vector<std::unique_ptr<BillboardEntity>> billboards_;
  std::vector<std::unique_ptr<PolylineEntity>>  polylines_;

  // Renderers — coordinate-agnostic. Held by unique_ptr so we can reset
  // them in `shutdown()` *before* deleting the device (otherwise their
  // destructors would fire after the device is gone and call vkDestroy on
  // a stale pointer).
  std::unique_ptr<PolylineRenderer>  polyline_renderer_;
  std::unique_ptr<BillboardRenderer> billboard_renderer_;
  BillboardAtlas    atlas_;
  RHITexturePtr     atlas_tex_;
  RHITextureViewPtr atlas_view_;
  RHISamplerPtr     atlas_sampler_;
};

bool ParkDemo::initialize() {
  if (!initWindow())                  { std::cerr << "[park_demo] initWindow failed\n";  return false; }
  if (!initVulkan())                  { std::cerr << "[park_demo] initVulkan failed\n";  return false; }
  if (!createRenderPass())            { std::cerr << "[park_demo] createRenderPass failed\n"; return false; }
  if (!createDepthAndFramebuffers())  { std::cerr << "[park_demo] createDepthAndFramebuffers failed\n"; return false; }
  if (!loadAtlas())                   { std::cerr << "[park_demo] loadAtlas failed\n";   return false; }
  if (!initRenderers())               { std::cerr << "[park_demo] initRenderers failed\n"; return false; }
  buildScene();
  return true;
}

bool ParkDemo::initWindow() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return false;
  }
  std::string title = "XNBY Park Demo (--mode=" + cli_.mode + ")";
  window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, window_w_, window_h_,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
  if (!window_) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
    return false;
  }
  return true;
}

bool ParkDemo::initVulkan() {
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

bool ParkDemo::createRenderPass() {
  RenderPassDesc rpd;

  AttachmentDescription ca;
  ca.format        = device_->getSwapchainFormat();
  ca.sampleCount   = 1;
  ca.initialLayout = ImageLayout::Undefined;
  ca.finalLayout   = ImageLayout::PresentSrc;
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

  render_pass_ = device_->createRenderPass(rpd);
  return render_pass_ != nullptr;
}

bool ParkDemo::createDepthAndFramebuffers() {
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

  framebuffers_.clear();
  framebuffers_.reserve(device_->getSwapchainImageCount());
  for (uint32_t i = 0; i < device_->getSwapchainImageCount(); ++i) {
    auto img = device_->getSwapchainImage(i);
    auto color_view = img->createView();
    auto fb = device_->createFramebuffer(
        render_pass_, {color_view, depth_view_},
        device_->getSwapchainExtent(), 1);
    if (!fb) return false;
    framebuffers_.push_back(fb);
  }
  return true;
}

bool ParkDemo::loadAtlas() {
  if (!atlas_.loadFromFile("texture/icons/park.json")) {
    std::cerr << "park_demo: failed to load atlas JSON\n";
    return false;
  }
  int w = 0, h = 0, channels = 0;
  unsigned char* pixels =
      stbi_load("texture/icons/park.png", &w, &h, &channels, 4);
  if (!pixels) {
    std::cerr << "park_demo: failed to load atlas PNG\n";
    return false;
  }

  TextureDesc td;
  td.type        = TextureType::Texture2D;
  td.extent      = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1u};
  td.mipLevels   = 1u;
  td.arrayLayers = 1u;
  td.format      = Format::R8G8B8A8_UNORM;
  td.usage       = TextureUsageFlagBits::TransferDst | TextureUsageFlagBits::Sampled;
  td.sampleCount = 1u;
  atlas_tex_ = device_->createTexture(td);
  if (!atlas_tex_) { stbi_image_free(pixels); return false; }

  RHIDevice::MipData mip;
  mip.width    = static_cast<uint32_t>(w);
  mip.height   = static_cast<uint32_t>(h);
  mip.data     = pixels;
  mip.dataSize = static_cast<size_t>(w) * h * 4;
  device_->uploadTextureData(atlas_tex_, {mip}, 0u);
  stbi_image_free(pixels);

  atlas_view_ = atlas_tex_->createView();
  SamplerDesc sd;
  sd.magFilter    = SamplerFilter::Linear;
  sd.minFilter    = SamplerFilter::Linear;
  sd.mipmapMode   = SamplerMipmapMode::Nearest;
  sd.addressModeU = SamplerAddressMode::ClampToEdge;
  sd.addressModeV = SamplerAddressMode::ClampToEdge;
  sd.addressModeW = SamplerAddressMode::ClampToEdge;
  atlas_sampler_ = device_->createSampler(sd);
  return atlas_view_ && atlas_sampler_;
}

bool ParkDemo::initRenderers() {
  polyline_renderer_  = std::make_unique<PolylineRenderer>();
  billboard_renderer_ = std::make_unique<BillboardRenderer>();
  if (!polyline_renderer_->initialize(device_, render_pass_)) return false;
  if (!billboard_renderer_->initialize(device_, render_pass_,
                                       atlas_view_, atlas_sampler_)) return false;
  return true;
}

void ParkDemo::buildScene() {
  // Configure origin + camera based on mode. The rest of the scene plug-
  // in uses the same coordinates, but converted through Geo* / Local* before
  // calling getRTCPosition().
  if (cli_.mode == "local") {
    origin_.setOrigin({0.0, 0.0, 0.0});
    auto cam = std::make_unique<LocalCamera>();
    cam->setScreenSize(static_cast<float>(window_w_),
                       static_cast<float>(window_h_));
    cam->lookAtTarget({0, 0, 0}, /*distance=*/22.0,
                      /*yaw=*/0.4, /*pitch=*/-0.8);
    camera_ = std::move(cam);
  } else {
    origin_.setOriginGeodetic(kTianAnMen);
    auto cam = std::make_unique<GlobeCamera>();
    cam->setScreenSize(static_cast<float>(window_w_),
                       static_cast<float>(window_h_));
    cam->setMinDistance(5.0);
    cam->lookAt(kTianAnMen, /*distance=*/22.0,
                /*heading=*/30.0, /*pitch=*/-50.0);
    camera_ = std::move(cam);
  }

  // 8 billboards in a 10 m ring at z = 1 m above ground.
  billboards_.clear();
  for (size_t i = 0; i < kRing.size(); ++i) {
    const float theta = static_cast<float>(i) * 2.0f *
                        static_cast<float>(M_PI) / static_cast<float>(kRing.size());
    const double east  = std::cos(theta) * 10.0;
    const double north = std::sin(theta) * 10.0;
    const double up    = 1.5;

    glm::vec4 uv_rect = atlas_.lookupUVRect(kRing[i].slot_name);
    if (cli_.mode == "local") {
      auto b = std::make_unique<LocalBillboard>();
      b->setPosition({east, north, up});
      b->setAtlasUV(uv_rect);
      b->setSizeMeters(kRing[i].size_m);
      b->setColor(kRing[i].color);
      billboards_.push_back(std::move(b));
    } else {
      auto b = std::make_unique<GeoBillboard>();
      b->setPosition(geoOffset(kTianAnMen, east, north, up));
      b->setAtlasUV(uv_rect);
      b->setSizeMeters(kRing[i].size_m);
      b->setColor(kRing[i].color);
      billboards_.push_back(std::move(b));
    }
  }

  // Two polylines: patrol (closed, solid yellow) + evacuation (open,
  // dashed green with a head arrow showing the exit direction). The
  // closure helper builds whichever (Geo|Local) flavour matches the mode
  // and returns a non-owning pointer for further attribute setting.
  polylines_.clear();
  auto buildPolyline = [&](const std::vector<glm::dvec3>& pts,
                           const glm::vec4& color, float width,
                           bool closed) -> PolylineEntity* {
    PolylineEntity* raw = nullptr;
    if (cli_.mode == "local") {
      auto p = std::make_unique<LocalPolyline>();
      p->setPoints(pts);
      p->setColor(color);
      p->setWidthMeters(width);
      p->setClosed(closed);
      raw = p.get();
      polylines_.push_back(std::move(p));
    } else {
      auto p = std::make_unique<GeoPolyline>();
      std::vector<Geodetic> g;
      g.reserve(pts.size());
      for (const auto& v : pts) {
        g.push_back(geoOffset(kTianAnMen, v.x, v.y, v.z));
      }
      p->setPoints(std::move(g));
      p->setColor(color);
      p->setWidthMeters(width);
      p->setClosed(closed);
      raw = p.get();
      polylines_.push_back(std::move(p));
    }
    return raw;
  };

  buildPolyline(kPatrolPointsLocal, {1.0f, 0.85f, 0.2f, 0.85f},
                /*width=*/0.6f, /*closed=*/true);

  PolylineEntity* evac = buildPolyline(
      kEvacPointsLocal, {0.2f, 0.85f, 0.4f, 0.95f},
      /*width=*/0.8f, /*closed=*/false);
  evac->setArrowHead(true);
  evac->setArrowTail(true);
  evac->setArrowSizeMeters(2.0f);
  evac->setDashed(true);
  evac->setDashSizeMeters(1.2f);
  evac->setDashGapRatio(0.35f);
}

void ParkDemo::renderFrame() {
  // Acquire + sync.
  fence_->wait();
  fence_->reset();
  image_index_ = device_->acquireNextImage(image_avail_);

  // Build per-frame camera matrices in RTC space.
  const glm::mat4  view_proj   = (cli_.mode == "global")
      ? camera_->getProjectionMatrix(static_cast<float>(window_w_) / window_h_)
            * camera_->getRTCViewMatrix(origin_)
      : camera_->getProjectionMatrix(static_cast<float>(window_w_) / window_h_)
            * camera_->getRTCViewMatrix(origin_);
  const glm::vec3  cam_pos_rtc = origin_.worldToLocal(camera_->getEyeWorld());

  // Lower scene entities into RTC float32 space.
  std::vector<PolylineRenderer::PolylineDraw> poly_draws;
  poly_draws.reserve(polylines_.size());
  for (const auto& p : polylines_) {
    PolylineRenderer::PolylineDraw d;
    d.points         = p->getRTCPoints(origin_);
    d.color          = p->getColor();
    d.width_m        = p->getWidthMeters();
    d.closed         = p->isClosed();
    d.arrow_head     = p->hasArrowHead();
    d.arrow_tail     = p->hasArrowTail();
    d.arrow_size_m   = p->getArrowSizeMeters();
    d.dashed         = p->isDashed();
    d.dash_size_m    = p->getDashSizeMeters();
    d.dash_gap_ratio = p->getDashGapRatio();
    poly_draws.push_back(std::move(d));
  }

  std::vector<BillboardRenderer::BillboardDraw> bb_draws;
  bb_draws.reserve(billboards_.size());
  for (const auto& b : billboards_) {
    BillboardRenderer::BillboardDraw d;
    d.world_pos = b->getRTCPosition(origin_);
    d.size_m    = b->getSizeMeters();
    d.atlas_uv  = b->getAtlasUV();
    d.color     = b->getColor();
    bb_draws.push_back(d);
  }

  // Compute camera right/up basis vectors for billboard orientation.
  // From the world-space view matrix: rows 0/1 of the upper 3×3 are the
  // camera right and up. We re-derive them in RTC space.
  glm::dmat4 view64 = camera_->getViewMatrix();
  glm::mat4  view32(view64);
  glm::vec3 cam_right(view32[0][0], view32[1][0], view32[2][0]);
  glm::vec3 cam_up   (view32[0][1], view32[1][1], view32[2][1]);

  // Begin render pass.
  cmd_->reset();
  cmd_->begin();
  Rect2D area;
  area.offset = {0, 0};
  area.extent = device_->getSwapchainExtent();
  std::vector<std::array<float, 4>> clears = {
      {0.10f, 0.12f, 0.18f, 1.0f},     // dark blue background
      {1.0f, 0.0f, 0.0f, 0.0f}};        // depth = 1.0
  cmd_->beginRenderPass(render_pass_, framebuffers_[image_index_],
                         area, clears);

  Viewport vp;
  vp.x = 0; vp.y = 0;
  vp.width    = static_cast<float>(area.extent.width);
  vp.height   = static_cast<float>(area.extent.height);
  vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
  cmd_->setViewport(vp);
  cmd_->setScissor(area);

  // Polylines first (depth-write off, drawn behind billboards).
  PolylineRenderer::Params pp;
  pp.view_proj      = view_proj;
  pp.camera_pos_rtc = cam_pos_rtc;
  polyline_renderer_->record(cmd_, pp, poly_draws);

  // Billboards on top.
  BillboardRenderer::Params bp;
  bp.view_proj      = view_proj;
  bp.camera_right   = cam_right;
  bp.camera_up      = cam_up;
  bp.camera_pos_rtc = cam_pos_rtc;
  bp.fade_near_m    = 0.0f;
  bp.fade_far_m     = 0.0f;
  billboard_renderer_->record(cmd_, bp, bb_draws);

  cmd_->endRenderPass();
  cmd_->end();
  device_->submit({cmd_}, {image_avail_}, {render_done_}, fence_);
  device_->present(render_done_);
}

void ParkDemo::run() {
  using clock = std::chrono::steady_clock;
  auto t0 = clock::now();
  int frame_idx = 0;
  bool running  = true;
  SDL_Event ev;

  std::cout << "[park_demo] mode=" << cli_.mode
            << " billboards=" << billboards_.size()
            << " polylines=" << polylines_.size()
            << " frames=" << cli_.frames
            << " screenshot=" << (cli_.screenshot.empty() ? "(none)" : cli_.screenshot)
            << "\n";

  // PickingService demo — pickGround returns world ground hit; we then find
  // the nearest billboard in the ring within 3 m to identify its slot name.
  // (BillboardEntity isn't a scene::Entity, so we can't registerEntity —
  // see scene/picking/picking_service.hpp for entity-based API used by
  // scenario_demo.)
  hongtu::scene::PickingService picking;

  while (running) {
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) running = false;
      else if (ev.type == SDL_KEYDOWN &&
               ev.key.keysym.sym == SDLK_ESCAPE) running = false;
      else if (ev.type == SDL_MOUSEBUTTONUP &&
               ev.button.button == SDL_BUTTON_LEFT && camera_) {
        auto r = picking.pickGround(ev.button.x, ev.button.y, *camera_,
                                     window_w_, window_h_);
        if (r.hit) {
          glm::dvec3 hit_world = r.world_hit;
          int best_i = -1;
          double best_d = 3.0;     // 3 m radius threshold
          for (size_t i = 0; i < billboards_.size(); ++i) {
            glm::dvec3 bb_world(billboards_[i]->getRTCPosition(origin_));
            bb_world += origin_.getOrigin();   // back to world (RTC + origin)
            double d = glm::length(glm::dvec2(hit_world) - glm::dvec2(bb_world));
            if (d < best_d) { best_d = d; best_i = static_cast<int>(i); }
          }
          if (best_i >= 0) {
            std::cout << "[park_demo] picked slot='" << kRing[best_i].slot_name
                      << "' (idx=" << best_i << ", dist=" << best_d << "m)\n";
          } else {
            std::cout << "[park_demo] ground hit, no billboard within 3m\n";
          }
        }
      }
    }
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
            std::cout << "[park_demo] wrote " << cli_.screenshot
                      << " (" << W << "x" << H << ")\n";
          } else {
            std::cerr << "[park_demo] stbi_write_png failed\n";
          }
        } else {
          std::cerr << "[park_demo] swapchain readback failed\n";
        }
      }
      running = false;
    }
  }

  auto t1 = clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
  std::cout << "[park_demo] rendered " << frame_idx << " frames in "
            << ms << " ms\n";

  device_->waitIdle();
}

void ParkDemo::shutdown() {
  // Wait for all in-flight work to finish before tearing down. Without
  // this, a fence still in use may queue a vkDestroyBuffer on a device
  // that's about to be freed below.
  if (device_) device_->waitIdle();

  // Release engine-owned RHI handles BEFORE deleting the device — they
  // all hold a (non-owning) device pointer internally and will call
  // vkDestroy* through it on destruction.
  polylines_.clear();
  billboards_.clear();
  polyline_renderer_.reset();
  billboard_renderer_.reset();
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

  // PolylineRenderer + BillboardRenderer hold their own RHI handles —
  // those get released when this ParkDemo dtor runs (after `shutdown()`)
  // since they're members. To force release here so the device is still
  // alive, explicit re-init isn't supported; the destructors will hit
  // before main returns and the device pointer remains valid because
  // we delete it last.

  delete device_;
  device_ = nullptr;
  if (window_) SDL_DestroyWindow(window_);
  SDL_Quit();
}

}  // namespace

int main(int argc, char** argv) {
#ifdef __APPLE__
  // Auto-configure MoltenVK ICD path (homebrew install). Without this the
  // Vulkan loader can't find the ICD and `SDL_CreateWindow(SDL_WINDOW_VULKAN)`
  // fails with "Installed Vulkan Portability library doesn't implement
  // VK_KHR_surface". Mirrors the same fix in local_demo / earth_demo.
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

  ParkDemo app(std::move(cli));
  if (!app.initialize()) {
    std::cerr << "[park_demo] initialize failed\n";
    return 1;
  }
  app.run();
  app.shutdown();
  return 0;
}
