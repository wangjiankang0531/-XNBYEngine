#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <hongtu/asset/GltfLoader.h>
#include <hongtu/asset/ShaderCompiler.h>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/gis/imagery/esri_provider.hpp>
#include <hongtu/gis/imagery/local_imagery_provider.hpp>
#include <hongtu/gis/imagery/routed_imagery_provider.hpp>
#include <hongtu/gis/imagery/tianditu_provider.hpp>
#include <hongtu/gis/terrain/terrarium_provider.hpp>
#include <hongtu/renderer/globe/globe_camera.hpp>
#include <hongtu/renderer/globe/globe_renderer.hpp>
#include <hongtu/renderer/globe/label_renderer.hpp>
#include <hongtu/renderer/pbr/mesh_vertex.hpp>
#include <hongtu/renderer/pbr/pbr_forward_pipeline.hpp>
#include <hongtu/renderer/pbr/pbr_material.hpp>
#include <hongtu/renderer/pbr/shadow_pass.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <hongtu/scene/geo_entity.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <vulkan/vulkan.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

using namespace HongTu::RHI;
using namespace HongTu::Asset;
using namespace hongtu::renderer;
using namespace hongtu::coordinate;
using namespace hongtu::gis;

// ============================================================================
// Earth Demo Application
// ============================================================================

// Headless test driver flags — set via CLI for automated screenshot capture.
struct DemoCli {
  std::string look_at;           // "beijing" | "everest" | "reset" | "lat,lon,alt_km"
  int exit_frames = 0;           // 0 = run interactively, >0 = exit after N frames
  int warmup_frames = 0;         // frames to render before counting toward exit_frames
  std::string screenshot;        // path for final screenshot (Vulkan readback)
  bool dump_stats = false;       // log drawing-set stats each second
  bool dump_drawing_set = false; // dump full drawing-set listing on exit frame
  bool no_labels = false;        // suppress the demo's city labels (they're 500km
                                 // billboards that fill the screen at close-up alts)
  bool no_tiles = false;         // disable imagery tile rendering — base globe only
  // Camera pitch in degrees, -90 = straight down, 0 = horizon. Tweak to
  // replicate user-reported oblique views where DEM elevation seams
  // become visible.
  double pitch_deg = -90.0;
  // City model overlay — loads a glTF/glb and places it via GeoEntity at the
  // configured Beijing lat/lon. Camera autosnaps to a nearby low-altitude
  // viewpoint so the building cluster is framed against the satellite imagery.
  std::string city_model;        // path to .gltf / .glb (empty = no city)
  double city_lat_deg = 39.9042; // Tian'anmen by default
  double city_lon_deg = 116.4074;
  double city_alt_m = 0.0; // model base sits at this altitude AMSL
  // Pin the sun to a fixed orbital longitude (radians) instead of animating.
  // Used to park the ocean sun-glint for deterministic screenshots. NaN = off.
  double sun_lon = std::numeric_limits<double>::quiet_NaN();
  // Local water body (M3): "lat,lon:lat,lon:..." boundary polygon placed at
  // --water-level altitude. Empty = no water body.
  std::string water_poly;
  double water_level_m = 0.0;
  // Imagery provider: "" = Tianditu (default), "esri" = ArcGIS World Imagery.
  std::string imagery;
  // Optional machine-readable final streaming snapshot.
  std::string stats_json;
};

class EarthDemoApp {
public:
  bool initialize();
  void setCli(const DemoCli& cli) {
    cli_ = cli;
  }
  void run();
  void shutdown();

private:
  DemoCli cli_;
  SDL_Window* window = nullptr;
  int windowWidth = 1280, windowHeight = 720;

  RHIDevice* device = nullptr;
  RHIRenderPassPtr renderPass;
  std::vector<RHIFramebufferPtr> framebuffers;
  RHICommandBufferPtr commandBuffer;
  RHISemaphorePtr imageAvailSema, renderDoneSema;
  RHIFencePtr fence;

  // Globe rendering
  std::unique_ptr<GlobeRenderer> globe;
  GlobeCamera camera;
  std::shared_ptr<gis::RoutedImageryProvider> huilianImagery_;

  // Depth buffer
  RHITexturePtr depthTexture;
  RHITextureViewPtr depthView;

  // Interaction state
  bool dragging = false;      // left button held
  bool rightDragging = false; // right button held
  glm::vec2 lastMouse;
  bool interactiveEnabled = false;

  // Timing
  std::chrono::high_resolution_clock::time_point startTime;
  std::chrono::high_resolution_clock::time_point lastTime;
  uint64_t frameCount = 0;
  double fpsTimer = 0;

  // Feature toggles (number keys)
  bool tilesOn = true;
  bool labelsOn = true;

  // Last swapchain image index acquired in renderFrame — used by the
  // --screenshot path to know which presented image to read back.
  uint32_t lastImgIdx_ = 0;

  // Push constants: MVP
  struct PushData {
    glm::mat4 mvp;
  };

  // --- City model overlay (PBR) ---------------------------------------------
  // Loaded only when --city is passed. Reuses the engine PBR forward pipeline
  // (embedded mode = draws into earth_demo's existing SRGB render pass with
  // inline ACES). Shadows are kept off (shadow_strength=0); ShadowPass is
  // still initialized so its descriptor set + cleared depth texture can fill
  // the set=1 binding the pipeline shader expects.
  hongtu::asset::ModelData cityModel_;
  bool cityLoaded_ = false;
  glm::vec3 cityBboxMin_{0.0f}, cityBboxMax_{0.0f};
  PbrMaterialFactory cityMaterialFactory_;
  ShadowPass cityShadowPass_;
  PbrForwardPipeline cityPipeline_;
  std::vector<PbrMaterial> cityMaterials_;
  struct CityPrimitive {
    uint32_t index_offset;
    uint32_t index_count;
    int material_id;
  };
  std::vector<CityPrimitive> cityPrimitives_;
  RHIBufferPtr cityVbo_, cityIbo_;
  hongtu::scene::GeoEntity cityEntity_;

  bool initWindow();
  bool initVulkan();
  bool createRenderPass();
  bool createDepthBuffer();
  bool createFramebuffers();
  bool createSync();
  void setupGlobe();
  void setupCamera();
  bool setupCity();  // returns false if --city was set but failed to load
  void renderCity(); // called inside the main render pass
  void handleInput(const SDL_Event& e);
  void renderFrame(float dt);
  void writeStatsJson() const;
};

bool EarthDemoApp::initWindow() {
  SDL_Init(SDL_INIT_VIDEO);
  window = SDL_CreateWindow("XNBY Engine - Digital Earth",
                            SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED,
                            windowWidth,
                            windowHeight,
                            SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
  return window != nullptr;
}

bool EarthDemoApp::initVulkan() {
  auto factory = Backend::createFactory(Backend::Type::Vulkan);
  if (!factory)
    return false;
  unsigned int ec = 0;
  SDL_Vulkan_GetInstanceExtensions(window, &ec, nullptr);
  std::vector<const char*> exts(ec);
  SDL_Vulkan_GetInstanceExtensions(window, &ec, exts.data());
  SwapchainDesc sc;
  sc.extent = {static_cast<uint32_t>(windowWidth), static_cast<uint32_t>(windowHeight)};
  sc.format = Format::R8G8B8A8_SRGB;
  sc.imageCount = 3;
  sc.vsync = true;
  device = factory->createDeviceWithSwapchain(window, sc, false, exts);
  Backend::destroyFactory(factory);
  if (!device)
    return false;
  std::cout << "GPU: " << device->getProperties().deviceName << std::endl;
  return true;
}

bool EarthDemoApp::createRenderPass() {
  RenderPassDesc rpd;

  // Color attachment (index 0)
  AttachmentDescription ca;
  ca.format = device->getSwapchainFormat();
  ca.sampleCount = 1;
  ca.initialLayout = ImageLayout::Undefined;
  ca.finalLayout = ImageLayout::PresentSrc;
  ca.loadOpClear = true;
  ca.storeOpStore = true;
  rpd.attachments.push_back(ca);

  // Depth attachment (index 1)
  AttachmentDescription da;
  da.format = Format::D32_SFLOAT;
  da.sampleCount = 1;
  da.initialLayout = ImageLayout::Undefined;
  da.finalLayout = ImageLayout::DepthStencilAttachmentOptimal;
  da.loadOpClear = true;
  da.storeOpStore = false;
  rpd.attachments.push_back(da);

  SubpassDescription sp;
  AttachmentReference cr;
  cr.attachment = 0;
  cr.layout = ImageLayout::ColorAttachmentOptimal;
  sp.colorAttachments.push_back(cr);
  sp.depthStencilAttachment.attachment = 1;
  sp.depthStencilAttachment.layout = ImageLayout::DepthStencilAttachmentOptimal;
  sp.hasDepthStencil = true;
  rpd.subpasses.push_back(sp);

  SubpassDependency dep;
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput) |
                     static_cast<PipelineStageFlags>(PipelineStageFlagBits::EarlyFragmentTests);
  dep.dstStageMask = static_cast<PipelineStageFlags>(PipelineStageFlagBits::ColorAttachmentOutput) |
                     static_cast<PipelineStageFlags>(PipelineStageFlagBits::EarlyFragmentTests);
  dep.dstAccessMask = static_cast<AccessFlags>(AccessFlagBits::ColorAttachmentWrite) |
                      static_cast<AccessFlags>(AccessFlagBits::DepthStencilAttachmentWrite);
  rpd.dependencies.push_back(dep);

  renderPass = device->createRenderPass(rpd);
  return renderPass != nullptr;
}

bool EarthDemoApp::createDepthBuffer() {
  auto ext = device->getSwapchainExtent();
  TextureDesc desc;
  desc.type = TextureType::Texture2D;
  desc.extent = {ext.width, ext.height, 1};
  desc.mipLevels = 1;
  desc.arrayLayers = 1;
  desc.format = Format::D32_SFLOAT;
  desc.usage = static_cast<TextureUsageFlags>(TextureUsageFlagBits::DepthStencilAttachment);
  desc.sampleCount = 1;
  depthTexture = device->createTexture(desc);
  if (!depthTexture) {
    std::cerr << "Failed to create depth texture" << std::endl;
    return false;
  }
  depthView = depthTexture->createView();
  return depthView != nullptr;
}

bool EarthDemoApp::createFramebuffers() {
  for (uint32_t i = 0; i < device->getSwapchainImageCount(); ++i) {
    auto img = device->getSwapchainImage(i);
    auto colorView = img->createView();
    auto fb = device->createFramebuffer(renderPass, {colorView, depthView}, device->getSwapchainExtent(), 1);
    if (!fb)
      return false;
    framebuffers.push_back(fb);
  }
  return true;
}

bool EarthDemoApp::createSync() {
  imageAvailSema = device->createSemaphore();
  renderDoneSema = device->createSemaphore();
  fence = device->createFence(true);
  commandBuffer = device->createCommandBuffer();
  return imageAvailSema && renderDoneSema && fence && commandBuffer;
}

void EarthDemoApp::setupGlobe() {
  spdlog::set_level(spdlog::level::debug);

  globe = std::make_unique<GlobeRenderer>(device);
  globe->setScreenSize(static_cast<f32>(windowWidth), static_cast<f32>(windowHeight));
  globe->setTileRenderingEnabled(true);
  // TileCache now adds the imagery provider's stable namespace beneath this
  // common root, so live provider switching cannot return stale z/x/y bytes.
  globe->setTileCachePath("./tile_cache");
  globe->setMaxConcurrentDownloads(12); // higher throughput; stale requests get cancelled
  // Satellite imagery — live HTTP download with disk cache. First run
  // downloads on demand; subsequent runs read from ./tile_cache.
  // --imagery=esri switches to ArcGIS World Imagery: its ocean rendering
  // is far more uniform than Tianditu's (whose deep-sea tiles flip between
  // near-black and bright cyan across LOD/source seams — the visible
  // "water boundary lines" users report come from that, not the shading).
  if (cli_.imagery == "huilian") {
    const std::filesystem::path root(HT_ENGINE_SOURCE_DIR);
    auto fallback = std::make_shared<gis::EsriImageryProvider>();
    huilianImagery_ = std::make_shared<gis::RoutedImageryProvider>("HuilianRegionalComposite-v1", fallback);

    auto globalLow =
        std::make_shared<gis::LocalImageryProvider>((root / "assets/tiles/tianditu_img").string(), "jpg", 1, 4);
    auto southChina =
        std::make_shared<gis::LocalImageryProvider>((root / "assets/tiles/esri_huilian").string(), "jpg", 8, 8);
    auto huilianNear =
        std::make_shared<gis::LocalImageryProvider>((root / "assets/tiles/esri_huilian").string(), "jpg", 10, 10);

    const auto world =
        gis::RoutedImageryProvider::boundsFromDegrees(-180.0, -85.05112877980659, 180.0, 85.05112877980659);
    const auto context =
        gis::RoutedImageryProvider::boundsFromDegrees(108.28125, 17.97873309555617, 119.53125, 28.304380682962773);
    const auto near =
        gis::RoutedImageryProvider::boundsFromDegrees(112.8515625, 21.616579336740603, 115.6640625, 24.20688962239802);
    huilianImagery_->addRoute({"tianditu-offline-z1-z4", world, 1, 4, 10, globalLow});
    huilianImagery_->addRoute({"south-china-offline-z8", context, 8, 8, 100, southChina});
    huilianImagery_->addRoute({"huilian-offline-z10", near, 10, 10, 200, huilianNear});
    globe->addImageryLayer(huilianImagery_);

    const double lat = 22.74776854553;
    const double lon = 114.599640461187;
    for (const int z : {3, 4, 8, 10}) {
      const gis::TileCoord probe = gis::TileCoord::fromGeodetic(lat, lon, z);
      spdlog::info("[huilian-route] {} -> {}", probe.toString(), huilianImagery_->resolveRouteName(probe));
    }
  } else if (cli_.imagery == "esri") {
    globe->addImageryLayer(std::make_shared<gis::EsriImageryProvider>());
  } else {
    const std::string kTiandituKey = "915ae31392f5b995fd3f2638cc0b3c1b";
    globe->addImageryLayer(
        std::make_shared<gis::TiandituProvider>(kTiandituKey, gis::TiandituProvider::LayerType::Imagery));
  }
  globe->setDemProvider(std::make_shared<gis::TerrariumProvider>());

  if (!globe->initialize(renderPass)) {
    std::cerr << "Failed to initialize GlobeRenderer" << std::endl;
    return;
  }

  // Pin the sun for deterministic ocean-glint screenshots. Same declination
  // as the renderer's day-orbit; only the longitude is user-driven.
  if (!std::isnan(cli_.sun_lon)) {
    constexpr double kDecl = 0.408; // ~23.4°
    glm::vec3 sun(std::cos(kDecl) * std::cos(cli_.sun_lon), std::cos(kDecl) * std::sin(cli_.sun_lon), std::sin(kDecl));
    globe->setSunDirection(sun);
    spdlog::info("[--sun-lon] sun pinned at lon={} → dir=({:.3f},{:.3f},{:.3f})", cli_.sun_lon, sun.x, sun.y, sun.z);
  }

  // Local water body from CLI: "lat,lon:lat,lon:..." at --water-level.
  if (!cli_.water_poly.empty()) {
    std::vector<hongtu::coordinate::Geodetic> boundary;
    std::stringstream ss(cli_.water_poly);
    std::string pair;
    while (std::getline(ss, pair, ':')) {
      double lat = 0.0, lon = 0.0;
      if (std::sscanf(pair.c_str(), "%lf,%lf", &lat, &lon) == 2) {
        boundary.push_back(hongtu::coordinate::Geodetic::fromDegrees(lat, lon, cli_.water_level_m));
      }
    }
    int id = globe->addWaterBody(std::move(boundary));
    spdlog::info("[--water-poly] water body id={} at level {} m", id, cli_.water_level_m);
  }

  // Demo labels at famous landmarks (large quads visible from space)
  auto addPin = [&](double lat, double lon, glm::vec4 color) {
    Label lbl;
    lbl.position = Geodetic::fromDegrees(lat, lon, 0.0);
    lbl.color = color;
    lbl.size_meters = 500000.0f; // 500 km — clearly visible from globe view
    globe->addLabel(lbl);
  };
  if (!cli_.no_labels) {
    addPin(39.9042, 116.4074, {1.0f, 0.2f, 0.2f, 1.0f}); // Beijing - red
    addPin(31.2304, 121.4737, {1.0f, 0.8f, 0.0f, 1.0f}); // Shanghai - yellow
    addPin(22.3193, 114.1694, {0.2f, 1.0f, 0.2f, 1.0f}); // Hong Kong - green
    addPin(40.7128, -74.0060, {0.2f, 0.5f, 1.0f, 1.0f}); // New York - blue
    addPin(51.5074, -0.1278, {1.0f, 0.5f, 1.0f, 1.0f});  // London - magenta
  }

  std::cout << "\n=== Earth Demo Controls ===\n"
            << "  Left drag  : pan globe\n"
            << "  Right drag : orbit camera\n"
            << "  Scroll     : zoom\n"
            << "  1          : toggle imagery tiles\n"
            << "  2          : toggle labels\n"
            << "  3          : fly to Beijing (close-up)\n"
            << "  4          : fly to Mt. Everest (terrain demo)\n"
            << "  R          : reset view\n"
            << "  C          : copy camera position as --look-at flag (clipboard)\n"
            << "  ESC        : quit\n"
            << "===========================\n"
            << std::endl;
}

void EarthDemoApp::writeStatsJson() const {
  if (cli_.stats_json.empty() || !globe) {
    return;
  }

  const GlobeStreamingStats stats = globe->getStreamingStats();
  nlohmann::json output = {
      {"schema", "hongtu.earth_streaming_stats.v1"},
      {"imagery_provider", stats.imagery_provider},
      {"cache_namespace", stats.cache_namespace},
      {"camera", {{"altitude_m", camera.getAltitude()}, {"pitch_deg", cli_.pitch_deg}, {"look_at", cli_.look_at}}},
      {"streaming",
       {{"visible_tiles", stats.visible_tiles},
        {"drawing_tiles", stats.drawing_tiles},
        {"loaded_tiles", stats.loaded_tiles},
        {"pending_tiles", stats.pending_tiles},
        {"decoded_tiles", stats.decoded_tiles},
        {"visible_min_level", stats.visible_min_level},
        {"visible_max_level", stats.visible_max_level},
        {"drawing_min_level", stats.drawing_min_level},
        {"drawing_max_level", stats.drawing_max_level},
        {"mipped_tiles", stats.mipped_tiles},
        {"tile_mip_levels_min", stats.tile_mip_levels_min},
        {"tile_mip_levels_max", stats.tile_mip_levels_max},
        {"lod_transition_tiles", stats.lod_transition_tiles},
        {"lod_transition_edges", stats.lod_transition_edges},
        {"source_transition_tiles", stats.source_transition_tiles},
        {"source_transition_edges", stats.source_transition_edges},
        {"queued_downloads", stats.queued_downloads},
        {"active_downloads", stats.active_downloads},
        {"cache_hits", stats.cache_hits},
        {"cache_misses", stats.cache_misses},
        {"cache_memory_usage_bytes", stats.cache_memory_usage},
        {"cache_memory_budget_bytes", stats.cache_memory_budget}}}};

  if (huilianImagery_) {
    constexpr double kHuilianLat = 22.74776854553;
    constexpr double kHuilianLon = 114.599640461187;
    nlohmann::json probes = nlohmann::json::object();
    for (const int z : {3, 4, 8, 10}) {
      const gis::TileCoord coord = gis::TileCoord::fromGeodetic(kHuilianLat, kHuilianLon, z);
      probes["z" + std::to_string(z)] = {{"coord", coord.toString()},
                                         {"route", huilianImagery_->resolveRouteName(coord)}};
    }
    output["huilian_routes"] = {
        {"route_count", huilianImagery_->getRouteCount()},
        {"tianditu_offline_z1_z4_requests", huilianImagery_->getRouteRequestCount("tianditu-offline-z1-z4")},
        {"south_china_offline_z8_requests", huilianImagery_->getRouteRequestCount("south-china-offline-z8")},
        {"huilian_offline_z10_requests", huilianImagery_->getRouteRequestCount("huilian-offline-z10")},
        {"fallback_requests", huilianImagery_->getFallbackRequestCount()},
        {"probes", probes}};
  }

  const std::filesystem::path path(cli_.stats_json);
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }
  std::ofstream file(path);
  if (!file) {
    spdlog::warn("[stats-json] could not open {}", path.string());
    return;
  }
  file << output.dump(2) << '\n';
  spdlog::info("[stats-json] wrote {}", path.string());
}

void EarthDemoApp::setupCamera() {
  camera.setScreenSize(static_cast<f32>(windowWidth), static_cast<f32>(windowHeight));
  auto startPos = Geodetic::fromDegrees(20.0, 105.0, 0.0);
  camera.lookAt(startPos, 18000000.0, 0.0, -90.0);
  camera.setInteractive(true);
  interactiveEnabled = true;
}

// =============================================================================
// City model overlay — opt-in via --city PATH
// =============================================================================

bool EarthDemoApp::setupCity() {
  if (cli_.city_model.empty())
    return true; // not requested, fine

  // ---- load glTF / glb -----------------------------------------------------
  hongtu::asset::GltfLoader loader;
  auto md = loader.load(cli_.city_model);
  if (!md.has_value()) {
    std::cerr << "[city] failed to load " << cli_.city_model << ": " << loader.lastError() << "\n";
    return false;
  }
  cityModel_ = std::move(*md);

  // ---- materials -----------------------------------------------------------
  if (!cityMaterialFactory_.initialize(device))
    return false;
  {
    auto fb = cityMaterialFactory_.createFallback();
    if (!fb)
      return false;
    cityMaterials_.push_back(std::move(*fb));
  }
  auto pickTex = [&](int idx) -> const hongtu::asset::TextureData* {
    if (idx < 0 || idx >= static_cast<int>(cityModel_.textures.size()))
      return nullptr;
    const auto& t = cityModel_.textures[idx];
    if (t.pixels.empty() || t.width <= 0 || t.height <= 0)
      return nullptr;
    return &t;
  };
  for (const auto& mat : cityModel_.materials) {
    const auto* base = pickTex(mat.base_color_texture);
    const auto* normal = pickTex(mat.normal_texture);
    const auto* mr = pickTex(mat.metallic_roughness_texture);
    auto pm = cityMaterialFactory_.create(base ? base->pixels.data() : nullptr,
                                          base ? base->width : 0,
                                          base ? base->height : 0,
                                          normal ? normal->pixels.data() : nullptr,
                                          normal ? normal->width : 0,
                                          normal ? normal->height : 0,
                                          mr ? mr->pixels.data() : nullptr,
                                          mr ? mr->width : 0,
                                          mr ? mr->height : 0,
                                          mat.base_color_factor,
                                          mat.metallic_factor,
                                          mat.roughness_factor);
    if (!pm)
      pm = cityMaterials_[0];
    cityMaterials_.push_back(std::move(*pm));
  }

  // ---- buffers + per-primitive draws --------------------------------------
  std::vector<MeshVertex> verts;
  std::vector<uint32_t> idx;
  uint32_t vertex_base = 0;
  glm::vec3 bbox_min(1e30f), bbox_max(-1e30f);
  for (const auto& mesh : cityModel_.meshes) {
    const bool has_n = mesh.normals.size() == mesh.positions.size();
    const bool has_u = mesh.uvs.size() == mesh.positions.size();
    for (size_t i = 0; i < mesh.positions.size(); ++i) {
      MeshVertex v;
      v.position = mesh.positions[i];
      v.normal = has_n ? mesh.normals[i] : glm::vec3(0.0f, 0.0f, 1.0f);
      v.uv = has_u ? mesh.uvs[i] : glm::vec2(0.0f);
      bbox_min = glm::min(bbox_min, v.position);
      bbox_max = glm::max(bbox_max, v.position);
      verts.push_back(v);
    }
    CityPrimitive p;
    p.index_offset = static_cast<uint32_t>(idx.size());
    p.index_count = static_cast<uint32_t>(mesh.indices.size());
    p.material_id = mesh.material_id;
    cityPrimitives_.push_back(p);
    for (uint32_t k : mesh.indices)
      idx.push_back(vertex_base + k);
    vertex_base += static_cast<uint32_t>(mesh.positions.size());
  }
  cityBboxMin_ = bbox_min;
  cityBboxMax_ = bbox_max;

  BufferDesc vbd;
  vbd.size = verts.size() * sizeof(MeshVertex);
  vbd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
  vbd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
  cityVbo_ = device->createBuffer(vbd);
  if (!cityVbo_)
    return false;
  std::memcpy(cityVbo_->map(), verts.data(), vbd.size);
  cityVbo_->unmap();

  BufferDesc ibd;
  ibd.size = idx.size() * sizeof(uint32_t);
  ibd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Index);
  ibd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
  cityIbo_ = device->createBuffer(ibd);
  if (!cityIbo_)
    return false;
  std::memcpy(cityIbo_->map(), idx.data(), ibd.size);
  cityIbo_->unmap();

  // ---- shadow stub: ShadowPass owns the descriptor the PBR pipeline expects
  // at set=1, but we never actually render shadows here — push constant
  // shadow_strength = 0 mixes the shadow lookup out. Still need a one-shot
  // empty render so the depth target leaves Undefined layout.
  if (!cityShadowPass_.initialize(device, 1024u))
    return false;
  cityShadowPass_.render(glm::vec3(0, 0, 1), glm::vec3(0), 1.0f, cityVbo_, cityIbo_, {}, {});

  // ---- PBR pipeline against earth_demo's existing render pass --------------
  if (!cityPipeline_.initializeWithRenderPass(device,
                                              cityMaterialFactory_.getDescriptorSetLayout(),
                                              cityShadowPass_.getDescriptorSetLayout(),
                                              renderPass)) {
    std::cerr << "[city] PbrForwardPipeline init failed\n";
    return false;
  }

  // ---- placement -----------------------------------------------------------
  cityEntity_.setPosition(Geodetic::fromDegrees(cli_.city_lat_deg, cli_.city_lon_deg, cli_.city_alt_m));
  cityEntity_.setScale(1.0f);

  // ---- aim the camera at the model -----------------------------------------
  const float diag = glm::length(bbox_max - bbox_min);
  const double view_distance_m = std::max(static_cast<double>(diag) * 1.2, 500.0);
  // Drop the orbit floor so scroll-in can punch through the default
  // 200 m "stay above buildings" safety margin. 5 m gets us right onto
  // a window without poking through the model.
  camera.setMinDistance(5.0);
  camera.lookAt(Geodetic::fromDegrees(cli_.city_lat_deg, cli_.city_lon_deg, cli_.city_alt_m + diag * 0.5),
                view_distance_m,
                /*heading*/ 30.0,
                /*pitch*/ -25.0);

  cityLoaded_ = true;
  std::cout << "[city] " << cityModel_.source_path << " — " << cityModel_.meshes.size() << " primitives, "
            << cityModel_.materials.size() << " materials, bbox diag=" << diag << " m, placed at (" << cli_.city_lat_deg
            << ", " << cli_.city_lon_deg << ", alt=" << cli_.city_alt_m << ")\n";
  return true;
}

void EarthDemoApp::renderCity() {
  if (!cityLoaded_)
    return;

  // The skybox pass / globe pass already bound their own pipelines — we
  // need to rebind PBR + our VBO/IBO here.
  commandBuffer->bindPipeline(cityPipeline_.getPipeline());
  auto layout = cityPipeline_.getPipelineLayout();
  commandBuffer->bindDescriptorSet(1u, cityShadowPass_.getDescriptorSet(), layout);
  commandBuffer->bindVertexBuffer(0u, cityVbo_);
  commandBuffer->bindIndexBuffer(cityIbo_);

  // Camera + RTC plumbing — same pattern geo_pbr_demo uses.
  const auto& rtc_origin = camera.getRTCOrigin();
  const float aspect = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
  // Use the globe renderer's projection straight — it already includes the
  // Vulkan Y-flip (see GlobeCamera::getProjectionMatrix). Adding another
  // flip here would render the city upside-down relative to the terrain.
  glm::mat4 proj = camera.getProjectionMatrix(aspect);
  glm::mat4 view = camera.getRTCViewMatrix();
  glm::mat4 model = cityEntity_.getRTCModelMatrix(rtc_origin, hongtu::coordinate::WGS84);
  glm::mat4 mvp = proj * view * model;

  glm::dvec3 eye_ecef = camera.getEyeWorld();
  glm::vec3 eye_rtc = rtc_origin.ecefToRTC(eye_ecef);

  // Sun: arbitrary "afternoon" direction in world space.
  glm::vec3 sun_dir_ws = glm::normalize(glm::vec3(0.4f, 0.6f, 0.8f));

  for (const auto& prim : cityPrimitives_) {
    const size_t mat_slot = (prim.material_id < 0 || static_cast<size_t>(prim.material_id + 1) >= cityMaterials_.size())
                                ? 0u
                                : static_cast<size_t>(prim.material_id + 1);
    const auto& gm = cityMaterials_[mat_slot];
    if (!gm.desc_set)
      continue;
    commandBuffer->bindDescriptorSet(0u, gm.desc_set, layout);

    PbrForwardPipeline::PushConstants pd{};
    pd.model = model;
    pd.mvp = mvp;
    pd.base_color = gm.base_color;
    pd.mr_params = glm::vec4(gm.metallic, gm.roughness, 0.0f, 0.0f);
    pd.camera_pos_ws = glm::vec4(eye_rtc, 0.0f);
    pd.light_dir_ws = glm::vec4(sun_dir_ws, 4.0f); // intensity = 4 (HDR)
    pd.light_vp = glm::mat4(1.0f);                 // ignored (shadow_strength=0)
    // .x sky_scale, .y IBL diffuse mul, .z exposure, .w shadow_strength=0
    pd.env_params = glm::vec4(1.0f, 1.4f, 1.0f, 0.0f);
    // .x skylight_bleed, .y render_mode=1 (PBR), .z lod_bias, .w inline_tonemap=1
    pd.env_params2 = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    commandBuffer->pushConstants(layout,
                                 static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex) |
                                     static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment),
                                 0,
                                 sizeof(pd),
                                 &pd);
    commandBuffer->drawIndexed(prim.index_count, 1u, prim.index_offset);
  }
}

bool EarthDemoApp::initialize() {
  std::cout << "Initializing window..." << std::endl;
  if (!initWindow()) {
    std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
    return false;
  }
  std::cout << "Initializing Vulkan..." << std::endl;
  if (!initVulkan()) {
    std::cerr << "Failed to initialize Vulkan" << std::endl;
    SDL_DestroyWindow(window);
    SDL_Quit();
    return false;
  }
  std::cout << "Creating render pass and depth buffer..." << std::endl;
  if (!createRenderPass()) {
    std::cerr << "Failed to create render pass" << std::endl;
    shutdown();
    return false;
  }
  if (!createDepthBuffer()) {
    std::cerr << "Failed to create depth buffer" << std::endl;
    shutdown();
    return false;
  }
  if (!createFramebuffers() || !createSync()) {
    std::cerr << "Failed to create framebuffers/sync" << std::endl;
    shutdown();
    return false;
  }

  setupGlobe();
  setupCamera();
  if (!setupCity()) {
    std::cerr << "Failed to set up --city overlay (continuing without it)" << std::endl;
  }

  startTime = std::chrono::high_resolution_clock::now();
  lastTime = startTime;

  std::cout << "\n=== XNBY Engine - Digital Earth Demo ===" << std::endl;
  std::cout << "  Earth texture: texture/earth/0_0_0.jpg + 0_1_0.jpg" << std::endl;
  std::cout << "\nControls: Left drag - Pan | Right drag - Orbit | Scroll - Zoom | Escape - Quit" << std::endl;
  return true;
}

void EarthDemoApp::handleInput(const SDL_Event& e) {
  switch (e.type) {
    case SDL_MOUSEWHEEL:
      if (interactiveEnabled) {
        // Use sub-unit precision on macOS trackpad (SDL 2.0.18+).
        // Fall back to integer y on older SDL.
#if SDL_VERSION_ATLEAST(2, 0, 18)
        camera.onMouseScroll(e.wheel.preciseY);
#else
        camera.onMouseScroll(static_cast<float>(e.wheel.y));
#endif
      }
      break;
    case SDL_MOUSEBUTTONDOWN:
      if (interactiveEnabled) {
        if (e.button.button == SDL_BUTTON_LEFT) {
          dragging = true;
          camera.onMouseDown(static_cast<float>(e.button.x), static_cast<float>(e.button.y));
        } else if (e.button.button == SDL_BUTTON_RIGHT) {
          rightDragging = true;
          lastMouse = {static_cast<float>(e.button.x), static_cast<float>(e.button.y)};
        }
      }
      break;
    case SDL_MOUSEBUTTONUP:
      if (e.button.button == SDL_BUTTON_LEFT) {
        dragging = false;
        camera.onMouseUp();
      } else if (e.button.button == SDL_BUTTON_RIGHT) {
        rightDragging = false;
      }
      break;
    case SDL_MOUSEMOTION:
      if (dragging && interactiveEnabled) {
        // Geodetic drag: pass absolute position
        camera.onMouseMove(static_cast<float>(e.motion.x), static_cast<float>(e.motion.y));
      }
      if (rightDragging && interactiveEnabled) {
        // Orbit tilt / heading: pass delta
        glm::vec2 cur = {static_cast<float>(e.motion.x), static_cast<float>(e.motion.y)};
        glm::vec2 delta = cur - lastMouse;
        camera.onRightDrag(delta.x, delta.y);
        lastMouse = cur;
      }
      break;
    default:
      break;
  }
}

void EarthDemoApp::renderFrame(float dt) {
  fence->wait();
  fence->reset();
  uint32_t imgIdx = device->acquireNextImage(imageAvailSema);
  lastImgIdx_ = imgIdx;

  // Update camera
  camera.update(dt);

  // Update globe mesh and load imagery tile overlays.
  globe->update(dt, camera);

  // Begin command buffer
  commandBuffer->reset();
  commandBuffer->begin();

  Rect2D area;
  area.offset = {0, 0};
  area.extent = device->getSwapchainExtent();
  // Dark space blue clear color
  std::vector<std::array<float, 4>> clearColors = {{0.0f, 0.0f, 0.02f, 1.0f}};
  commandBuffer->beginRenderPass(renderPass, framebuffers[imgIdx], area, clearColors);

  Viewport vp;
  vp.x = 0;
  vp.y = 0;
  vp.width = static_cast<float>(area.extent.width);
  vp.height = static_cast<float>(area.extent.height);
  vp.minDepth = 0;
  vp.maxDepth = 1;
  commandBuffer->setViewport(vp);
  commandBuffer->setScissor(area);

  // Render the globe
  globe->render(commandBuffer, camera);

  // City overlay (PBR forward path) — only if --city was passed.
  renderCity();

  commandBuffer->endRenderPass();
  commandBuffer->end();

  device->submit({commandBuffer}, {imageAvailSema}, {renderDoneSema}, fence);
  device->present(renderDoneSema);
}

void EarthDemoApp::run() {
  bool running = true;
  SDL_Event event;

  // Apply --look-at before the first frame so tile streaming has time to
  // chase the destination through the warmup window. lookAt is instantaneous
  // (no fly animation), so the camera is settled immediately.
  if (cli_.no_tiles && globe) {
    globe->setTileRenderingEnabled(false);
  }

  if (!cli_.look_at.empty()) {
    double lat = 20.0, lon = 105.0, alt_m = 18'000'000.0;
    if (cli_.look_at == "beijing") {
      lat = 39.9042;
      lon = 116.4074;
      alt_m = 500'000.0;
    } else if (cli_.look_at == "everest") {
      lat = 27.9881;
      lon = 86.9250;
      alt_m = 200'000.0;
    } else if (cli_.look_at == "reset") { /* defaults */
    } else {
      // Parse "lat,lon,alt_km" — altitude in kilometers for ergonomics.
      double a = 0, b = 0, c = 0;
      if (std::sscanf(cli_.look_at.c_str(), "%lf,%lf,%lf", &a, &b, &c) == 3) {
        lat = a;
        lon = b;
        alt_m = c * 1000.0;
      } else {
        spdlog::warn("[--look-at] could not parse '{}', using reset view", cli_.look_at);
      }
    }
    camera.lookAt(Geodetic::fromDegrees(lat, lon, 0.0), alt_m, 0.0, cli_.pitch_deg);
    spdlog::info("[--look-at] camera at lat={} lon={} alt={}m pitch={}", lat, lon, alt_m, cli_.pitch_deg);
  }

  // In autotest mode, disable interactive input: SDL emits phantom mouse
  // motion events on some macOS configurations even with no user input,
  // and handleInput() would record them as drags, seeding inertia that
  // slowly drifts the camera off the requested look-at over ~10 seconds.
  if (cli_.exit_frames > 0) {
    camera.setInteractive(false);
  }

  int frame_idx = 0;
  while (running) {
    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;

    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
        break;
      }
      if (event.type == SDL_KEYDOWN) {
        SDL_Keycode k = event.key.keysym.sym;
        if (k == SDLK_ESCAPE) {
          running = false;
          break;
        }
        if (k == SDLK_1) {
          tilesOn = !tilesOn;
          globe->setTileRenderingEnabled(tilesOn);
          spdlog::info("[Key 1] imagery tiles = {}", tilesOn);
        }
        if (k == SDLK_2) {
          labelsOn = !labelsOn;
          if (labelsOn) {
            // Re-add the demo labels
            auto add = [&](double lat, double lon, glm::vec4 c) {
              Label l;
              l.position = Geodetic::fromDegrees(lat, lon, 0.0);
              l.color = c;
              l.size_meters = 500000.0f;
              globe->addLabel(l);
            };
            add(39.9042, 116.4074, {1.0f, 0.2f, 0.2f, 1.0f});
            add(31.2304, 121.4737, {1.0f, 0.8f, 0.0f, 1.0f});
            add(22.3193, 114.1694, {0.2f, 1.0f, 0.2f, 1.0f});
            add(40.7128, -74.0060, {0.2f, 0.5f, 1.0f, 1.0f});
            add(51.5074, -0.1278, {1.0f, 0.5f, 1.0f, 1.0f});
          } else {
            globe->clearLabels();
          }
          spdlog::info("[Key 2] labels = {}", labelsOn);
        }
        if (k == SDLK_3) {
          camera.flyTo(Geodetic::fromDegrees(39.9042, 116.4074, 0.0), 500000.0, 3.0);
          spdlog::info("[Key 3] flying to Beijing");
        }
        if (k == SDLK_4) {
          camera.flyTo(Geodetic::fromDegrees(27.9881, 86.9250, 0.0), 200000.0, 3.0);
          spdlog::info("[Key 4] flying to Mt. Everest");
        }
        if (k == SDLK_r) {
          camera.lookAt(Geodetic::fromDegrees(20.0, 105.0, 0.0), 18000000.0, 0.0, -90.0);
          spdlog::info("[Key R] reset view");
        }
        if (k == SDLK_c) {
          // Print + copy the current camera position as a ready-to-paste
          // CLI flag, so the user can hand me an exact repro for any
          // visual bug they spot.
          Geodetic g = camera.getPositionGeodetic();
          double lat_deg = 0, lon_deg = 0;
          g.toDegrees(lat_deg, lon_deg);
          double alt_km = camera.getAltitude() / 1000.0;
          char cli[256];
          std::snprintf(cli, sizeof(cli), "--look-at=%.6f,%.6f,%.3f", lat_deg, lon_deg, alt_km);
          spdlog::info("[Key C] camera: {}", cli);
          // SDL provides one clipboard path for Windows, macOS and Linux.
          if (SDL_SetClipboardText(cli) == 0) {
            spdlog::info("[Key C] copied to clipboard");
          } else {
            spdlog::warn("[Key C] clipboard unavailable: {}", SDL_GetError());
          }
        }
      }
      handleInput(event);
    }
    if (!running)
      break;

    renderFrame(dt);

    // FPS and status display in title bar
    frameCount++;
    fpsTimer += static_cast<double>(dt);
    if (fpsTimer >= 1.0) {
      double fps = frameCount / fpsTimer;
      double alt = camera.getAltitude();
      std::string altStr;
      if (alt >= 1000000.0) {
        altStr = std::to_string(static_cast<int>(alt / 1000.0)) + " km";
      } else if (alt >= 1000.0) {
        altStr = std::to_string(static_cast<int>(alt)) + " m";
      } else {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f m", alt);
        altStr = buf;
      }

      int vis = globe ? globe->getVisibleTileCount() : 0;
      int load = globe ? globe->getLoadedTileCount() : 0;
      int pend = globe ? globe->getPendingTileCount() : 0;

      std::string title = "XNBY Earth | Alt: " + altStr + " | " + std::to_string(static_cast<int>(fps)) + " FPS" +
                          " | Tiles vis:" + std::to_string(vis) + " load:" + std::to_string(load) +
                          " pend:" + std::to_string(pend) + " | [1]tiles [2]labels [3]Beijing [4]Everest [R]reset";
      SDL_SetWindowTitle(window, title.c_str());
      if (cli_.dump_stats && globe) {
        spdlog::info("[stats] alt={:.0f}m fps={:.1f} vis={} load={} pend={}", alt, fps, vis, load, pend);
      }
      frameCount = 0;
      fpsTimer = 0;
    }

    // --frames N → exit after N frames. The screenshot is captured via
    // Vulkan image readback after the last rendered frame's fence has
    // signaled, then the demo exits.
    ++frame_idx;
    if (cli_.exit_frames > 0 && frame_idx >= cli_.exit_frames) {
      if (cli_.dump_drawing_set && globe) {
        globe->dumpDrawingSet();
      }
      if (!cli_.screenshot.empty()) {
        fence->wait(); // ensure the last rendered frame is done
        std::vector<uint8_t> rgba;
        uint32_t W = 0, H = 0;
        if (device->readbackSwapchainImage(lastImgIdx_, rgba, W, H)) {
          if (stbi_write_png(cli_.screenshot.c_str(),
                             static_cast<int>(W),
                             static_cast<int>(H),
                             4,
                             rgba.data(),
                             static_cast<int>(W) * 4) != 0) {
            spdlog::info("[screenshot] wrote {} ({}x{})", cli_.screenshot, W, H);
          } else {
            spdlog::warn("[screenshot] stbi_write_png failed for {}", cli_.screenshot);
          }
        } else {
          spdlog::warn("[screenshot] swapchain readback failed");
        }
      }
      writeStatsJson();
      running = false;
    }
  }
  device->waitIdle();
}

void EarthDemoApp::shutdown() {
  if (device) {
    device->waitIdle();
    // City overlay holds RHI handles too — release before the device dies,
    // otherwise their shared_ptr destructors try to call into a freed device
    // and the Vulkan loader screams `vkDestroyBuffer: Invalid device`.
    cityIbo_.reset();
    cityVbo_.reset();
    cityMaterials_.clear();
    cityPipeline_ = PbrForwardPipeline{};
    cityShadowPass_ = ShadowPass{};
    cityMaterialFactory_ = PbrMaterialFactory{};
    globe.reset();
    commandBuffer.reset();
    fence.reset();
    imageAvailSema.reset();
    renderDoneSema.reset();
    framebuffers.clear();
    depthView.reset();
    depthTexture.reset();
    renderPass.reset();
    delete device;
    device = nullptr;
  }
  if (window) {
    SDL_DestroyWindow(window);
    SDL_Quit();
  }
}

// ============================================================================
int main(int argc, char* argv[]) {
#if defined(_WIN32)
  SDL_SetMainReady();
#endif
#ifdef __APPLE__
  // Auto-configure MoltenVK on macOS if not already set
  if (!std::getenv("VK_ICD_FILENAMES")) {
    const char* icd = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json";
    if (std::ifstream(icd).good()) {
      setenv("VK_ICD_FILENAMES", icd, 0);
      setenv("VK_DRIVER_FILES", icd, 0);
    }
  }
#endif

  DemoCli cli;
  auto starts_with = [](const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
  };
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--help" || a == "-h") {
      std::cout << "XNBY Earth Demo\n\n"
                << "Interactive controls:\n"
                << "  Left drag / Right drag / Scroll — pan, orbit, zoom\n"
                << "  1/2/3/4/R — toggle tiles, labels, fly-to Beijing/Everest, reset\n\n"
                << "Autotest flags (for scripted runs + screenshots):\n"
                << "  --look-at=NAME|lat,lon,alt_km    teleport at startup\n"
                << "                                   NAME: beijing | everest | reset\n"
                << "  --frames=N                       exit after N frames (auto headless run)\n"
                << "  --screenshot=PATH                capture the final Vulkan frame before exit\n"
                << "  --dump-stats                     log alt/fps/vis/load/pend each second\n"
                << "  --no-labels                      suppress city-marker billboards\n"
                << "  --dump-drawing-set               dump per-tile drawing-set state at exit\n"
                << "  --pitch=<deg>                    camera pitch (-90=down, 0=horizon, default -90)\n"
                << "  --city=PATH                      load a glTF/glb city model and place it on the globe\n"
                << "  --city-lat=<deg>                 city latitude  (default 39.9042 — Beijing)\n"
                << "  --city-lon=<deg>                 city longitude (default 116.4074)\n"
                << "  --city-alt=<m>                   city base altitude AMSL (default 0)\n"
                << "  --sun-lon=<rad>                  pin sun orbital longitude (parks ocean glint)\n"
                << "  --water-poly=lat,lon:lat,lon:... local water body boundary polygon\n"
                << "  --water-level=<m>                water level altitude for --water-poly (default 0)\n"
                << "  --imagery=esri|huilian           use ArcGIS or routed Huilian offline imagery\n"
                << "  --stats-json=PATH                write final LOD/cache/route telemetry JSON\n";
      return 0;
    }
    if (starts_with(a, "--look-at="))
      cli.look_at = a.substr(10);
    else if (starts_with(a, "--frames="))
      cli.exit_frames = std::atoi(a.c_str() + 9);
    else if (starts_with(a, "--screenshot="))
      cli.screenshot = a.substr(13);
    else if (a == "--dump-stats")
      cli.dump_stats = true;
    else if (a == "--no-labels")
      cli.no_labels = true;
    else if (a == "--dump-drawing-set")
      cli.dump_drawing_set = true;
    else if (a == "--no-tiles")
      cli.no_tiles = true;
    else if (starts_with(a, "--pitch="))
      cli.pitch_deg = std::atof(a.c_str() + 8);
    else if (starts_with(a, "--city="))
      cli.city_model = a.substr(7);
    else if (starts_with(a, "--city-lat="))
      cli.city_lat_deg = std::atof(a.c_str() + 11);
    else if (starts_with(a, "--city-lon="))
      cli.city_lon_deg = std::atof(a.c_str() + 11);
    else if (starts_with(a, "--city-alt="))
      cli.city_alt_m = std::atof(a.c_str() + 11);
    else if (starts_with(a, "--sun-lon="))
      cli.sun_lon = std::atof(a.c_str() + 10);
    else if (starts_with(a, "--water-poly="))
      cli.water_poly = a.substr(13);
    else if (starts_with(a, "--water-level="))
      cli.water_level_m = std::atof(a.c_str() + 14);
    else if (starts_with(a, "--imagery="))
      cli.imagery = a.substr(10);
    else if (starts_with(a, "--stats-json="))
      cli.stats_json = a.substr(13);
    else
      spdlog::warn("Unknown argument: {}", a);
  }

  EarthDemoApp app;
  app.setCli(cli);
  if (!app.initialize())
    return 1;
  app.run();
  app.shutdown();
  return 0;
}
