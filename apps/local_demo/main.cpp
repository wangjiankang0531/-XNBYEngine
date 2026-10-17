// HongTu Engine — local-mode demo.
//
// Demonstrates the dual-coordinate API: this app uses LocalCamera (orbit +
// free-fly) and LocalEntity (TRS in flat-world meters), with no globe / no
// geodetic / no ellipsoid. The shared CameraBase + Entity bases mean the
// downstream rendering path is identical to what earth_demo uses — only the
// concrete camera and entity types differ.
//
// Scene: 8 lit cubes arranged in a circle around the origin.
// Controls:
//   Left drag    — orbit around the scene
//   Right drag   — adjust yaw / pitch
//   Scroll       — zoom in/out (orbit) or move forward (free-fly)
//   F            — toggle FreeFly / Orbit
//   WASD / QE    — translate in free-fly mode
//   R            — reset view
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
#include <glm/gtc/constants.hpp>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <hongtu/asset/GltfLoader.h>
#include <hongtu/asset/ShaderCompiler.h>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/renderer/camera/local_camera.hpp>
#include <hongtu/renderer/pbr/forward_scene_renderer.hpp>
#include <hongtu/renderer/pbr/mesh_vertex.hpp>
#include <hongtu/renderer/pbr/pbr_material.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <hongtu/scene/local_entity.hpp>

using namespace HongTu::RHI;
using namespace HongTu::Asset;
using hongtu::asset::GltfLoader;
using hongtu::asset::ModelData;
using hongtu::asset::TextureData;
using hongtu::coordinate::PrecisionOrigin;
using hongtu::renderer::ForwardSceneRenderer;
using hongtu::renderer::LocalCamera;
using hongtu::renderer::MeshVertex;
using hongtu::renderer::PbrMaterial;
using hongtu::scene::LocalEntity;

// ----------------------------------------------------------------------------
// Vertex — shared engine MeshVertex (position + normal + uv).
// ----------------------------------------------------------------------------

// Per-primitive geometry slice into the shared VBO/IBO. The orchestrator
// expects this shape via `ForwardSceneRenderer::PrimitiveDraw`.
using PrimitiveDraw = ForwardSceneRenderer::PrimitiveDraw;

// GpuMaterial / material-creation helpers now live in
// renderer/pbr/pbr_material.hpp as PbrMaterial / PbrMaterialFactory.

// ----------------------------------------------------------------------------
// Unit-cube mesh (24 vertices = 6 faces × 4 verts so per-face normals stay flat).
// ----------------------------------------------------------------------------

static std::vector<MeshVertex> makeCubeVertices() {
  // Each face is two triangles; 6 faces × 4 verts = 24 verts. Flat normals.
  // UVs map each face as (0,0)-(1,0)-(1,1)-(0,1) so a checker test texture
  // would tile correctly per face.
  auto v = [](glm::vec3 p, glm::vec3 n, glm::vec2 u) {
    return MeshVertex{p, n, u};
  };
  return {
    // +X
    v({ 0.5f,-0.5f,-0.5f}, { 1,0,0}, {0,0}),
    v({ 0.5f, 0.5f,-0.5f}, { 1,0,0}, {1,0}),
    v({ 0.5f, 0.5f, 0.5f}, { 1,0,0}, {1,1}),
    v({ 0.5f,-0.5f, 0.5f}, { 1,0,0}, {0,1}),
    // -X
    v({-0.5f,-0.5f, 0.5f}, {-1,0,0}, {0,0}),
    v({-0.5f, 0.5f, 0.5f}, {-1,0,0}, {1,0}),
    v({-0.5f, 0.5f,-0.5f}, {-1,0,0}, {1,1}),
    v({-0.5f,-0.5f,-0.5f}, {-1,0,0}, {0,1}),
    // +Y
    v({-0.5f, 0.5f,-0.5f}, { 0,1,0}, {0,0}),
    v({-0.5f, 0.5f, 0.5f}, { 0,1,0}, {1,0}),
    v({ 0.5f, 0.5f, 0.5f}, { 0,1,0}, {1,1}),
    v({ 0.5f, 0.5f,-0.5f}, { 0,1,0}, {0,1}),
    // -Y
    v({ 0.5f,-0.5f,-0.5f}, { 0,-1,0}, {0,0}),
    v({ 0.5f,-0.5f, 0.5f}, { 0,-1,0}, {1,0}),
    v({-0.5f,-0.5f, 0.5f}, { 0,-1,0}, {1,1}),
    v({-0.5f,-0.5f,-0.5f}, { 0,-1,0}, {0,1}),
    // +Z
    v({-0.5f,-0.5f, 0.5f}, { 0,0,1}, {0,0}),
    v({ 0.5f,-0.5f, 0.5f}, { 0,0,1}, {1,0}),
    v({ 0.5f, 0.5f, 0.5f}, { 0,0,1}, {1,1}),
    v({-0.5f, 0.5f, 0.5f}, { 0,0,1}, {0,1}),
    // -Z
    v({-0.5f, 0.5f,-0.5f}, { 0,0,-1}, {0,0}),
    v({ 0.5f, 0.5f,-0.5f}, { 0,0,-1}, {1,0}),
    v({ 0.5f,-0.5f,-0.5f}, { 0,0,-1}, {1,1}),
    v({-0.5f,-0.5f,-0.5f}, { 0,0,-1}, {0,1}),
  };
}

static std::vector<uint32_t> makeCubeIndices() {
  std::vector<uint32_t> idx;
  idx.reserve(36);
  for (uint32_t face = 0; face < 6; ++face) {
    uint32_t b = face * 4;
    idx.push_back(b + 0); idx.push_back(b + 1); idx.push_back(b + 2);
    idx.push_back(b + 0); idx.push_back(b + 2); idx.push_back(b + 3);
  }
  return idx;
}

// ----------------------------------------------------------------------------
// App
// ----------------------------------------------------------------------------

class LocalDemoApp {
 public:
  bool initialize();
  void run();
  void shutdown();

  void setModelPath(std::string p) { model_path_ = std::move(p); }

 private:
  bool initWindow();
  bool initVulkan();
  bool createBuffers();
  /// Build VBO/IBO from a loaded glTF model. Concatenates all primitives'
  /// geometry into one shared buffer while recording per-primitive
  /// (offset, count, material) so each material can be drawn individually.
  bool buildBuffersFromModel(const ModelData& model);
  /// Per-material PBR texture upload + descriptor set,now via engine API.
  bool createMaterialsFromModel(const ModelData& model);
  void buildScene();
  void handleEvent(const SDL_Event& e);
  void update(float dt);
  void render();
  void cleanup();
  void printParams() const;

  // SDL / RHI
  SDL_Window* window_ = nullptr;
  int         windowW_ = 1280;
  int         windowH_ = 720;
  RHIDevice*  device_  = nullptr;

  RHIBufferPtr        vbo_, ibo_;
  uint32_t            indexCount_ = 0;

  // The whole PBR forward path — material factory, shadow pass, HDR compose
  // target, main pipeline, depth RT, command buffer, sync — lives here.
  ForwardSceneRenderer scene_renderer_;

  // Runtime-tunable rendering parameters. Key bindings in handleEvent().
  struct RenderParams {
    // Defaults aim to PRESERVE the asset's material colours — lighting modulates
    // texture brightness uniformly, not via additive sky tints that wash out
    // dark-textured surfaces. skylight_bleed is OFF by default and only kicks
    // in when the user explicitly wants to lift pitch-black albedo (M key).
    float sun_intensity     = 4.0f;   // direct sunlight (HDR units)
    float sky_scale         = 1.0f;   // multiplier on analyticEnv() output
    float ibl_diffuse_scale = 1.4f;   // slight boost — IBL math is correct at 1.0
    float exposure          = 1.0f;   // pre-tonemap multiplier
    float shadow_strength   = 0.5f;   // 0=disable shadows, 1=full darkness
    float skylight_bleed    = 0.0f;   // OFF by default — texture colours stay true
    // SSAO — disabled by default; `O` ramps strength up, `P` ramps down.
    float ssao_strength     = 0.0f;   // 0=off, 1=full
    float ssao_radius_m     = 1.0f;
    float ssao_intensity    = 1.5f;
    float ssao_bias         = 0.025f;
    // Procedural sky on by default — uses the same analyticEnv() that IBL
    // samples, so visible sky and indirect lighting agree. K toggles.
    bool  skybox_enabled    = true;
    // Bloom — additive HDR glow on bright pixels. Off by default (preserves
    // existing baselines); B ramps up, V ramps down.
    float bloom_intensity   = 0.0f;   // 0=off; 0.3-0.6 looks good at night
    float bloom_threshold   = 1.0f;
    float bloom_radius      = 1.0f;
    // FXAA — runs inside the compose shader after tonemap. Off by default.
    bool  fxaa_enabled      = false;
    // Render modes — T cycles. Default 0 = RAW (no lighting), so first launch
    // shows the asset's textures untouched and we can verify the sampling
    // pipeline before stacking effects on top.
    //   0 RAW      : raw albedo, no shading (default)
    //   1 PBR      : full Cook-Torrance + IBL + shadow + tonemap
    //   2 NORMAL   : world-space normal visualised as RGB
    //   3 UV       : fract(UV) checker pattern — verify UV layout
    //   4 MIP      : LOD level the sampler picks (red→green→blue = coarse)
    int   render_mode       = 0;
    float lod_bias          = 0.0f;   // [-6..+6] override texture LOD
    // Sun direction in world space (unit vector). Rotated by arrow keys.
    glm::vec3 sun_dir       = glm::normalize(glm::vec3(0.4f, 0.6f, 0.8f));
  };
  RenderParams params_;

  // GPU resources for every glTF material (slot 0 is fallback).
  std::vector<PbrMaterial>   gpu_materials_;

  // Per-draw geometry slices into the shared VBO/IBO.
  std::vector<PrimitiveDraw> primitives_;

  // Scene
  LocalCamera     camera_;
  PrecisionOrigin origin_;
  std::vector<LocalEntity> entities_;
  std::vector<glm::vec4>   entityColors_;

  // Optional glTF/glb model — when set, replaces the hardcoded cube scene.
  std::string  model_path_;
  ModelData    model_data_;
  bool         model_loaded_ = false;
  glm::vec3    model_bbox_min_{0.0f}, model_bbox_max_{0.0f};

  // Input
  bool dragLeft_  = false;
  bool dragRight_ = false;
  glm::vec2 lastMouse_{0.0f, 0.0f};
};

// ----------------------------------------------------------------------------
// Init
// ----------------------------------------------------------------------------

bool LocalDemoApp::initialize() {
  if (!initWindow())              { std::cerr << "initWindow failed\n";        return false; }
  if (!initVulkan())              { std::cerr << "initVulkan failed\n";        return false; }
  if (!scene_renderer_.initialize(device_)) {
    std::cerr << "scene_renderer init failed\n"; return false;
  }
  if (!createBuffers())           { std::cerr << "createBuffers failed\n";     return false; }
  buildScene();
  return true;
}

bool LocalDemoApp::initWindow() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
    return false;
  }
  window_ = SDL_CreateWindow("XNBY Engine - Local Demo (free-fly + orbit, no globe)",
                             SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED,
                             windowW_,
                             windowH_,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
  return window_ != nullptr;
}

bool LocalDemoApp::initVulkan() {
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


bool LocalDemoApp::createMaterialsFromModel(const ModelData& model) {
  gpu_materials_.clear();
  gpu_materials_.reserve(model.materials.size() + 1);

  auto& factory = scene_renderer_.getMaterialFactory();

  // Slot 0 = fallback so primitives with material_id == -1 fall through here.
  auto fb = factory.createFallback();
  if (!fb) return false;
  gpu_materials_.push_back(std::move(*fb));

  auto pickTex = [&](int idx) -> const TextureData* {
    if (idx < 0 || idx >= static_cast<int>(model.textures.size())) return nullptr;
    const auto& t = model.textures[idx];
    if (t.pixels.empty() || t.width <= 0 || t.height <= 0) return nullptr;
    return &t;
  };

  size_t with_base = 0, with_normal = 0, with_mr = 0;
  for (const auto& mat : model.materials) {
    const auto* base   = pickTex(mat.base_color_texture);
    const auto* normal = pickTex(mat.normal_texture);
    const auto* mr     = pickTex(mat.metallic_roughness_texture);
    if (base)   ++with_base;
    if (normal) ++with_normal;
    if (mr)     ++with_mr;

    auto pm = factory.create(
        base   ? base->pixels.data()   : nullptr, base   ? base->width   : 0, base   ? base->height   : 0,
        normal ? normal->pixels.data() : nullptr, normal ? normal->width : 0, normal ? normal->height : 0,
        mr     ? mr->pixels.data()     : nullptr, mr     ? mr->width     : 0, mr     ? mr->height     : 0,
        mat.base_color_factor, mat.metallic_factor, mat.roughness_factor);
    if (!pm) {
      std::cerr << "[material] gpu upload failed for '" << mat.name << "', using fallback\n";
      pm = gpu_materials_[0];
    }
    gpu_materials_.push_back(std::move(*pm));
  }

  std::cout << "[material] " << (gpu_materials_.size() - 1)
            << " glTF materials uploaded ("
            << with_base   << " base, "
            << with_normal << " normal, "
            << with_mr     << " metallic-roughness)\n";
  return true;
}

bool LocalDemoApp::buildBuffersFromModel(const ModelData& model) {
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

    // Record this primitive's index range BEFORE appending so the draw call
    // knows where to start. `material_id + 1` because slot 0 is the fallback.
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

  if (verts.empty() || idx.empty()) {
    std::cerr << "model has no geometry — falling back to cube scene\n";
    return false;
  }
  indexCount_ = static_cast<uint32_t>(idx.size());

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
            << model.meshes.size() << " primitives\n";
  return true;
}

bool LocalDemoApp::createBuffers() {
  // Try to load the model first; if it succeeds, use its geometry. Otherwise
  // fall back to the hardcoded cube ring.
  if (!model_path_.empty()) {
    GltfLoader loader;
    auto md = loader.load(model_path_);
    if (!md.has_value()) {
      std::cerr << "[model] failed to load " << model_path_ << ": " << loader.lastError() << "\n";
    } else {
      model_data_   = std::move(*md);
      if (!createMaterialsFromModel(model_data_)) {
        std::cerr << "[model] material upload failed; falling back to cube\n";
      } else {
        model_loaded_ = buildBuffersFromModel(model_data_);
        if (model_loaded_) return true;
        gpu_materials_.clear();  // reset for cube path
      }
    }
  }

  // Cube fallback: single fallback material, single primitive.
  {
    auto fb = scene_renderer_.getMaterialFactory().createFallback();
    if (!fb) return false;
    gpu_materials_.clear();
    gpu_materials_.push_back(std::move(*fb));
  }
  auto verts = makeCubeVertices();
  auto idx   = makeCubeIndices();
  indexCount_ = static_cast<uint32_t>(idx.size());
  primitives_.clear();
  primitives_.push_back({0u, static_cast<uint32_t>(idx.size()), -1});

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
  return true;
}

void LocalDemoApp::buildScene() {
  origin_.setOrigin({0.0, 0.0, 0.0});

  camera_.setScreenSize(static_cast<float>(windowW_), static_cast<float>(windowH_));
  camera_.setMode(LocalCamera::Mode::Orbit);
  camera_.lookAtTarget({0.0, 0.0, 0.0}, 25.0,
                        /*yaw*/ 0.4, /*pitch*/ -0.4);
  camera_.setFovYDegrees(60.0f);
  camera_.setNearFar(0.1f, 1000.0f);

  // Model mode: one LocalEntity at the origin holding the whole concatenated
  // model geometry. The camera is auto-fitted to the model's bounding box so
  // the user actually sees something regardless of source scale (a CAD model
  // might be hundreds of meters wide; a game asset, just a few).
  if (model_loaded_) {
    LocalEntity e;
    e.setScale(1.0f);
    entities_.push_back(std::move(e));
    entityColors_.emplace_back(0.85f, 0.85f, 0.85f, 1.0f);

    glm::vec3 center = 0.5f * (model_bbox_min_ + model_bbox_max_);
    glm::vec3 extent = model_bbox_max_ - model_bbox_min_;
    float     diag   = glm::length(extent);
    float     dist   = std::max(diag * 0.9f, 1.0f);

    // Aim camera at the model centre, framed by the diagonal. Near/far
    // adapt to scale so a 3 km city and a 30 cm CAD part both render.
    camera_.lookAtTarget(glm::dvec3(center), dist, /*yaw*/ 0.6, /*pitch*/ -0.45);
    camera_.setNearFar(std::max(diag * 0.001f, 0.01f),
                        std::max(diag * 10.0f, 1000.0f));

    // SSAO radius lives in view-space metres — autoscale so the same default
    // looks correct on a 30 cm CAD part and a 3 km city.
    params_.ssao_radius_m = std::max(diag * 0.002f, 0.05f);

    std::cout << "[scene] loaded " << entities_.size()
              << " root entities from model\n";
    std::cout << "[scene] bbox center (" << center.x << ", " << center.y << ", "
              << center.z << "), extent (" << extent.x << ", " << extent.y
              << ", " << extent.z << ")\n";
    std::cout << "[scene] camera distance=" << dist
              << ", near=" << camera_.getNear()
              << ", far=" << camera_.getFar() << "\n";

    std::cout << "\n=== Local Demo Controls (model mode) ===\n"
              << "  Left drag    orbit\n"
              << "  Right drag   yaw + pitch\n"
              << "  Scroll       zoom / forward\n"
              << "  F            toggle FreeFly / Orbit\n"
              << "  WASD QE      translate (FreeFly)\n"
              << "  R            reset view\n"
              << "  P            print current render params\n"
              << "  1 / 2        sun intensity     -/+\n"
              << "  3 / 4        sky brightness    -/+\n"
              << "  5 / 6        IBL diffuse       -/+\n"
              << "  7 / 8        exposure          -/+\n"
              << "  9 / 0        shadow strength   -/+\n"
              << "  N / M        skylight bleed    -/+ (fixes dark ground)\n"
              << "  F1 / F2      SSAO strength     -/+ (0=off, 1=full)\n"
              << "  V  / B       Bloom intensity   -/+ (0=off)\n"
              << "  X            toggle FXAA\n"
              << "  K            toggle procedural skybox\n"
              << "  T            cycle render mode (RAW → PBR → normal-viz → uv → mip)\n"
              << "  [ / ]        texture LOD bias  -/+ (force finer / coarser mip)\n"
              << "  ← → ↑ ↓      rotate sun direction (azimuth / elevation)\n"
              << "  Esc          quit\n"
              << "========================================\n\n";
    return;
  }

  // 8 cubes in a ring, each a different colour.
  constexpr int N = 8;
  for (int i = 0; i < N; ++i) {
    LocalEntity e;
    double angle = (2.0 * glm::pi<double>()) * i / N;
    double r     = 8.0;
    e.setPosition({r * std::cos(angle), r * std::sin(angle), 0.0});
    e.setScale(2.0f);
    entities_.push_back(std::move(e));

    float h = static_cast<float>(i) / N;
    entityColors_.emplace_back(
        0.5f + 0.5f * std::cos(6.28f * h),
        0.5f + 0.5f * std::cos(6.28f * (h + 0.33f)),
        0.5f + 0.5f * std::cos(6.28f * (h + 0.67f)),
        1.0f);
  }

  // One bigger cube at the centre to give the orbit a clear focal point.
  LocalEntity center;
  center.setPosition({0.0, 0.0, 0.0});
  center.setScale(3.0f);
  entities_.push_back(std::move(center));
  entityColors_.emplace_back(0.85f, 0.85f, 0.85f, 1.0f);

  std::cout << "\n=== Local Demo Controls ===\n"
            << "  Left drag    orbit / yaw+pitch\n"
            << "  Right drag   yaw + pitch\n"
            << "  Scroll       zoom / forward\n"
            << "  F            toggle FreeFly / Orbit\n"
            << "  WASD QE      translate (FreeFly)\n"
            << "  R            reset view\n"
            << "  Esc          quit\n"
            << "===========================\n\n";
}

// ----------------------------------------------------------------------------
// Loop
// ----------------------------------------------------------------------------

void LocalDemoApp::handleEvent(const SDL_Event& e) {
  switch (e.type) {
    case SDL_MOUSEBUTTONDOWN:
      if (e.button.button == SDL_BUTTON_LEFT) {
        dragLeft_  = true;
        lastMouse_ = {static_cast<float>(e.button.x), static_cast<float>(e.button.y)};
        camera_.onMouseDown(lastMouse_.x, lastMouse_.y);
      } else if (e.button.button == SDL_BUTTON_RIGHT) {
        dragRight_ = true;
        lastMouse_ = {static_cast<float>(e.button.x), static_cast<float>(e.button.y)};
      }
      break;

    case SDL_MOUSEBUTTONUP:
      if (e.button.button == SDL_BUTTON_LEFT)  { dragLeft_ = false; camera_.onMouseUp(); }
      if (e.button.button == SDL_BUTTON_RIGHT) { dragRight_ = false; }
      break;

    case SDL_MOUSEMOTION:
      if (dragLeft_) {
        camera_.onMouseMove(static_cast<float>(e.motion.x), static_cast<float>(e.motion.y));
      } else if (dragRight_) {
        float dx = static_cast<float>(e.motion.x) - lastMouse_.x;
        float dy = static_cast<float>(e.motion.y) - lastMouse_.y;
        camera_.onRightDrag(dx, dy);
        lastMouse_ = {static_cast<float>(e.motion.x), static_cast<float>(e.motion.y)};
      }
      break;

    case SDL_MOUSEWHEEL:
      camera_.onMouseScroll(static_cast<float>(e.wheel.y));
      break;

    case SDL_KEYDOWN: {
      const float step = 0.5f;
      switch (e.key.keysym.sym) {
        case SDLK_f:
          camera_.setMode(camera_.getMode() == LocalCamera::Mode::Orbit
                              ? LocalCamera::Mode::FreeFly
                              : LocalCamera::Mode::Orbit);
          break;
        case SDLK_r:
          camera_.lookAtTarget({0.0, 0.0, 0.0}, 25.0, 0.4, -0.4);
          break;
        case SDLK_w: camera_.moveForward(+step); break;
        case SDLK_s: camera_.moveForward(-step); break;
        case SDLK_d: camera_.moveRight(+step); break;
        case SDLK_a: camera_.moveRight(-step); break;
        case SDLK_e: camera_.moveUp(+step); break;
        case SDLK_q: camera_.moveUp(-step); break;

        // --- Runtime rendering params -----------------------------------
        // Sun intensity
        case SDLK_1: params_.sun_intensity = std::max(0.0f, params_.sun_intensity - 0.5f);
                     printParams(); break;
        case SDLK_2: params_.sun_intensity += 0.5f; printParams(); break;
        // Sky brightness scale
        case SDLK_3: params_.sky_scale = std::max(0.05f, params_.sky_scale - 0.1f);
                     printParams(); break;
        case SDLK_4: params_.sky_scale += 0.1f; printParams(); break;
        // IBL diffuse multiplier
        case SDLK_5: params_.ibl_diffuse_scale = std::max(0.0f, params_.ibl_diffuse_scale - 0.1f);
                     printParams(); break;
        case SDLK_6: params_.ibl_diffuse_scale += 0.1f; printParams(); break;
        // Exposure
        case SDLK_7: params_.exposure = std::max(0.1f, params_.exposure - 0.1f);
                     printParams(); break;
        case SDLK_8: params_.exposure += 0.1f; printParams(); break;
        // Shadow strength
        case SDLK_9: params_.shadow_strength = std::max(0.0f, params_.shadow_strength - 0.1f);
                     printParams(); break;
        case SDLK_0: params_.shadow_strength = std::min(1.0f, params_.shadow_strength + 0.1f);
                     printParams(); break;
        // Skylight bleed (albedo-independent ambient — fixes dark ground)
        case SDLK_n: params_.skylight_bleed = std::max(0.0f, params_.skylight_bleed - 0.02f);
                     printParams(); break;
        case SDLK_m: params_.skylight_bleed += 0.02f; printParams(); break;
        // SSAO strength — F1 lowers, F2 raises (0=off, 1=full).
        case SDLK_F1: params_.ssao_strength = std::max(0.0f, params_.ssao_strength - 0.1f);
                      std::cout << "[ssao] strength=" << params_.ssao_strength << "\n"; break;
        case SDLK_F2: params_.ssao_strength = std::min(1.0f, params_.ssao_strength + 0.1f);
                      std::cout << "[ssao] strength=" << params_.ssao_strength << "\n"; break;
        // Bloom intensity — V lowers, B raises (0=off, ~1=very glowy)
        case SDLK_v: params_.bloom_intensity = std::max(0.0f, params_.bloom_intensity - 0.1f);
                     std::cout << "[bloom] intensity=" << params_.bloom_intensity << "\n"; break;
        case SDLK_b: params_.bloom_intensity = std::min(2.0f, params_.bloom_intensity + 0.1f);
                     std::cout << "[bloom] intensity=" << params_.bloom_intensity << "\n"; break;
        // FXAA toggle
        case SDLK_x: params_.fxaa_enabled = !params_.fxaa_enabled;
                     std::cout << "[fxaa] " << (params_.fxaa_enabled ? "on" : "off") << "\n"; break;
        // Skybox toggle
        case SDLK_k: params_.skybox_enabled = !params_.skybox_enabled;
                     std::cout << "[skybox] " << (params_.skybox_enabled ? "on" : "off") << "\n"; break;
        // Render-mode cycle — start in RAW (texture only), step through PBR
        // and debug visualisations.
        case SDLK_t:
          params_.render_mode = (params_.render_mode + 1) % 5;
          {
            static const char* names[] = {
              "RAW (texture only, no shading)",
              "PBR (Cook-Torrance + IBL + shadow + tonemap)",
              "DEBUG world normal",
              "DEBUG UV checker",
              "DEBUG mip level"};
            std::cout << "[mode] " << params_.render_mode << " — "
                      << names[params_.render_mode] << "\n";
          }
          break;
        // LOD bias — push toward finer (negative) mips to force more detail.
        case SDLK_LEFTBRACKET:
          params_.lod_bias = std::max(-6.0f, params_.lod_bias - 0.5f);
          std::cout << "[debug] lod_bias=" << params_.lod_bias << "\n"; break;
        case SDLK_RIGHTBRACKET:
          params_.lod_bias = std::min( 6.0f, params_.lod_bias + 0.5f);
          std::cout << "[debug] lod_bias=" << params_.lod_bias << "\n"; break;
        // Sun direction — Z (azimuth) and X (elevation)
        case SDLK_LEFT: {
          float a = std::atan2(params_.sun_dir.y, params_.sun_dir.x) - 0.1f;
          float z = params_.sun_dir.z;
          float r = std::sqrt(1.0f - z * z);
          params_.sun_dir = {r * std::cos(a), r * std::sin(a), z};
          printParams(); break;
        }
        case SDLK_RIGHT: {
          float a = std::atan2(params_.sun_dir.y, params_.sun_dir.x) + 0.1f;
          float z = params_.sun_dir.z;
          float r = std::sqrt(1.0f - z * z);
          params_.sun_dir = {r * std::cos(a), r * std::sin(a), z};
          printParams(); break;
        }
        case SDLK_UP: {
          params_.sun_dir.z = std::min(0.95f, params_.sun_dir.z + 0.05f);
          params_.sun_dir = glm::normalize(params_.sun_dir);
          printParams(); break;
        }
        case SDLK_DOWN: {
          params_.sun_dir.z = std::max(0.05f, params_.sun_dir.z - 0.05f);
          params_.sun_dir = glm::normalize(params_.sun_dir);
          printParams(); break;
        }
        case SDLK_p: {
          printParams();
          // Profiler HUD: dump rolling-window average + per-pass split. Cheap
          // CPU-side timing — no GPU readback, so it's accurate to the
          // `renderFrame()` envelope but doesn't see inside the submit/present.
          std::cout << scene_renderer_.getProfiler().getSummaryText() << "\n";
          break;
        }
        default: break;
      }
      break;
    }
    default: break;
  }
}

void LocalDemoApp::printParams() const {
  std::cout << "[params] sun=" << params_.sun_intensity
            << " sky="    << params_.sky_scale
            << " ibl="    << params_.ibl_diffuse_scale
            << " expo="   << params_.exposure
            << " shadow=" << params_.shadow_strength
            << " bleed="  << params_.skylight_bleed
            << " sun_dir=(" << params_.sun_dir.x
            << ", " << params_.sun_dir.y
            << ", " << params_.sun_dir.z << ")\n";
}

void LocalDemoApp::update(float dt) {
  camera_.update(dt);
}

void LocalDemoApp::render() {
  // Flatten scene state into the orchestrator's per-frame view.
  ForwardSceneRenderer::FrameParams fp;

  // Camera (Vulkan-flipped projection matches earth_demo's convention).
  auto ext = device_->getSwapchainExtent();
  const float aspect = static_cast<float>(ext.width) /
                       static_cast<float>(ext.height);
  fp.proj = camera_.getProjectionMatrix(aspect);
  fp.proj[1][1] *= -1.0f;
  fp.view = camera_.getRTCViewMatrix(origin_);
  glm::dvec3 cam_ws = camera_.getEyeWorld();
  fp.eye_world = glm::vec3(cam_ws);

  // Sun + scene bbox for shadow ortho.
  fp.sun_dir_world = params_.sun_dir;
  fp.sun_intensity = params_.sun_intensity;
  fp.bbox_center = 0.5f * (model_loaded_
                              ? (model_bbox_min_ + model_bbox_max_)
                              : glm::vec3(0.0f));
  fp.bbox_extent = model_loaded_
                      ? glm::length(model_bbox_max_ - model_bbox_min_) * 0.5f
                      : 20.0f;

  // Runtime tunables (driven by handleEvent's keyboard).
  fp.sky_scale         = params_.sky_scale;
  fp.ibl_diffuse_scale = params_.ibl_diffuse_scale;
  fp.exposure          = params_.exposure;
  fp.shadow_strength   = params_.shadow_strength;
  fp.skylight_bleed    = params_.skylight_bleed;
  fp.render_mode       = params_.render_mode;
  fp.lod_bias          = params_.lod_bias;
  fp.ssao_strength     = params_.ssao_strength;
  fp.ssao_radius_m     = params_.ssao_radius_m;
  fp.ssao_intensity    = params_.ssao_intensity;
  fp.ssao_bias         = params_.ssao_bias;
  fp.skybox_enabled    = params_.skybox_enabled;
  fp.bloom_intensity   = params_.bloom_intensity;
  fp.bloom_threshold   = params_.bloom_threshold;
  fp.bloom_radius      = params_.bloom_radius;
  fp.fxaa_enabled      = params_.fxaa_enabled;

  // Clear colour: HDR-bright sky for PBR (ACES has something to compress),
  // neutral light-grey for RAW / debug (background looks natural).
  if (params_.render_mode == 1) {
    const float s = params_.sky_scale;
    fp.clear_color_hdr = glm::vec3(2.4f * s, 2.4f * s, 2.6f * s);
  } else {
    fp.clear_color_hdr = glm::vec3(0.78f, 0.78f, 0.80f);
  }

  // Per-entity model + tint. Also compute a world-space AABB by transforming
  // the model's local bbox by the model matrix — the renderer uses it for
  // frustum culling. For the cube ring path bbox is constant; for the
  // loaded glTF model bbox came from buildBuffersFromModel.
  std::vector<ForwardSceneRenderer::EntityDraw> entity_draws;
  entity_draws.reserve(entities_.size());
  const glm::vec3 local_min = model_loaded_ ? model_bbox_min_ : glm::vec3(-0.5f);
  const glm::vec3 local_max = model_loaded_ ? model_bbox_max_ : glm::vec3( 0.5f);
  for (size_t i = 0; i < entities_.size(); ++i) {
    ForwardSceneRenderer::EntityDraw ed;
    ed.model      = entities_[i].getRTCModelMatrix(origin_);
    ed.color_tint = entityColors_[i];
    // Transform 8 corners by model matrix, re-min/max to get world AABB.
    const glm::vec3 c[8] = {
        {local_min.x, local_min.y, local_min.z},
        {local_max.x, local_min.y, local_min.z},
        {local_min.x, local_max.y, local_min.z},
        {local_max.x, local_max.y, local_min.z},
        {local_min.x, local_min.y, local_max.z},
        {local_max.x, local_min.y, local_max.z},
        {local_min.x, local_max.y, local_max.z},
        {local_max.x, local_max.y, local_max.z}};
    glm::vec3 wmin( std::numeric_limits<float>::max());
    glm::vec3 wmax(-std::numeric_limits<float>::max());
    for (int k = 0; k < 8; ++k) {
      glm::vec3 w = glm::vec3(ed.model * glm::vec4(c[k], 1.0f));
      wmin = glm::min(wmin, w);
      wmax = glm::max(wmax, w);
    }
    ed.aabb_world.min_point = wmin;
    ed.aabb_world.max_point = wmax;
    ed.has_aabb = true;
    entity_draws.push_back(ed);
  }

  scene_renderer_.renderFrame(fp, vbo_, ibo_, gpu_materials_,
                                entity_draws, primitives_);

  // Once per second, print culling stats so we can sanity-check the wiring.
  static int frame_counter = 0;
  if ((++frame_counter % 60) == 0) {
    const auto& cs = scene_renderer_.getLastCullingStats();
    std::cout << "[cull] entities " << cs.drawn_entities
              << "/" << cs.total_entities
              << " (frustum culled " << cs.culled_frustum << ")\n";
  }
}

void LocalDemoApp::run() {
  auto last = std::chrono::high_resolution_clock::now();
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
    if (!running) break;

    auto now = std::chrono::high_resolution_clock::now();
    float dt = std::chrono::duration<float>(now - last).count();
    last = now;

    update(dt);
    render();
  }
  if (device_) device_->waitIdle();
}

void LocalDemoApp::shutdown() {
  cleanup();
}

void LocalDemoApp::cleanup() {
  if (device_) {
    device_->waitIdle();
    gpu_materials_.clear();
    ibo_.reset();
    vbo_.reset();
    // scene_renderer_ destructs naturally with the app — all its RHI handles
    // are ref-counted.
    delete device_;
    device_ = nullptr;
  }
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  SDL_Quit();
}

// ----------------------------------------------------------------------------
// Entry
// ----------------------------------------------------------------------------

int main(int argc, char* argv[]) {
#ifdef __APPLE__
  // Auto-configure MoltenVK on macOS if not already set — same pattern as
  // earth_demo. Without this, Vulkan loader can't find the ICD on systems
  // that install MoltenVK via homebrew.
  if (!std::getenv("VK_ICD_FILENAMES")) {
    const char* icd = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json";
    if (std::ifstream(icd).good()) {
      setenv("VK_ICD_FILENAMES", icd, 0);
      setenv("VK_DRIVER_FILES",  icd, 0);
    }
  }
#endif

  std::cout << "XNBY Engine — Local Demo\n";
  std::cout << "==========================\n";

  LocalDemoApp app;

  // CLI: --model PATH (loads a glTF or glb model in place of the cube ring).
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--help" || a == "-h") {
      std::cout
        << "Usage: local_demo [--model PATH]\n"
        << "  --model PATH    load a .gltf or .glb model instead of the default cube ring\n";
      return 0;
    } else if (a.rfind("--model=", 0) == 0) {
      app.setModelPath(a.substr(8));
    } else if (a == "--model" && i + 1 < argc) {
      app.setModelPath(argv[++i]);
    } else {
      std::cerr << "Unknown argument: " << a << " (try --help)\n";
      return 1;
    }
  }

  if (!app.initialize()) {
    std::cerr << "Initialization failed.\n";
    app.shutdown();
    return 1;
  }
  app.run();
  app.shutdown();
  return 0;
}
