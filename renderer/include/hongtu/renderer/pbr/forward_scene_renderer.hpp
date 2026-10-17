#pragma once

#include <hongtu/renderer/pbr/bloom_pass.hpp>
#include <hongtu/renderer/pbr/hdr_compose_target.hpp>
#include <hongtu/renderer/pbr/pbr_forward_pipeline.hpp>
#include <hongtu/renderer/pbr/pbr_material.hpp>
#include <hongtu/renderer/pbr/procedural_ibl.hpp>
#include <hongtu/renderer/pbr/shadow_pass.hpp>
#include <hongtu/renderer/pbr/skybox_pass.hpp>
#include <hongtu/renderer/pbr/ssao_pass.hpp>
#include <hongtu/renderer/instancing/instance_buffer.hpp>   // AABB, Frustum
#include <hongtu/renderer/profiler/render_profiler.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

#include <functional>
#include <vector>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// One-stop orchestrator for the PBR forward path. Wraps the four engine
/// components (`PbrMaterialFactory`, `ShadowPass`, `HdrComposeTarget`,
/// `PbrForwardPipeline`) plus the per-frame plumbing (depth target, main
/// framebuffer, command buffer, sync primitives) and runs the full draw
/// sequence: shadow pass → main pass → compose → submit → present.
///
/// Apps use it like this:
///   1.  `renderer.initialize(device);`
///   2.  Build materials via `renderer.getMaterialFactory()` (slot 0 must be
///       the fallback `createFallback()` material).
///   3.  Every frame: fill a `FrameParams` (camera matrices + sun + tunables +
///       bbox) plus the `EntityDraw` / `PrimitiveDraw` lists, then call
///       `renderFrame(...)`.
///
/// The orchestrator does NOT own the VBO / IBO / material list — those stay
/// with the app, which is the only part of the system that understands the
/// scene graph.
class ForwardSceneRenderer {
 public:
  /// Per-frame state shared by every draw: camera, light, tunables, bbox.
  struct FrameParams {
    // Camera
    glm::mat4 view;        // RTC view matrix (world → camera)
    glm::mat4 proj;        // already Vulkan-flipped (Y negated)
    glm::vec3 eye_world;   // camera position in world space

    // Directional sun light
    glm::vec3 sun_dir_world;
    float     sun_intensity;

    // Scene bbox used by the shadow ortho frustum.
    glm::vec3 bbox_center;
    float     bbox_extent;  // half-diagonal (radius)

    // Runtime-tunable shading parameters (driven by app's keyboard / UI).
    float sky_scale;
    float ibl_diffuse_scale;
    float exposure;
    float shadow_strength;
    float skylight_bleed;
    int   render_mode;   // 0 RAW, 1 PBR, 2 NORMAL, 3 UV, 4 MIP
    float lod_bias;

    // HDR clear color the main pass uses (pre-tonemap). Pick a bright sky
    // for PBR mode so ACES has something to compress, a neutral grey for
    // RAW mode so backgrounds look natural.
    glm::vec3 clear_color_hdr;

    // SSAO controls (skipped when ssao_strength == 0).
    float ssao_strength  = 0.0f;   // 0 = disabled, 1 = full effect at compose
    float ssao_radius_m  = 1.0f;   // view-space sample radius (meters)
    float ssao_intensity = 1.5f;   // multiplies the per-tap occlusion sum
    float ssao_bias      = 0.025f; // depth-equality tolerance

    // Procedural skybox — when on, replaces the manual `clear_color_hdr`
    // with `analyticEnv()` sampled along the per-pixel view ray.
    bool  skybox_enabled = true;
    // 0 = deep-space starfield (back-compat default), 1 = daylight sky
    // (analyticEnv gradient + sun disc, consistent with the IBL).
    int   skybox_mode    = 0;
    // 昼夜系数(0 昼…1 夜):驱动窗灯(env_params3.w)与 skybox mode 2
    // 的昼夜混合。默认 0 —— 老调用方行为不变。
    float night_factor   = 0.0f;
    // Display brightness of the daylight sky (IBL unaffected). The HDR
    // gradient tones to paper-white through ACES at 1.0; ~0.5 keeps it blue.
    float skybox_display_scale = 1.0f;
    // 程序化天空云量(0=晴空…1=多云):喂给 skybox 的 extra.y,镜像编辑器
    // viewport_renderer.cpp 的 sky_pass_local_ 推送(sky_cloud_amount)。
    // 默认 0 —— 老调用方行为不变。
    float sky_cloud_amount = 0.0f;

    // Bloom — additive HDR glow blended in at compose time.
    // 0 = off; ~0.5 is a noticeable nighttime glow without washing out daytime
    // scenes (default off so existing screenshot baselines don't drift).
    float bloom_intensity = 0.0f;
    float bloom_threshold = 1.0f;   ///< HDR brightness gate; values above glow
    float bloom_radius    = 1.0f;   ///< Upsample tent-blur radius (tunes spread)

    // FXAA — applied inside the compose shader after AO/bloom/tonemap.
    // Default off (existing screenshots have hard edges and our test diffing
    // tools would otherwise flag every pixel).
    bool  fxaa_enabled    = false;

    // CPU-side frustum culling against entity AABBs (those with `has_aabb`).
    // No GPU readback needed — extracts 6 planes from `proj * view` and uses
    // `InstanceBuffer::isAABBVisible`. Off-screen entities are skipped both
    // in the shadow pass and the main draw loop. Default ON because it's
    // always a win when AABBs are provided (and a no-op when they're not).
    // (Hi-Z occlusion culling is a separate task — needs depth readback.)
    bool  frustum_cull_enabled = true;

    // 后处理参数(由 applyEnvironment 填;renderFrame 透传给 HdrComposeTarget。
    // sun_uv 在 renderFrame 内由 sun_dir_world + view/proj 计算覆盖,调用方无需设置)。
    HdrComposeTarget::ComposeParams compose;
  };

  /// Per-frame culling stats — filled after `renderFrame()` returns so demos
  /// / Profiler HUD can read how many entities the cull rejected.
  struct CullingStats {
    uint32_t total_entities   = 0;
    uint32_t culled_frustum   = 0;
    uint32_t drawn_entities   = 0;
  };

  const CullingStats& getLastCullingStats() const { return last_stats_; }

  /// Per-frame CPU timing collector. Records `shadow`, `main`, `ssao`,
  /// `bloom`, `compose` durations every `renderFrame()`. Demos read
  /// `profiler.getSummaryText()` to dump average ms/fps + per-pass split.
  RenderProfiler&       getProfiler()       { return profiler_; }
  const RenderProfiler& getProfiler() const { return profiler_; }

  /// One entity in the scene.
  struct EntityDraw {
    glm::mat4 model;      // already RTC-collapsed
    glm::vec4 color_tint; // multiplied into material base_color (use {1,1,1,1} if unused)
    // Optional world-space AABB (already RTC-relative, matching `model`'s space).
    // When `has_aabb` is true, the renderer runs frustum culling against this
    // box; entities behind / outside the view frustum are skipped. Default
    // false = "always draw" (back-compat for demos that don't compute it).
    AABB aabb_world{};
    bool has_aabb = false;
    // Per-entity primitive slice into the shared `primitives` list. The default
    // (offset 0, count 0) means "draw every primitive" — back-compat for the
    // single-geometry callers (ground + path) where one entity legitimately
    // draws all primitives. When an entity owns a distinct mesh (a loaded glTF
    // model), set [prim_offset, prim_offset+prim_count) so it draws ONLY its own
    // primitives and the ground entity doesn't draw the model's geometry (and
    // vice versa). Both the main pass and the shadow pass honor this range.
    uint32_t prim_offset = 0;
    uint32_t prim_count  = 0;  // 0 = all primitives
  };

  /// One slice of the shared IBO + which material to sample.
  struct PrimitiveDraw {
    uint32_t index_offset;
    uint32_t index_count;
    int      material_id;  // -1 = fallback (slot 0 in `materials`)
  };

  bool initialize(RHIDevice* device);

  PbrMaterialFactory&       getMaterialFactory()       { return material_factory_; }
  const PbrMaterialFactory& getMaterialFactory() const { return material_factory_; }

  // ---- extension points ---------------------------------------------------
  // Layers with their own pipelines (vegetation, decals, …) draw inside the
  // SAME HDR pass, after the entity loop. They initialize against
  // `getHdrRenderPass()` + `getShadowSetLayout()`, then record through this
  // hook (render pass, viewport and scissor are already set when it runs).
  using ExternalHdrDraw = std::function<void(const RHICommandBufferPtr& cmd)>;
  void setExternalHdrDraw(ExternalHdrDraw fn) { external_hdr_draw_ = std::move(fn); }

  RHIRenderPassPtr getHdrRenderPass() const { return pbr_pipeline_.getRenderPass(); }
  RHIDescriptorSetLayoutPtr getShadowSetLayout() {
    return shadow_pass_.getDescriptorSetLayout();
  }

  // ---- Procedural IBL --------------------------------------------------------
  // Built in initialize() (procedural BlueSky preset) and bound at set=2 for the
  // PBR mesh draws. External layers (vegetation in the cloud server) that want
  // the SAME warm IBL initialize their pipeline against getIblSetLayout() and
  // bind getIblDescriptorSet() in their record hook.
  RHIDescriptorSetLayoutPtr getIblSetLayout()      { return ibl_.getDescriptorLayout(); }
  RHIDescriptorSetPtr       getIblDescriptorSet()  { return ibl_.getDescriptorSet(); }
  ProceduralIbl&            getIbl()               { return ibl_; }
  RHIDescriptorSetPtr getShadowDescSet() { return shadow_pass_.getDescriptorSet(); }
  glm::mat4 getLightViewProj() const { return shadow_pass_.getLightViewProj(); }
  RHIRenderPassPtr getShadowRenderPass() const { return shadow_pass_.getRenderPass(); }
  /// External shadow casters (vegetation, …) — forwarded to ShadowPass.
  void setExternalShadowDraw(ShadowPass::ExternalShadowDraw fn) {
    shadow_pass_.setExternalShadowDraw(std::move(fn));
  }

  /// 帧内捕获开关:开启后每帧 compose 之后把 swapchain 图像拷进设备的
  /// 持久 staging(present 之前,race-free),用
  /// `device->readCapturedFrame()` 取像素。云渲染逐帧推流必须走这条 ——
  /// 对已 present 图像做 readbackSwapchainImage 在 MoltenVK 上会间歇读到
  /// 压缩 tile 垃圾(黑块闪烁)。
  void setCaptureEnabled(bool on) { capture_enabled_ = on; }

  /// Swapchain image index of the most recent `renderFrame` — feed to
  /// `RHIDevice::readbackSwapchainImage` for headless screenshots.
  uint32_t getLastImageIndex() const { return last_image_index_; }

  /// Render one frame and present. Every entity draws every primitive — the
  /// caller pre-flattens into the two vectors before calling.
  void renderFrame(const FrameParams& frame,
                    const RHIBufferPtr& vbo,
                    const RHIBufferPtr& ibo,
                    const std::vector<PbrMaterial>& materials,
                    const std::vector<EntityDraw>& entities,
                    const std::vector<PrimitiveDraw>& primitives);

 private:
  bool createDepthBuffer();
  bool createFramebuffer();

  RHIDevice*          device_ = nullptr;

  PbrMaterialFactory  material_factory_;
  ShadowPass          shadow_pass_;
  HdrComposeTarget    hdr_compose_;
  ProceduralIbl       ibl_;
  PbrForwardPipeline  pbr_pipeline_;
  SsaoPass            ssao_pass_;
  SkyboxPass          skybox_pass_;
  BloomPass           bloom_pass_;

  RHITexturePtr       depth_tex_;
  RHITextureViewPtr   depth_view_;
  RHIFramebufferPtr   hdr_fb_;
  RHICommandBufferPtr cmd_;
  RHISemaphorePtr     image_avail_;
  RHISemaphorePtr     render_done_;
  RHIFencePtr         fence_;

  CullingStats        last_stats_{};
  RenderProfiler      profiler_{300};
  uint32_t            frame_counter_ = 0;
  ExternalHdrDraw     external_hdr_draw_;
  bool                capture_enabled_ = false;
  uint32_t            last_image_index_ = 0;
};

}  // namespace hongtu::renderer
