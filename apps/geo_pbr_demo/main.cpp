// HongTu Engine — global-mode PBR verification demo.
//
// Proves the shared-base refactor works end-to-end in global (earth) mode:
//   * GlobeCamera     (CameraBase)  — geodetic-target orbit / drag interaction
//   * GeoEntity       (Entity)      — places a glTF model at lat/lon/alt
//   * ForwardSceneRenderer          — engine PBR forward pipeline, identical
//                                      to what local_demo uses
//
// No globe tiles, no imagery, no atmosphere — just a model floating at the
// requested geodetic position viewed in ECEF / RTC. Exists purely to
// validate that the same engine renderer drives both coordinate modes.
//
// Controls:
//   Left drag    — orbit the geodetic target
//   Right drag   — adjust heading / pitch
//   Scroll       — zoom in/out
//   T            — cycle render mode (RAW / PBR / NORMAL / UV / MIP)
//   Esc          — quit

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <hongtu/asset/GltfLoader.h>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/renderer/globe/globe_camera.hpp>
#include <hongtu/renderer/pbr/forward_scene_renderer.hpp>
#include <hongtu/renderer/pbr/mesh_vertex.hpp>
#include <hongtu/renderer/pbr/pbr_material.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <hongtu/scene/geo_entity.hpp>

using namespace HongTu::RHI;
using hongtu::asset::GltfLoader;
using hongtu::asset::ModelData;
using hongtu::asset::TextureData;
using hongtu::coordinate::Geodetic;
using hongtu::coordinate::RTCOrigin;
using hongtu::renderer::ForwardSceneRenderer;
using hongtu::renderer::GlobeCamera;
using hongtu::renderer::MeshVertex;
using hongtu::renderer::PbrMaterial;
using hongtu::scene::GeoEntity;

using PrimitiveDraw = ForwardSceneRenderer::PrimitiveDraw;

class GeoPbrDemo {
 public:
  bool initialize();
  void run();
  void shutdown();
  void setModelPath(std::string p) { model_path_ = std::move(p); }
  void setPosition(const Geodetic& g) { geo_pos_ = g; }
  void setViewDistance(double m) { view_distance_m_ = m; }

 private:
  bool initWindow();
  bool initVulkan();
  bool createBuffers();
  bool createMaterialsFromModel(const ModelData& model);
  bool buildBuffersFromModel(const ModelData& model);
  void buildScene();
  void handleEvent(const SDL_Event& e);
  void render();
  void cleanup();

  SDL_Window* window_ = nullptr;
  int         windowW_ = 1280;
  int         windowH_ = 720;
  RHIDevice*  device_  = nullptr;

  RHIBufferPtr        vbo_, ibo_;

  ForwardSceneRenderer scene_renderer_;
  std::vector<PbrMaterial>   gpu_materials_;
  std::vector<PrimitiveDraw> primitives_;

  GlobeCamera       camera_;
  GeoEntity         entity_;
  Geodetic          geo_pos_{glm::radians(39.9042), glm::radians(116.4074), 50.0};  // Beijing
  double            view_distance_m_ = 200.0;

  std::string  model_path_;
  ModelData    model_data_;
  bool         model_loaded_ = false;
  glm::vec3    model_bbox_min_{0.0f}, model_bbox_max_{0.0f};

  // Lighting / mode controls.
  int   render_mode_   = 0;
  float lod_bias_      = 0.0f;
  glm::vec3 sun_dir_ws_{0.4f, 0.6f, 0.8f};

  // Input
  bool dragLeft_  = false;
  bool dragRight_ = false;
  glm::vec2 lastMouse_{0.0f, 0.0f};
};

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

bool GeoPbrDemo::initialize() {
  if (!initWindow())                            { std::cerr << "initWindow failed\n";  return false; }
  if (!initVulkan())                            { std::cerr << "initVulkan failed\n";  return false; }
  if (!scene_renderer_.initialize(device_))     { std::cerr << "scene_renderer init failed\n"; return false; }
  if (!createBuffers())                         { std::cerr << "createBuffers failed\n"; return false; }
  buildScene();
  return true;
}

bool GeoPbrDemo::initWindow() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return false;
  }
  window_ = SDL_CreateWindow("XNBY Engine - Geo PBR Demo (GlobeCamera + GeoEntity + PBR)",
                             SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED,
                             windowW_,
                             windowH_,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
  return window_ != nullptr;
}

bool GeoPbrDemo::initVulkan() {
  auto factory = Backend::createFactory(Backend::Type::Vulkan);
  if (!factory) return false;

  unsigned int extCount = 0;
  SDL_Vulkan_GetInstanceExtensions(window_, &extCount, nullptr);
  std::vector<const char*> exts(extCount);
  SDL_Vulkan_GetInstanceExtensions(window_, &extCount, exts.data());

  SwapchainDesc sc;
  sc.extent     = {static_cast<uint32_t>(windowW_), static_cast<uint32_t>(windowH_)};
  sc.format     = Format::R8G8B8A8_SRGB;
  sc.imageCount = 3;
  sc.vsync      = true;

  device_ = factory->createDeviceWithSwapchain(window_, sc, false, exts);
  Backend::destroyFactory(factory);
  return device_ != nullptr;
}

bool GeoPbrDemo::createMaterialsFromModel(const ModelData& model) {
  auto& factory = scene_renderer_.getMaterialFactory();
  gpu_materials_.clear();
  gpu_materials_.reserve(model.materials.size() + 1);

  auto fb = factory.createFallback();
  if (!fb) return false;
  gpu_materials_.push_back(std::move(*fb));

  auto pickTex = [&](int idx) -> const TextureData* {
    if (idx < 0 || idx >= static_cast<int>(model.textures.size())) return nullptr;
    const auto& t = model.textures[idx];
    if (t.pixels.empty() || t.width <= 0 || t.height <= 0) return nullptr;
    return &t;
  };

  for (const auto& mat : model.materials) {
    const auto* base   = pickTex(mat.base_color_texture);
    const auto* normal = pickTex(mat.normal_texture);
    const auto* mr     = pickTex(mat.metallic_roughness_texture);
    auto pm = factory.create(
        base   ? base->pixels.data()   : nullptr, base   ? base->width   : 0, base   ? base->height   : 0,
        normal ? normal->pixels.data() : nullptr, normal ? normal->width : 0, normal ? normal->height : 0,
        mr     ? mr->pixels.data()     : nullptr, mr     ? mr->width     : 0, mr     ? mr->height     : 0,
        mat.base_color_factor, mat.metallic_factor, mat.roughness_factor);
    if (!pm) pm = gpu_materials_[0];
    gpu_materials_.push_back(std::move(*pm));
  }
  return true;
}

bool GeoPbrDemo::buildBuffersFromModel(const ModelData& model) {
  std::vector<MeshVertex> verts;
  std::vector<uint32_t>   idx;
  uint32_t vertex_base = 0;
  glm::vec3 bbox_min(1e30f), bbox_max(-1e30f);
  primitives_.clear();
  primitives_.reserve(model.meshes.size());

  for (const auto& mesh : model.meshes) {
    const bool has_normals = mesh.normals.size() == mesh.positions.size();
    const bool has_uvs     = mesh.uvs.size()     == mesh.positions.size();
    for (size_t i = 0; i < mesh.positions.size(); ++i) {
      MeshVertex v;
      v.position = mesh.positions[i];
      v.normal   = has_normals ? mesh.normals[i] : glm::vec3(0.0f, 1.0f, 0.0f);
      v.uv       = has_uvs     ? mesh.uvs[i]     : glm::vec2(0.0f, 0.0f);
      bbox_min = glm::min(bbox_min, v.position);
      bbox_max = glm::max(bbox_max, v.position);
      verts.push_back(v);
    }
    PrimitiveDraw pd;
    pd.index_offset = static_cast<uint32_t>(idx.size());
    pd.index_count  = static_cast<uint32_t>(mesh.indices.size());
    pd.material_id  = mesh.material_id;
    primitives_.push_back(pd);
    for (uint32_t k : mesh.indices) idx.push_back(vertex_base + k);
    vertex_base += static_cast<uint32_t>(mesh.positions.size());
  }
  model_bbox_min_ = bbox_min;
  model_bbox_max_ = bbox_max;

  if (verts.empty() || idx.empty()) return false;

  BufferDesc vbd;
  vbd.size  = verts.size() * sizeof(MeshVertex);
  vbd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
  vbd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
  vbo_ = device_->createBuffer(vbd);
  if (!vbo_) return false;
  std::memcpy(vbo_->map(), verts.data(), vbd.size);
  vbo_->unmap();

  BufferDesc ibd;
  ibd.size  = idx.size() * sizeof(uint32_t);
  ibd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Index);
  ibd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
  ibo_ = device_->createBuffer(ibd);
  if (!ibo_) return false;
  std::memcpy(ibo_->map(), idx.data(), ibd.size);
  ibo_->unmap();

  std::cout << "[model] " << model.source_path << " — "
            << verts.size() << " verts, " << idx.size() << " indices, "
            << model.meshes.size() << " primitives, "
            << model.materials.size() << " materials\n";
  return true;
}

bool GeoPbrDemo::createBuffers() {
  if (model_path_.empty()) {
    std::cerr << "geo_pbr_demo requires --model PATH (no built-in fallback)\n";
    return false;
  }
  GltfLoader loader;
  auto md = loader.load(model_path_);
  if (!md.has_value()) {
    std::cerr << "[model] failed to load " << model_path_ << ": " << loader.lastError() << "\n";
    return false;
  }
  model_data_ = std::move(*md);
  if (!createMaterialsFromModel(model_data_)) return false;
  model_loaded_ = buildBuffersFromModel(model_data_);
  return model_loaded_;
}

void GeoPbrDemo::buildScene() {
  // Camera looks at the configured geodetic position from `view_distance_m_`
  // away. GlobeCamera::lookAt seeds the orbit target — the RTC origin tracks
  // the camera position internally.
  camera_.setScreenSize(static_cast<float>(windowW_), static_cast<float>(windowH_));
  camera_.lookAt(geo_pos_, view_distance_m_,
                  /*heading*/ 0.0, /*pitch*/ -0.3);
  camera_.setInteractive(true);

  // Entity at the requested lat/lon. Scale depends on the model's local
  // bbox — for big city assets keep scale = 1; for small meshes (cube, etc.)
  // the user passes --model and we just respect its native units.
  entity_.setPosition(geo_pos_);
  entity_.setScale(1.0f);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void GeoPbrDemo::handleEvent(const SDL_Event& e) {
  switch (e.type) {
    case SDL_MOUSEBUTTONDOWN:
      lastMouse_ = {static_cast<float>(e.button.x), static_cast<float>(e.button.y)};
      if (e.button.button == SDL_BUTTON_LEFT)  {
        dragLeft_  = true;  camera_.onMouseDown(lastMouse_.x, lastMouse_.y);
      }
      if (e.button.button == SDL_BUTTON_RIGHT) dragRight_ = true;
      break;
    case SDL_MOUSEBUTTONUP:
      if (e.button.button == SDL_BUTTON_LEFT)  { dragLeft_ = false; camera_.onMouseUp(); }
      if (e.button.button == SDL_BUTTON_RIGHT)   dragRight_ = false;
      break;
    case SDL_MOUSEMOTION: {
      glm::vec2 cur(static_cast<float>(e.motion.x), static_cast<float>(e.motion.y));
      glm::vec2 d = cur - lastMouse_;
      lastMouse_ = cur;
      if (dragLeft_)  camera_.onMouseMove(cur.x, cur.y);
      if (dragRight_) camera_.onRightDrag(d.x, d.y);
      break;
    }
    case SDL_MOUSEWHEEL:
      camera_.onMouseScroll(static_cast<float>(e.wheel.y));
      break;
    case SDL_KEYDOWN:
      if (e.key.keysym.sym == SDLK_t) {
        render_mode_ = (render_mode_ + 1) % 5;
        const char* names[] = {"RAW", "PBR", "NORMAL", "UV", "MIP"};
        std::cout << "[mode] " << names[render_mode_] << "\n";
      } else if (e.key.keysym.sym == SDLK_p) {
        // Profiler HUD: rolling-window CPU pass timings + culling stats.
        std::cout << scene_renderer_.getProfiler().getSummaryText() << "\n";
        const auto& cs = scene_renderer_.getLastCullingStats();
        std::cout << "[cull] entities " << cs.drawn_entities << "/"
                  << cs.total_entities << " (frustum culled "
                  << cs.culled_frustum << ")\n";
      }
      break;
    default: break;
  }
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void GeoPbrDemo::render() {
  camera_.update(0.016f);

  ForwardSceneRenderer::FrameParams fp;

  // Projection (flip Y for Vulkan, same as local_demo).
  const float aspect = static_cast<float>(windowW_) / static_cast<float>(windowH_);
  fp.proj = camera_.getProjectionMatrix(aspect);
  fp.proj[1][1] *= -1.0f;

  // RTC view matrix — both camera and model use the same RTCOrigin (the one
  // GlobeCamera owns) so geometry stays in float-precision near zero.
  fp.view = camera_.getRTCViewMatrix();

  // Camera position in RTC space (NOT raw ECEF — ECEF magnitudes are
  // ~6.4M m which would blow float precision in the shader).
  const RTCOrigin& origin = camera_.getRTCOrigin();
  glm::dvec3 eye_ecef = camera_.getEyeWorld();
  glm::vec3  eye_rtc  = origin.ecefToRTC(eye_ecef);
  fp.eye_world = eye_rtc;

  // Sun + scene bbox. The entity's RTC model matrix puts the model at some
  // RTC position; use that as the bbox center and inflate by the model's
  // own bbox extent. Light direction is a unit vector — coordinate-free.
  // Disambiguate to the earth-flavoured GeoEntity overload that knows the
  // ellipsoid; the base-class generic overload would treat the position as a
  // plain world point and miss the ENU rotation.
  glm::mat4 model_rtc_mat = entity_.getRTCModelMatrix(origin, hongtu::coordinate::WGS84);
  glm::vec3 model_rtc_pos = glm::vec3(model_rtc_mat[3]);
  fp.bbox_center   = model_rtc_pos;
  fp.bbox_extent   = std::max(glm::length(model_bbox_max_ - model_bbox_min_) * 0.5f, 5.0f);
  fp.sun_dir_world = glm::normalize(sun_dir_ws_);
  fp.sun_intensity = 4.0f;

  // Tunables — keep the same defaults as local_demo's "looks reasonable" baseline.
  fp.sky_scale         = 1.0f;
  fp.ibl_diffuse_scale = 1.4f;
  fp.exposure          = 1.0f;
  fp.shadow_strength   = 0.5f;
  fp.skylight_bleed    = 0.0f;
  fp.render_mode       = render_mode_;
  fp.lod_bias          = lod_bias_;

  // Clear background.
  if (render_mode_ == 1) {
    fp.clear_color_hdr = glm::vec3(2.4f, 2.4f, 2.6f);  // bright HDR sky for PBR
  } else {
    fp.clear_color_hdr = glm::vec3(0.78f, 0.78f, 0.80f);  // neutral grey for RAW / debug
  }

  // Single entity. Color tint is identity — material handles colour.
  ForwardSceneRenderer::EntityDraw ed;
  ed.model      = model_rtc_mat;
  ed.color_tint = glm::vec4(1.0f);
  // World AABB for frustum culling: 8 corners of local bbox through model_rtc_mat.
  {
    const glm::vec3 c[8] = {
        {model_bbox_min_.x, model_bbox_min_.y, model_bbox_min_.z},
        {model_bbox_max_.x, model_bbox_min_.y, model_bbox_min_.z},
        {model_bbox_min_.x, model_bbox_max_.y, model_bbox_min_.z},
        {model_bbox_max_.x, model_bbox_max_.y, model_bbox_min_.z},
        {model_bbox_min_.x, model_bbox_min_.y, model_bbox_max_.z},
        {model_bbox_max_.x, model_bbox_min_.y, model_bbox_max_.z},
        {model_bbox_min_.x, model_bbox_max_.y, model_bbox_max_.z},
        {model_bbox_max_.x, model_bbox_max_.y, model_bbox_max_.z}};
    glm::vec3 wmin( std::numeric_limits<float>::max());
    glm::vec3 wmax(-std::numeric_limits<float>::max());
    for (int k = 0; k < 8; ++k) {
      glm::vec3 w = glm::vec3(model_rtc_mat * glm::vec4(c[k], 1.0f));
      wmin = glm::min(wmin, w);
      wmax = glm::max(wmax, w);
    }
    ed.aabb_world.min_point = wmin;
    ed.aabb_world.max_point = wmax;
    ed.has_aabb = true;
  }
  std::vector<ForwardSceneRenderer::EntityDraw> entities{ed};

  scene_renderer_.renderFrame(fp, vbo_, ibo_, gpu_materials_,
                                entities, primitives_);
}

void GeoPbrDemo::run() {
  bool running = true;
  SDL_Event e;
  while (running) {
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) { running = false; break; }
      if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) {
        running = false; break;
      }
      handleEvent(e);
    }
    render();
  }
}

void GeoPbrDemo::shutdown() { cleanup(); }

void GeoPbrDemo::cleanup() {
  if (device_) {
    device_->waitIdle();
    gpu_materials_.clear();
    ibo_.reset();
    vbo_.reset();
    delete device_;
    device_ = nullptr;
  }
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  SDL_Quit();
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
#ifdef __APPLE__
  // Auto-configure MoltenVK on macOS if not already set — SDL_WINDOW_VULKAN
  // needs the loader to find the ICD. Same pattern as local_demo / earth_demo.
  if (!std::getenv("VK_ICD_FILENAMES")) {
    const char* icd = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json";
    if (std::ifstream(icd).good()) {
      setenv("VK_ICD_FILENAMES", icd, 0);
      setenv("VK_DRIVER_FILES",  icd, 0);
    }
  }
#endif

  GeoPbrDemo app;

  // Tiny CLI:  --model PATH  --lat DEG  --lon DEG  --alt M  --distance M
  std::string model_path;
  double lat = 39.9042, lon = 116.4074, alt = 50.0;
  double dist = 200.0;
  for (int i = 1; i + 1 < argc; ++i) {
    std::string a = argv[i];
    if      (a == "--model")    model_path = argv[++i];
    else if (a == "--lat")      lat        = std::stod(argv[++i]);
    else if (a == "--lon")      lon        = std::stod(argv[++i]);
    else if (a == "--alt")      alt        = std::stod(argv[++i]);
    else if (a == "--distance") dist       = std::stod(argv[++i]);
  }

  app.setModelPath(model_path);
  app.setPosition({glm::radians(lat), glm::radians(lon), alt});
  app.setViewDistance(dist);

  if (!app.initialize()) {
    std::cerr << "geo_pbr_demo initialize failed\n";
    return 1;
  }
  app.run();
  app.shutdown();
  return 0;
}
