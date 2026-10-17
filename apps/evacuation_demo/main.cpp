// evacuation_demo — phase 5.6 integration. 100 agents in a 30×30m room
// with two exits and an interior wall; Timeline triggers a fire at t=2s
// that becomes an obstacle, pushing agents toward the exits. CrowdSimulator
// drives the boid + repulsive flock; each agent renders as a colored disc
// on the floor (PolygonRenderer fan).
//
// P0 simplification: agents are rendered as ground discs rather than
// SkinnedPbrPipeline characters. Real character + 2-bone walk animation
// is left as a follow-up — engine has SkinnedPbrPipeline + animation
// machinery in place(phases 5.1-5.3), just no procedural character mesh
// generator yet.
//
// Usage:
//   evacuation_demo --mode=local|global [--agents=N] [--time=T]
//                    [--frames=N --screenshot=PATH]

#include <SDL.h>
#include <SDL_vulkan.h>

#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/renderer/camera/local_camera.hpp>
#include <hongtu/renderer/animation/procedural_character.hpp>
#include <hongtu/renderer/effects/polygon_renderer.hpp>
#include <hongtu/renderer/effects/polyline_renderer.hpp>
#include <hongtu/renderer/effects/particle_renderer.hpp>
#include <hongtu/renderer/effects/particle_system.hpp>
#include <hongtu/renderer/globe/globe_camera.hpp>
#include <hongtu/renderer/pbr/skinned_pbr_pipeline.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <hongtu/rhi/RHITypes.h>
#include <hongtu/scene/agent.hpp>
#include <hongtu/scene/crowd/crowd_simulator.hpp>
#include <hongtu/scene/emitter_entity.hpp>
#include <hongtu/scene/timeline/timeline.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
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
using hongtu::scene::GeoAgent;
using hongtu::scene::LocalAgent;
using hongtu::scene::Timeline;
using hongtu::scene::Track;
using hongtu::scene::CallbackDriver;
using hongtu::scene::crowd::CrowdSimulator;
using hongtu::scene::crowd::ObstacleAABB;

namespace {

struct Cli {
  std::string mode      = "local";
  int         agents    = 100;
  float       time      = -1.0f;
  int         frames    = 0;
  std::string screenshot;
};

bool parseCli(int argc, char** argv, Cli& cli) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if      (a.rfind("--mode=",   0) == 0) cli.mode   = a.substr(7);
    else if (a.rfind("--agents=", 0) == 0) cli.agents = std::stoi(a.substr(9));
    else if (a.rfind("--time=",   0) == 0) cli.time   = std::stof(a.substr(7));
    else if (a.rfind("--frames=", 0) == 0) cli.frames = std::stoi(a.substr(9));
    else if (a.rfind("--screenshot=", 0) == 0) cli.screenshot = a.substr(13);
    else if (a == "--help" || a == "-h") {
      std::cout << "evacuation_demo --mode=local|global [--agents=N]\n"
                << "                 [--time=T] [--frames=N --screenshot=PATH]\n";
      return false;
    } else {
      std::cerr << "Unknown arg: " << a << "\n";
      return false;
    }
  }
  if (cli.mode != "local" && cli.mode != "global") return false;
  return true;
}

// 30×30 room with one interior wall + 2 exits on the right side.
// All AABB obstacles in world coordinates; for global mode the anchor is
// converted at runtime via ENU.
struct RoomLayout {
  // Each wall is an AABB(min, max). Origin = room center.
  std::vector<ObstacleAABB> walls;
  // Two exit centers — agents will pick the nearest.
  std::vector<glm::dvec3>    exits;
};

RoomLayout buildRoom() {
  RoomLayout r;
  // 30×30 outer perimeter walls (thickness 0.5m), with gaps for exits on +X.
  // North wall (long, full width)
  r.walls.push_back({{-15, 14.5, -1}, {15, 15.0, 2}});
  // South wall
  r.walls.push_back({{-15, -15.0, -1}, {15, -14.5, 2}});
  // West wall
  r.walls.push_back({{-15.0, -15, -1}, {-14.5, 15, 2}});
  // East wall — split with 2 exit gaps at y ∈ [-5,-3] and [3,5]
  r.walls.push_back({{14.5, -15.0, -1}, {15.0, -5.0, 2}});
  r.walls.push_back({{14.5, -3.0,  -1}, {15.0,  3.0, 2}});
  r.walls.push_back({{14.5,  5.0,  -1}, {15.0, 15.0, 2}});
  // Interior dividing wall — splits room into left/right halves with 1.5m gap
  r.walls.push_back({{-0.25, -15.0, -1}, {0.25, -0.75, 2}});
  r.walls.push_back({{-0.25,  0.75, -1}, {0.25, 15.0, 2}});

  r.exits.push_back({15.0, -4.0, 0.0});
  r.exits.push_back({15.0,  4.0, 0.0});
  return r;
}

class EvacuationDemo {
 public:
  explicit EvacuationDemo(Cli cli) : cli_(std::move(cli)) {}

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

  glm::dvec3 nearestExit(const glm::dvec3& pos) const {
    glm::dvec3 best = room_.exits[0];
    double best_d2 = glm::dot(pos - best, pos - best);
    for (size_t i = 1; i < room_.exits.size(); ++i) {
      double d2 = glm::dot(pos - room_.exits[i], pos - room_.exits[i]);
      if (d2 < best_d2) { best_d2 = d2; best = room_.exits[i]; }
    }
    return best;
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

  RoomLayout                                room_;
  std::vector<std::unique_ptr<LocalAgent>>  local_agents_;
  std::vector<std::unique_ptr<GeoAgent>>    geo_agents_;
  CrowdSimulator                            crowd_;
  Timeline                                  timeline_;
  bool                                       fire_active_ = false;

  std::unique_ptr<PolygonRenderer>  polygon_renderer_;
  std::unique_ptr<PolylineRenderer> polyline_renderer_;
  std::unique_ptr<hongtu::renderer::ParticleRenderer> particle_renderer_;
  std::unique_ptr<hongtu::renderer::ParticleSystem>   fire_particles_;

  // Skinned character infrastructure — all agents share the same procedural
  // capsule mesh + skin pose (lockstep walking animation). Per-agent
  // variance is just position + heading via push-constant model matrix.
  std::unique_ptr<hongtu::renderer::SkinnedPbrPipeline> skinned_pipeline_;
  hongtu::renderer::animation::CharacterAsset           character_;
  RHIBufferPtr                  char_vbo_;
  RHIBufferPtr                  char_ibo_;
  RHIBufferPtr                  char_skin_ssbo_;     // 2 mat4 = 128 B
  RHIDescriptorSetPtr           char_desc_set_;
  hongtu::renderer::animation::SkinPose       char_pose_;
};

bool EvacuationDemo::initialize() {
  if (!initWindow())                  return false;
  if (!initVulkan())                  return false;
  if (!createRenderPass())            return false;
  if (!createDepthAndFramebuffers())  return false;
  if (!initRenderers())               return false;
  buildScene();
  return true;
}

bool EvacuationDemo::initWindow() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) return false;
  std::string title = "XNBY Evacuation Demo (--mode=" + cli_.mode + ")";
  window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED,
                             SDL_WINDOWPOS_CENTERED, window_w_, window_h_,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN);
  return window_ != nullptr;
}

bool EvacuationDemo::initVulkan() {
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

bool EvacuationDemo::createRenderPass() {
  RenderPassDesc rpd;
  AttachmentDescription ca;
  ca.format = device_->getSwapchainFormat();
  ca.sampleCount = 1;
  ca.initialLayout = ImageLayout::Undefined;
  ca.finalLayout   = ImageLayout::PresentSrc;
  ca.loadOpClear = true; ca.storeOpStore = true;
  rpd.attachments.push_back(ca);
  AttachmentDescription da;
  da.format = Format::D32_SFLOAT;
  da.sampleCount = 1;
  da.initialLayout = ImageLayout::Undefined;
  da.finalLayout   = ImageLayout::DepthStencilAttachmentOptimal;
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

bool EvacuationDemo::createDepthAndFramebuffers() {
  TextureDesc dd;
  dd.type = TextureType::Texture2D;
  dd.extent = {static_cast<uint32_t>(window_w_), static_cast<uint32_t>(window_h_), 1u};
  dd.mipLevels = 1u; dd.arrayLayers = 1u;
  dd.format = Format::D32_SFLOAT;
  dd.usage = static_cast<TextureUsageFlags>(TextureUsageFlagBits::DepthStencilAttachment);
  dd.sampleCount = 1u;
  depth_tex_ = device_->createTexture(dd);
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

bool EvacuationDemo::initRenderers() {
  polygon_renderer_  = std::make_unique<PolygonRenderer>();
  polyline_renderer_ = std::make_unique<PolylineRenderer>();
  particle_renderer_ = std::make_unique<hongtu::renderer::ParticleRenderer>();
  skinned_pipeline_  = std::make_unique<hongtu::renderer::SkinnedPbrPipeline>();
  if (!polygon_renderer_->initialize(device_, render_pass_))  return false;
  if (!polyline_renderer_->initialize(device_, render_pass_)) return false;
  if (!particle_renderer_->initialize(device_, render_pass_)) return false;
  if (!skinned_pipeline_->initialize(device_, render_pass_))  return false;

  // ---------------------------------------------------------------------------
  // Build procedural capsule character + upload VBO/IBO/SSBO once.
  // ---------------------------------------------------------------------------
  character_ = hongtu::renderer::animation::ProceduralCharacter::buildCapsule();
  hongtu::renderer::animation::AnimationSampler::bindPose(character_.skeleton, char_pose_);

  {
    BufferDesc bd;
    bd.size  = character_.vertices.size() * sizeof(hongtu::renderer::SkinnedMeshVertex);
    bd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Vertex);
    bd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
    char_vbo_ = device_->createBuffer(bd);
    if (!char_vbo_) return false;
    std::memcpy(char_vbo_->map(), character_.vertices.data(), bd.size);
    char_vbo_->unmap();
  }
  {
    BufferDesc bd;
    bd.size  = character_.indices.size() * sizeof(uint32_t);
    bd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Index);
    bd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
    char_ibo_ = device_->createBuffer(bd);
    if (!char_ibo_) return false;
    std::memcpy(char_ibo_->map(), character_.indices.data(), bd.size);
    char_ibo_->unmap();
  }
  {
    BufferDesc bd;
    bd.size  = character_.skeleton.jointCount() * sizeof(glm::mat4);  // 2 mat4 = 128 B
    bd.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Storage);
    bd.memoryProperties = MemoryPropertyFlagBits::HostVisible | MemoryPropertyFlagBits::HostCoherent;
    char_skin_ssbo_ = device_->createBuffer(bd);
    if (!char_skin_ssbo_) return false;
  }
  char_desc_set_ = skinned_pipeline_->createDescriptorSet(
      char_skin_ssbo_,
      static_cast<uint32_t>(character_.skeleton.jointCount() * sizeof(glm::mat4)));
  return char_desc_set_ != nullptr;
}

void EvacuationDemo::buildScene() {
  room_ = buildRoom();

  // Camera + origin.
  if (cli_.mode == "local") {
    origin_.setOrigin({0.0, 0.0, 0.0});
    auto cam = std::make_unique<LocalCamera>();
    cam->setScreenSize(static_cast<float>(window_w_), static_cast<float>(window_h_));
    cam->lookAtTarget({0, 0, 0}, /*distance=*/45.0,
                      /*yaw=*/0.4, /*pitch=*/-1.1);     // bird's-eye-ish
    camera_ = std::move(cam);
  } else {
    Geodetic anchor = Geodetic::fromDegrees(39.9042, 116.4074, 0);
    origin_.setOriginGeodetic(anchor);
    auto cam = std::make_unique<GlobeCamera>();
    cam->setScreenSize(static_cast<float>(window_w_), static_cast<float>(window_h_));
    cam->setMinDistance(5.0);
    cam->lookAt(anchor, /*distance=*/55.0, /*heading=*/30.0, /*pitch=*/-65.0);
    camera_ = std::move(cam);
  }

  // Spawn agents in random positions inside the left half of the room.
  std::mt19937 rng(42);
  std::uniform_real_distribution<double> x_dist(-13.0, -1.0);
  std::uniform_real_distribution<double> y_dist(-13.0,  13.0);

  crowd_.clearAgents();
  crowd_.clearObstacles();

  const int N = std::max(1, cli_.agents);
  local_agents_.clear(); geo_agents_.clear();
  local_agents_.reserve(cli_.mode == "local" ? N : 0);
  geo_agents_  .reserve(cli_.mode == "global" ? N : 0);

  for (int i = 0; i < N; ++i) {
    glm::dvec3 p {x_dist(rng), y_dist(rng), 0.0};
    glm::dvec3 exit = nearestExit(p);
    if (cli_.mode == "local") {
      auto a = std::make_unique<LocalAgent>();
      a->setPosition(p);
      a->setTarget(exit);
      a->setMaxSpeedMps(1.4f + 0.4f * (i % 3) / 3.0f);  // small variance
      crowd_.addAgent(a.get());
      local_agents_.push_back(std::move(a));
    } else {
      // Convert local meter offsets to geodetic via ENU around anchor.
      Geodetic anchor = Geodetic::fromDegrees(39.9042, 116.4074, 0);
      glm::dvec3 ecef_anchor = hongtu::coordinate::geodeticToECEF(anchor);
      glm::dvec3 ecef_pos = hongtu::coordinate::enuToECEF(p, anchor, ecef_anchor);
      auto a = std::make_unique<GeoAgent>();
      a->setPositionGeo(hongtu::coordinate::ecefToGeodetic(ecef_pos));
      glm::dvec3 ecef_exit = hongtu::coordinate::enuToECEF(exit, anchor, ecef_anchor);
      a->setTarget(ecef_exit);
      a->setMaxSpeedMps(1.4f);
      crowd_.addAgent(a.get());
      geo_agents_.push_back(std::move(a));
    }
  }

  // Register walls (and later fire) as obstacles. For local mode the AABBs
  // are already in world coords. For global mode we need to convert each
  // wall AABB from ENU meters to ECEF — but ECEF AABBs are not really
  // axis-aligned at this scale; for P0 we just use the same local AABBs
  // and accept that obstacle math approximates in ECEF (room is < 30m,
  // ECEF curvature negligible).
  for (const auto& w : room_.walls) crowd_.addObstacle(w);

  // Fire particles (placeholder spec — used when timeline turns fire on).
  fire_particles_ = std::make_unique<hongtu::renderer::ParticleSystem>(2048);

  // Timeline:fire ignites at t=2s (interior of room, slightly off-center).
  timeline_.clearDrivers();
  timeline_.setDuration(45.0f);
  timeline_.play();
  fire_active_ = false;

  Track<bool> fire_track;
  fire_track.addKey(0.0f, false);
  fire_track.addKey(2.0f, true);
  timeline_.addDriver(std::make_unique<CallbackDriver<bool>>(
      std::move(fire_track),
      [this](const bool& on) {
        if (on && !fire_active_) {
          // Add fire AABB as an obstacle and start particle emission.
          // Fire location: room center (between the two sub-rooms gap).
          crowd_.addObstacle(ObstacleAABB{{-1.5, -1.5, -1.0}, {1.5, 1.5, 2.0}});
          // Make sure all agents target their nearest exit.
          for (auto& a : local_agents_) a->setTarget(nearestExit(a->getPosition()));
          fire_active_ = true;

          hongtu::renderer::EmitterDesc d =
              hongtu::renderer::EmitterPresets::makeFire({0, 0, 0.5f}, 200.0f);
          fire_particles_->setEmitter(d);
        }
      }));

  if (cli_.time >= 0.0f) {
    // Headless screenshot mode — drive timeline to target time discretely
    // so the fire actually fires before we capture.
    const float step = 1.0f / 60.0f;
    for (float t = 0; t < cli_.time; t += step) timeline_.update(step);
    // Also pre-simulate crowd for time elapsed.
    for (float t = 0; t < cli_.time; t += step) crowd_.update(step);
    if (fire_active_) {
      const int fire_steps = static_cast<int>(std::ceil((cli_.time - 2.0f) * 60.0f));
      for (int i = 0; i < std::max(0, fire_steps); ++i) fire_particles_->update(step);
    }
  }
}

void EvacuationDemo::renderFrame() {
  fence_->wait();
  fence_->reset();
  image_index_ = device_->acquireNextImage(image_avail_);

  const float aspect = static_cast<float>(window_w_) / static_cast<float>(window_h_);
  glm::mat4 view_proj = camera_->getProjectionMatrix(aspect)
                      * camera_->getRTCViewMatrix(origin_);
  glm::vec3 cam_pos_rtc = origin_.worldToLocal(camera_->getEyeWorld());
  glm::dmat4 view64 = camera_->getViewMatrix();
  glm::mat4 view32(view64);
  glm::vec3 cam_right(view32[0][0], view32[1][0], view32[2][0]);
  glm::vec3 cam_up   (view32[0][1], view32[1][1], view32[2][1]);

  // Build world draws — walls as filled polygons, agent discs as colored
  // polygon fans.
  auto agentToWorld = [&](size_t idx) -> glm::dvec3 {
    if (cli_.mode == "local") return local_agents_[idx]->getWorldPosition();
    return geo_agents_[idx]->getWorldPosition();
  };

  std::vector<PolygonRenderer::PolygonDraw> polys;
  std::vector<PolylineRenderer::PolylineDraw> lines;

  // Floor outline as polylines.
  glm::vec4 floor_color {0.2f, 0.25f, 0.3f, 1.0f};
  glm::vec4 wall_color  {0.5f, 0.5f, 0.55f, 0.7f};
  for (const auto& w : room_.walls) {
    PolygonRenderer::PolygonDraw d;
    glm::vec3 a = origin_.worldToLocal({w.min.x, w.min.y, 0.0});
    glm::vec3 b = origin_.worldToLocal({w.max.x, w.min.y, 0.0});
    glm::vec3 c = origin_.worldToLocal({w.max.x, w.max.y, 0.0});
    glm::vec3 e = origin_.worldToLocal({w.min.x, w.max.y, 0.0});
    d.points = {a, b, c, e};
    d.color  = wall_color;
    polys.push_back(std::move(d));
  }

  // Agents are rendered as SkinnedPbrPipeline characters further down,
  // not as disc polygons — keep the polygon list for walls + exits only.

  // Exits as bright green stripes.
  for (const auto& e : room_.exits) {
    PolygonRenderer::PolygonDraw d;
    glm::vec3 c = origin_.worldToLocal(e);
    d.points = {
        {c.x - 0.4f, c.y - 1.0f, 0.02f},
        {c.x + 0.4f, c.y - 1.0f, 0.02f},
        {c.x + 0.4f, c.y + 1.0f, 0.02f},
        {c.x - 0.4f, c.y + 1.0f, 0.02f},
    };
    d.color = {0.3f, 1.0f, 0.4f, 1.0f};
    polys.push_back(std::move(d));
  }

  // Fire particles.
  std::vector<hongtu::renderer::ParticleInstance> particle_inst;
  if (fire_active_) fire_particles_->buildInstanceBuffer(particle_inst);

  cmd_->reset();
  cmd_->begin();
  Rect2D area;
  area.offset = {0, 0};
  area.extent = device_->getSwapchainExtent();
  std::vector<std::array<float, 4>> clears = {
      {floor_color.r, floor_color.g, floor_color.b, 1.0f},
      {1.0f, 0.0f, 0.0f, 0.0f}};
  cmd_->beginRenderPass(render_pass_, framebuffers_[image_index_], area, clears);
  Viewport vp;
  vp.x = 0; vp.y = 0;
  vp.width  = static_cast<float>(area.extent.width);
  vp.height = static_cast<float>(area.extent.height);
  vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
  cmd_->setViewport(vp);
  cmd_->setScissor(area);

  PolygonRenderer::Params gp;
  gp.view_proj = view_proj;
  polygon_renderer_->record(cmd_, gp, polys);

  // ---------------------------------------------------------------------------
  // Skinned characters — sample shared walking animation, upload skin
  // matrices to the shared SSBO once, then draw N characters with per-agent
  // model push constant. All agents move in lockstep visually; the bigger
  // value-add of the SkinnedPbrPipeline is showcased here without needing
  // a real glTF asset.
  // ---------------------------------------------------------------------------
  {
    using hongtu::renderer::animation::AnimationSampler;
    const float anim_t = std::fmod(timeline_.currentTime(),
                                    character_.walking.duration_seconds);
    AnimationSampler::bindPose(character_.skeleton, char_pose_);
    AnimationSampler::sample(character_.skeleton, character_.walking, anim_t, char_pose_);
    std::vector<glm::mat4> skin_mats;
    character_.skeleton.skinMatrices(char_pose_, skin_mats);
    void* dst = char_skin_ssbo_->map();
    std::memcpy(dst, skin_mats.data(), skin_mats.size() * sizeof(glm::mat4));
    char_skin_ssbo_->unmap();

    cmd_->bindPipeline(skinned_pipeline_->getPipeline());
    cmd_->bindDescriptorSet(0, char_desc_set_, skinned_pipeline_->getPipelineLayout());
    cmd_->bindVertexBuffer(0, char_vbo_);
    cmd_->bindIndexBuffer(char_ibo_);

    hongtu::renderer::SkinnedPbrPipeline::PushConstants pc{};
    pc.view_proj   = view_proj;
    pc.light_dir   = glm::vec4(glm::normalize(glm::vec3(0.3f, 0.4f, -1.0f)), 0.0f);
    pc.light_color = glm::vec4(1.0f, 0.95f, 0.85f, 1.0f);
    pc.base_color  = glm::vec4(0.85f, 0.7f, 0.45f, 1.0f);   // skin tone

    auto pushAndDraw = [&](const glm::mat4& model) {
      pc.model = model;
      cmd_->pushConstants(
          skinned_pipeline_->getPipelineLayout(),
          static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
            | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment),
          0, sizeof(pc), &pc);
      cmd_->drawIndexed(static_cast<uint32_t>(character_.indices.size()), 1, 0, 0, 0);
    };
    if (cli_.mode == "local") {
      for (auto& a : local_agents_) pushAndDraw(a->getRTCModelMatrix(origin_));
    } else {
      for (auto& a : geo_agents_) pushAndDraw(a->getRTCModelMatrix(origin_));
    }
  }

  if (!particle_inst.empty()) {
    hongtu::renderer::ParticleRenderer::Params pp;
    pp.view_proj    = view_proj;
    pp.camera_right = cam_right;
    pp.camera_up    = cam_up;
    particle_renderer_->record(cmd_, pp, particle_inst);
  }

  cmd_->endRenderPass();
  cmd_->end();
  device_->submit({cmd_}, {image_avail_}, {render_done_}, fence_);
  device_->present(render_done_);
}

void EvacuationDemo::run() {
  using clock = std::chrono::steady_clock;
  auto t_prev = clock::now();
  int frame_idx = 0;
  bool running = true;
  SDL_Event ev;
  std::cout << "[evacuation_demo] mode=" << cli_.mode
            << " agents=" << cli_.agents
            << " time=" << cli_.time
            << "\n";

  while (running) {
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT) running = false;
      else if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = false;
    }
    float dt = 0.0f;
    if (cli_.frames > 0) dt = 1.0f / 60.0f;
    else {
      auto t_now = clock::now();
      dt = std::chrono::duration<float>(t_now - t_prev).count();
      t_prev = t_now;
    }
    // In screenshot mode buildScene already drove sim forward; here we just
    // render. In interactive mode tick everything live.
    if (cli_.time < 0.0f) {
      timeline_.update(dt);
      crowd_.update(dt);
      if (fire_active_) fire_particles_->update(dt);
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
            std::cout << "[evacuation_demo] wrote " << cli_.screenshot
                      << " (" << W << "x" << H << ")\n";
          }
        }
      }
      running = false;
    }
  }
  device_->waitIdle();
}

void EvacuationDemo::shutdown() {
  if (device_) device_->waitIdle();
  fire_particles_.reset();
  local_agents_.clear();
  geo_agents_.clear();
  char_desc_set_.reset();
  char_skin_ssbo_.reset();
  char_ibo_.reset();
  char_vbo_.reset();
  skinned_pipeline_.reset();
  particle_renderer_.reset();
  polygon_renderer_.reset();
  polyline_renderer_.reset();
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
  EvacuationDemo app(std::move(cli));
  if (!app.initialize()) { std::cerr << "init failed\n"; return 1; }
  app.run();
  app.shutdown();
  return 0;
}
