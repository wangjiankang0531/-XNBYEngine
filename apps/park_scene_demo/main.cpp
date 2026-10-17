// park_scene_demo — showcase park scene for the P1 vegetation system.
//
// Composes engine capabilities only (no engine logic here): ground plane with
// a tiling PBR lawn material, VegetationLayer instancing (trees / shrubs /
// grass clumps with wind + frustum culling), ForwardSceneRenderer (CSM shadow
// → PBR forward → SSAO → bloom → FXAA compose).
//
// Headless verification:
//   park_scene_demo --frames=180 --screenshot=/tmp/park.png
//   park_scene_demo --cam-az=2.4 --cam-el=0.25 --cam-dist=80 --stats
//
// Assets come from assets/park/ (bash tools/prep_park_assets.sh).

#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>

#include <hongtu/asset/GltfLoader.h>
#include <hongtu/renderer/pbr/forward_scene_renderer.hpp>
#include <hongtu/renderer/pbr/mesh_vertex.hpp>
#include <hongtu/renderer/vegetation/vegetation_layer.hpp>
#include <hongtu/rhi/RHIDevice.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <spdlog/spdlog.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace HongTu::RHI;
using hongtu::asset::GltfLoader;
using hongtu::asset::ModelData;
using hongtu::renderer::ForwardSceneRenderer;
using hongtu::renderer::MeshVertex;
using hongtu::renderer::PbrMaterial;
using hongtu::renderer::VegetationInstance;
using hongtu::renderer::VegetationLayer;
using hongtu::renderer::VegetationPipeline;

namespace {

struct Cli {
  int         frames = 0;       // 0 = interactive
  std::string screenshot;
  bool        stats   = false;
  bool        no_veg  = false;  // debug: ground+sky only
  int         render_mode = 1;  // 0 RAW, 1 PBR (debug)
  bool        fxaa = true;
  float       bloom = 0.0f;
  glm::vec3   dbg_clear{0.0f};  // debug: known clear color through the chain
  bool        dbg_clear_set = false;
  float       cam_az  = 2.35f;  // azimuth (rad)
  float       cam_el  = 0.22f;  // elevation (rad)
  float       cam_dist = 90.0f;
  glm::vec3   look_at{0.0f, 0.0f, 2.0f};
  float       sun_az = 2.0f, sun_el = 0.7f;
  float       wind   = 0.5f;
  float       sun_int = 4.0f, ibl = 1.4f, sky = 1.0f, exposure = 1.0f, shadow = 0.6f, ssao = 0.6f;
  int         width = 1600, height = 900;
};

// Deterministic LCG so every run plants the same park.
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed) {}
  float next() {  // [0,1)
    s = s * 1664525u + 1013904223u;
    return static_cast<float>(s >> 8) / 16777216.0f;
  }
  float range(float a, float b) { return a + (b - a) * next(); }
};

// ----------------------------------------------------------------------------
// Ground plane + path strip
// ----------------------------------------------------------------------------
// 步道:沿 X 轴的条带(林荫道中线),微微抬升避免 z-fight。UV 沿长度铺。
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
    // 与 makeGround 同绕序约定(叉积 -Z 为正面)— 反了会被
    // gl_FrontFacing 翻转法线朝下,IBL 采到地面色,整条变黑。
    idx.insert(idx.end(), {a, b, c, b, d, c});
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

// Load a jpg/png as RGBA8.
std::vector<uint8_t> loadRgba(const char* path, int& w, int& h) {
  int n = 0;
  unsigned char* p = stbi_load(path, &w, &h, &n, 4);
  if (!p) return {};
  std::vector<uint8_t> out(p, p + static_cast<size_t>(w) * h * 4);
  stbi_image_free(p);
  return out;
}

}  // namespace

int main(int argc, char** argv) {
#ifdef __APPLE__
  // Auto-configure MoltenVK if not already set (same as the other demos).
  if (!std::getenv("VK_ICD_FILENAMES")) {
    const char* icd = "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json";
    if (std::ifstream(icd).good()) {
      setenv("VK_ICD_FILENAMES", icd, 0);
      setenv("VK_DRIVER_FILES", icd, 0);
    }
  }
#endif

  Cli cli;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto starts = [&](const char* p) { return a.rfind(p, 0) == 0; };
    if (starts("--frames="))          cli.frames     = std::atoi(a.c_str() + 9);
    else if (starts("--screenshot=")) cli.screenshot = a.substr(13);
    else if (a == "--stats")          cli.stats      = true;
    else if (a == "--no-veg")         cli.no_veg     = true;
    else if (starts("--bloom="))      cli.bloom = std::atof(a.c_str() + 8);
    else if (starts("--fxaa="))       cli.fxaa = std::atoi(a.c_str() + 7) != 0;
    else if (starts("--mode="))       cli.render_mode = std::atoi(a.c_str() + 7);
    else if (starts("--clear=")) {    // debug: solid clear color "r,g,b", skybox off
      std::sscanf(a.c_str() + 8, "%f,%f,%f", &cli.dbg_clear.x, &cli.dbg_clear.y,
                  &cli.dbg_clear.z);
      cli.dbg_clear_set = true;
    }
    else if (starts("--cam-az="))     cli.cam_az     = std::atof(a.c_str() + 9);
    else if (starts("--cam-el="))     cli.cam_el     = std::atof(a.c_str() + 9);
    else if (starts("--cam-dist="))   cli.cam_dist   = std::atof(a.c_str() + 11);
    else if (starts("--sun-az="))     cli.sun_az     = std::atof(a.c_str() + 9);
    else if (starts("--sun-el="))     cli.sun_el     = std::atof(a.c_str() + 9);
    else if (starts("--wind="))       cli.wind       = std::atof(a.c_str() + 7);
    else if (starts("--sun-int="))    cli.sun_int    = std::atof(a.c_str() + 10);
    else if (starts("--ibl="))        cli.ibl        = std::atof(a.c_str() + 6);
    else if (starts("--sky="))        cli.sky        = std::atof(a.c_str() + 6);
    else if (starts("--exposure="))   cli.exposure   = std::atof(a.c_str() + 11);
    else if (starts("--shadow="))     cli.shadow     = std::atof(a.c_str() + 9);
    else if (starts("--ssao="))       cli.ssao       = std::atof(a.c_str() + 7);
    else if (starts("--width="))      cli.width      = std::atoi(a.c_str() + 8);
    else if (starts("--height="))     cli.height     = std::atoi(a.c_str() + 9);
    else spdlog::warn("unknown arg {}", a);
  }

  // ---- window + device ------------------------------------------------------
  spdlog::info("park_scene_demo starting ({}x{})", cli.width, cli.height);
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    spdlog::error("SDL_Init failed: {}", SDL_GetError());
    return 1;
  }
  SDL_Window* window = SDL_CreateWindow("XNBY — Park Scene",
                                        SDL_WINDOWPOS_CENTERED,
                                        SDL_WINDOWPOS_CENTERED,
                                        cli.width,
                                        cli.height,
                                        SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
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
  sc.vsync      = true;
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

  // ---- scene VBO/IBO: ground -----------------------------------------------
  std::vector<MeshVertex> verts;
  std::vector<uint32_t>   idx;
  makeGround(verts, idx);
  const uint32_t ground_index_count = static_cast<uint32_t>(idx.size());
  makePath(verts, idx);
  const uint32_t path_index_count = static_cast<uint32_t>(idx.size()) - ground_index_count;

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

  // ---- materials: slot 0 fallback, slot 1 lawn ------------------------------
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
    // ARM packs AO/rough/metal in R/G/B — glTF MR wants G=rough B=metal, so
    // the channel layout already matches.
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
  }

  // ---- vegetation -----------------------------------------------------------
  VegetationLayer veg;
  if (!veg.initialize(device, &mf, renderer.getShadowSetLayout(),
                      renderer.getHdrRenderPass())) {
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
    if (!res) {
      spdlog::error("load failed: {}", path);
      return -1;
    }
    return veg.addArchetype(*res);
  };
  const int arch_jacaranda = addArch("assets/park/trees/jacaranda_tree.glb");
  const int arch_island    = addArch("assets/park/trees/island_tree_02.glb");
  const int arch_shrub     = addArch("assets/park/shrubs/shrub_02.glb");
  const int arch_grass     = addArch("assets/park/grass/grass_medium_01.glb");
  if (arch_island < 0) return 1;
  // 远景档:两种树都有 LOD;草/灌木限绘制距离。
  if (auto lod = loader.load("assets/park/trees/island_tree_02_lod1.glb")) {
    veg.setArchetypeLod(arch_island, *lod, 60.0f);
  }
  if (auto lod = loader.load("assets/park/trees/jacaranda_tree_lod1.glb")) {
    veg.setArchetypeLod(arch_jacaranda, *lod, 55.0f);
  }
  if (arch_grass >= 0) { veg.setMaxDrawDistance(arch_grass, 90.0f); veg.setCastsShadow(arch_grass, false); }
  if (arch_shrub >= 0) veg.setMaxDrawDistance(arch_shrub, 220.0f);
  const int arch_lamp  = addArch("assets/park/props/street_lamp_01.glb");
  const int arch_bench = addArch("assets/park/props/bench.glb");
  if (arch_lamp  >= 0) veg.setSwayAmplitude(arch_lamp, 0.0f);
  if (arch_bench >= 0) veg.setSwayAmplitude(arch_bench, 0.0f);

  // ---- plant the park (deterministic) --------------------------------------
  // Layout: a jacaranda alley along the X axis, island-tree clusters on both
  // sides, shrubs sprinkled between, grass clumps everywhere on the lawn.
  {
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
    // 大规模丛植 — jacaranda 是主力景观树(LOD 在 55m 接管)。
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
    if (arch_jacaranda >= 0) veg.setInstances(arch_jacaranda, jac);
    spdlog::info("planted {} jacaranda", jac.size());

    std::vector<VegetationInstance> isl;
    for (int c = 0; c < 14; ++c) {  // 次层绿树
      float cx = rng.range(-170.0f, 170.0f);
      float cy = rng.range(-170.0f, 170.0f);
      if (std::abs(cy) < 25.0f) cy += (cy < 0 ? -40.0f : 40.0f);  // keep alley clear
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
    veg.setInstances(arch_island, isl);
    spdlog::info("planted {} island trees", isl.size());

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
      veg.setInstances(arch_lamp, lamps);
    }
    if (arch_bench >= 0) {
      std::vector<VegetationInstance> benches;
      for (float x = -72.0f; x <= 84.0f; x += 36.0f) {
        VegetationInstance v;
        v.pos = {x + 9.0f, -4.4f, 0.0f};
        v.yaw = 0.0f;
        benches.push_back(v);
      }
      veg.setInstances(arch_bench, benches);
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
    if (arch_shrub >= 0) veg.setInstances(arch_shrub, shrubs);

    std::vector<VegetationInstance> grass;
    for (int i = 0; i < 2200; ++i) {
      VegetationInstance v;
      v.pos   = {rng.range(-120.0f, 120.0f), rng.range(-120.0f, 120.0f), 0.0f};
      v.yaw   = rng.range(0.0f, 6.28f);
      v.scale = rng.range(0.9f, 1.8f);
      v.tint  = rng.next();
      grass.push_back(v);
    }
    if (arch_grass >= 0) veg.setInstances(arch_grass, grass);
  }

  // ---- frame loop -----------------------------------------------------------
  const glm::vec3 sun_dir = glm::normalize(glm::vec3(
      std::cos(cli.sun_el) * std::cos(cli.sun_az),
      std::cos(cli.sun_el) * std::sin(cli.sun_az),
      std::sin(cli.sun_el)));

  ForwardSceneRenderer::EntityDraw ground_entity;
  ground_entity.model      = glm::mat4(1.0f);
  ground_entity.color_tint = glm::vec4(1.0f);
  std::vector<ForwardSceneRenderer::EntityDraw> entities = {ground_entity};
  if (std::getenv("PARK_SHADOW_WALL")) {
    // TEMP: vertical wall to verify the entity shadow chain end-to-end.
    ForwardSceneRenderer::EntityDraw wall;
    wall.model = glm::rotate(glm::translate(glm::mat4(1.0f), {30.0f, 0.0f, 0.0f}),
                             glm::half_pi<float>(), glm::vec3(1, 0, 0));
    wall.model = glm::scale(wall.model, glm::vec3(0.15f));
    wall.color_tint = glm::vec4(1, 0.4, 0.4, 1);
    entities.push_back(wall);
  }
  std::vector<ForwardSceneRenderer::PrimitiveDraw> prims = {
      {0u, ground_index_count, 0 /*lawn*/},
      {ground_index_count, path_index_count, 1 /*path*/}};

  bool  running = true;
  int   frame = 0;
  float t = 0.0f;
  uint64_t t_prev = SDL_GetPerformanceCounter();
  double fps_acc = 0.0;
  int    fps_n   = 0;

  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) running = false;
      if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE) running = false;
    }

    uint64_t t_now = SDL_GetPerformanceCounter();
    float dt = static_cast<float>(t_now - t_prev) / SDL_GetPerformanceFrequency();
    t_prev = t_now;
    t += dt;
    if (dt > 0.0f) { fps_acc += 1.0 / dt; ++fps_n; }

    // Camera orbit (fixed unless CLI overrides).
    glm::vec3 eye = cli.look_at +
        glm::vec3(std::cos(cli.cam_el) * std::cos(cli.cam_az),
                  std::cos(cli.cam_el) * std::sin(cli.cam_az),
                  std::sin(cli.cam_el)) * cli.cam_dist;
    glm::mat4 view = glm::lookAt(eye, cli.look_at, glm::vec3(0, 0, 1));
    glm::mat4 proj = glm::perspective(glm::radians(50.0f),
                                      static_cast<float>(cli.width) / cli.height,
                                      0.5f, 2000.0f);
    proj[1][1] *= -1.0f;  // Vulkan Y flip

    ForwardSceneRenderer::FrameParams fp{};
    fp.view              = view;
    fp.proj              = proj;
    fp.eye_world         = eye;
    fp.sun_dir_world     = sun_dir;
    // Baseline = local_demo's proven defaults (sun 4.0 / sky 1.0 / ibl 1.4 /
    // bleed 0 — bleed tints everything blue). Park-specific deltas applied
    // cautiously and verified by screenshot.
    fp.sun_intensity     = cli.sun_int;
    fp.bbox_center       = {0.0f, 0.0f, 8.0f};
    fp.bbox_extent       = 230.0f;
    fp.sky_scale         = cli.sky;
    fp.ibl_diffuse_scale = cli.ibl;
    fp.exposure          = cli.exposure;
    fp.shadow_strength   = cli.shadow;
    fp.skylight_bleed    = 0.0f;
    fp.render_mode       = cli.render_mode;
    fp.lod_bias          = 0.0f;
    fp.clear_color_hdr   = cli.dbg_clear_set ? cli.dbg_clear
                                             : glm::vec3{0.45f, 0.65f, 1.0f};
    fp.skybox_enabled    = !cli.dbg_clear_set;
    fp.skybox_mode       = 1;  // 昼天空(与 IBL 一致)
    fp.skybox_display_scale = 1.0f;
    fp.ssao_strength     = cli.ssao;
    fp.ssao_radius_m     = 1.2f;
    fp.bloom_intensity   = cli.bloom;
    fp.bloom_threshold   = 1.2f;
    fp.fxaa_enabled      = cli.fxaa;

    // Vegetation: cull + per-frame push constants, drawn via the HDR hook.
    veg.update(proj * view, eye);
    VegetationPipeline::PushConstants vpc{};
    vpc.view_proj     = proj * view;
    vpc.light_vp      = renderer.getLightViewProj();
    vpc.camera_pos_ws = glm::vec4(eye, 0.0f);
    vpc.light_dir_ws  = glm::vec4(sun_dir, fp.sun_intensity);
    vpc.env_params    = glm::vec4(fp.sky_scale, fp.ibl_diffuse_scale,
                                  fp.exposure, fp.shadow_strength);
    vpc.env_params2   = glm::vec4(fp.skylight_bleed, 1.0f, fp.lod_bias, 0.0f);
    vpc.wind          = glm::vec4(glm::vec2(0.9f, 0.42f) * cli.wind, t, 1.7f);
    if (cli.no_veg) {
      renderer.setExternalHdrDraw(nullptr);
    } else {
      renderer.setExternalHdrDraw([&](const RHICommandBufferPtr& cmd) {
        veg.record(cmd, vpc, renderer.getShadowDescSet());
      });
    }

    renderer.renderFrame(fp, vbo, ibo, materials, entities, prims);

    ++frame;
    if (cli.stats && frame % 60 == 0) {
      const auto& s = veg.stats();
      spdlog::info("[stats] fps={:.1f} veg total={} visible={} draws={}",
                   fps_n ? fps_acc / fps_n : 0.0, s.total_instances,
                   s.visible_instances, s.draw_calls);
      fps_acc = 0.0; fps_n = 0;
    }

    if (cli.frames > 0 && frame >= cli.frames) {
      if (!cli.screenshot.empty()) {
        device->waitIdle();
        std::vector<uint8_t> rgba;
        uint32_t W = 0, H = 0;
        if (device->readbackSwapchainImage(renderer.getLastImageIndex(), rgba, W, H)) {
          if (stbi_write_png(cli.screenshot.c_str(), static_cast<int>(W),
                             static_cast<int>(H), 4, rgba.data(),
                             static_cast<int>(W) * 4)) {
            spdlog::info("[screenshot] wrote {} ({}x{})", cli.screenshot, W, H);
          }
        } else {
          spdlog::warn("[screenshot] readback failed");
        }
      }
      running = false;
    }
  }

  device->waitIdle();
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
