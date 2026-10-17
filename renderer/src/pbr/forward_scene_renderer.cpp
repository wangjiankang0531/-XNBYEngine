#include <hongtu/renderer/pbr/forward_scene_renderer.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <utility>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
inline double elapsedMs(Clock::time_point a, Clock::time_point b) {
  using ms = std::chrono::duration<double, std::milli>;
  return std::chrono::duration_cast<ms>(b - a).count();
}
}  // namespace

namespace hongtu::renderer {

bool ForwardSceneRenderer::initialize(RHIDevice* device) {
  device_ = device;
  if (!material_factory_.initialize(device_))         return false;
  if (!shadow_pass_.initialize(device_, 2048u))       return false;
  if (!hdr_compose_.initialize(device_))              return false;
  // Procedural IBL — same warm BlueSky bake the editor uses. Build the layout
  // FIRST so the PBR pipeline can reference set=2, then bake the cubes. If the
  // bake fails we fall back to the analytic-env (non-IBL) pipeline so the cloud
  // still renders (just flatter, the old behaviour).
  const bool ibl_ok = ibl_.buildLayout(device_) && ibl_.buildProcedural(device_, 1);
  if (ibl_ok) {
    if (!pbr_pipeline_.initializeWithIblOwnedPass(
            device_,
            material_factory_.getDescriptorSetLayout(),
            shadow_pass_.getDescriptorSetLayout(),
            ibl_.getDescriptorLayout(),
            hdr_compose_.getHdrFormat())) {
      return false;
    }
  } else if (!pbr_pipeline_.initialize(device_,
                                  material_factory_.getDescriptorSetLayout(),
                                  shadow_pass_.getDescriptorSetLayout(),
                                  hdr_compose_.getHdrFormat())) {
    return false;
  }
  if (!createDepthBuffer())   return false;
  if (!createFramebuffer())   return false;
  // SSAO consumes the depth view directly; the main pass's render pass has
  // been configured to leave depth in ShaderReadOnlyOptimal at end.
  if (!ssao_pass_.initialize(device_, depth_view_)) return false;
  hdr_compose_.setAoView(ssao_pass_.getAoView());
  hdr_compose_.setDepthView(depth_view_);  // full compose: bilateral AO / DoF / haze
  if (!skybox_pass_.initialize(device_, pbr_pipeline_.getRenderPass())) return false;
  // BloomPass needs the HDR view + its dimensions. It writes to its own
  // half-res mip chain and feeds the final mip 0 back to the compose pass.
  {
    auto ext = device_->getSwapchainExtent();
    if (!bloom_pass_.initialize(device_, hdr_compose_.getHdrView(),
                                  ext.width, ext.height)) return false;
    hdr_compose_.setBloomView(bloom_pass_.getBloomView());
  }
  cmd_         = device_->createCommandBuffer();
  image_avail_ = device_->createSemaphore();
  render_done_ = device_->createSemaphore();
  fence_       = device_->createFence(true);
  return cmd_ && image_avail_ && render_done_ && fence_;
}

bool ForwardSceneRenderer::createDepthBuffer() {
  auto ext = device_->getSwapchainExtent();
  TextureDesc desc;
  desc.type        = TextureType::Texture2D;
  desc.extent      = {ext.width, ext.height, 1};
  desc.mipLevels   = 1;
  desc.arrayLayers = 1;
  desc.format      = Format::D32_SFLOAT;
  // Sampled usage so SSAO can read depth in its fullscreen pass.
  desc.usage       = TextureUsageFlagBits::DepthStencilAttachment
                   | TextureUsageFlagBits::Sampled;
  desc.sampleCount = 1;
  depth_tex_ = device_->createTexture(desc);
  if (!depth_tex_) return false;
  depth_view_ = depth_tex_->createView();
  return depth_view_ != nullptr;
}

bool ForwardSceneRenderer::createFramebuffer() {
  hdr_fb_ = device_->createFramebuffer(pbr_pipeline_.getRenderPass(),
                                         {hdr_compose_.getHdrView(), depth_view_},
                                         device_->getSwapchainExtent(), 1);
  return hdr_fb_ != nullptr;
}

void ForwardSceneRenderer::renderFrame(const FrameParams& frame,
                                         const RHIBufferPtr& vbo,
                                         const RHIBufferPtr& ibo,
                                         const std::vector<PbrMaterial>& materials,
                                         const std::vector<EntityDraw>& entities,
                                         const std::vector<PrimitiveDraw>& primitives) {
  profiler_.beginFrame(++frame_counter_);
  const auto frame_start = Clock::now();

  // ---- frustum culling -----------------------------------------------------
  // Cheap CPU pre-filter: extract 6 planes from proj×view, test each entity's
  // optional AABB. Skipped entries don't reach the shadow pass or main draw.
  // Demos that don't compute AABBs (has_aabb=false) all stay visible.
  const auto cull_t0 = Clock::now();
  last_stats_ = {};
  last_stats_.total_entities = static_cast<uint32_t>(entities.size());
  const Frustum frustum =
      frame.frustum_cull_enabled
        ? InstanceBuffer::extractFrustum(frame.proj * frame.view)
        : Frustum{};

  std::vector<const EntityDraw*> visible;
  visible.reserve(entities.size());
  for (const auto& e : entities) {
    if (frame.frustum_cull_enabled && e.has_aabb &&
        !InstanceBuffer::isAABBVisible(e.aabb_world, frustum)) {
      ++last_stats_.culled_frustum;
      continue;
    }
    visible.push_back(&e);
  }
  last_stats_.drawn_entities = static_cast<uint32_t>(visible.size());
  profiler_.recordPass("cull", elapsedMs(cull_t0, Clock::now()),
                       0, 0);

  // ---- shadow pass ---------------------------------------------------------
  const auto shadow_t0 = Clock::now();
  {
    // Whether any entity restricts itself to a sub-range of `primitives`. When
    // none does, every entity casts every primitive (the legacy single-geometry
    // ground+path case) and we take the cheap flat render() path. When some do
    // (loaded glTF models), each entity must cast ONLY its own primitives — we
    // route through renderBatched(), one batch per entity, so the ground entity
    // doesn't cast the model's geometry at the origin (and vice versa).
    bool any_range = false;
    for (auto* e : visible) {
      if (e->prim_count != 0) { any_range = true; break; }
    }
    if (!any_range) {
      std::vector<glm::mat4> entity_models;
      entity_models.reserve(visible.size());
      for (auto* e : visible) entity_models.push_back(e->model);
      std::vector<ShadowPass::PrimitiveSlice> slices;
      slices.reserve(primitives.size());
      for (const auto& p : primitives) {
        slices.emplace_back(p.index_offset, p.index_count);
      }
      shadow_pass_.render(frame.sun_dir_world, frame.bbox_center, frame.bbox_extent,
                           vbo, ibo, entity_models, slices);
    } else {
      std::vector<ShadowPass::Batch> batches;
      batches.reserve(visible.size());
      for (auto* e : visible) {
        ShadowPass::Batch b;
        b.vbo = vbo;
        b.ibo = ibo;
        b.entity_models = {e->model};
        const size_t pb = e->prim_count == 0 ? 0u : e->prim_offset;
        const size_t pe = e->prim_count == 0
                              ? primitives.size()
                              : std::min<size_t>(primitives.size(),
                                                 e->prim_offset + e->prim_count);
        for (size_t pi = pb; pi < pe; ++pi) {
          b.primitives.emplace_back(primitives[pi].index_offset,
                                    primitives[pi].index_count);
        }
        batches.push_back(std::move(b));
      }
      shadow_pass_.renderBatched(frame.sun_dir_world, frame.bbox_center,
                                  frame.bbox_extent, batches);
    }
  }
  profiler_.recordPass("shadow", elapsedMs(shadow_t0, Clock::now()),
                       static_cast<uint32_t>(visible.size() * primitives.size()), 0);

  // ---- acquire + begin main pass ------------------------------------------
  const auto main_t0 = Clock::now();
  fence_->wait();
  fence_->reset();
  uint32_t image_index = device_->acquireNextImage(image_avail_);
  last_image_index_    = image_index;

  cmd_->reset();
  cmd_->begin();

  Rect2D area;
  area.offset = {0, 0};
  area.extent = device_->getSwapchainExtent();
  std::vector<std::array<float, 4>> clears = {
      {frame.clear_color_hdr.r, frame.clear_color_hdr.g, frame.clear_color_hdr.b, 1.0f},
      {1.0f, 0.0f, 0.0f, 0.0f}};
  cmd_->beginRenderPass(pbr_pipeline_.getRenderPass(), hdr_fb_, area, clears);

  Viewport vp;
  vp.x = 0.0f; vp.y = 0.0f;
  vp.width    = static_cast<float>(area.extent.width);
  vp.height   = static_cast<float>(area.extent.height);
  vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
  cmd_->setViewport(vp);
  cmd_->setScissor(area);

  // Skybox first — draws at far plane so it sits behind every entity that
  // follows. `record()` binds its own pipeline; we MUST re-bind the PBR
  // pipeline below so the entity loop doesn't accidentally draw geometry
  // through the fullscreen-triangle skybox shader.
  if (frame.skybox_enabled) {
    SkyboxPass::PushData spc;
    spc.inv_view_proj = glm::inverse(frame.proj * frame.view);
    // 与编辑器 sky_pass 喂法一致:.w = 天空动画时间(云漂移);
    // sun_dir_ws.w = 归一化太阳盘强度 clamp(intensity/6,0,1)(编辑器同款),
    // 否则黄昏太阳盘/辉光亮度与编辑器不一致。
    spc.sky_params    = glm::vec4(static_cast<float>(frame.skybox_mode),
                                  frame.skybox_display_scale,
                                  frame.night_factor, frame.compose.time);
    spc.sun_dir_ws    = glm::vec4(glm::normalize(frame.sun_dir_world),
                                  glm::clamp(frame.sun_intensity / 6.0f, 0.0f, 1.0f));
    // extra.x = inline_tonemap (0 in PBR — compose does ACES; else 1 so the
    // raw-mode HDR sky doesn't blow out). extra.y = sky_cloud_amount (mirrors
    // the editor's sky_pass_local_ push). Matches SkyboxPass::PushData.extra.
    spc.extra         = glm::vec4(frame.render_mode == 1 ? 0.0f : 1.0f,
                                  frame.sky_cloud_amount, 0.0f, 0.0f);
    skybox_pass_.record(cmd_, spc);
  }

  // (Re-)bind the PBR pipeline for the entity draws — covers both the
  // "skybox just ran and stole the binding" and the "skybox disabled,
  // first time binding" cases.
  cmd_->bindPipeline(pbr_pipeline_.getPipeline());
  cmd_->bindVertexBuffer(0, vbo);
  cmd_->bindIndexBuffer(ibo);

  auto layout = pbr_pipeline_.getPipelineLayout();
  cmd_->bindDescriptorSet(1u, shadow_pass_.getDescriptorSet(), layout);
  // set=2 IBL cubes — only when the pipeline was built IBL-capable. Mirrors the
  // editor (viewport_renderer.cpp ~1933). When the IBL bake failed the pipeline
  // falls back to analyticEnv and there's no set=2 to bind.
  if (pbr_pipeline_.hasIbl() && ibl_.getDescriptorSet()) {
    cmd_->bindDescriptorSet(2u, ibl_.getDescriptorSet(), layout);
  }

  for (const auto* ent_ptr : visible) {
    const auto& ent = *ent_ptr;
    const glm::mat4 mvp = frame.proj * frame.view * ent.model;
    // Per-entity primitive slice (default 0/0 = draw all — see EntityDraw).
    const size_t prim_begin = ent.prim_count == 0 ? 0u : ent.prim_offset;
    const size_t prim_end   = ent.prim_count == 0
                                  ? primitives.size()
                                  : std::min<size_t>(primitives.size(),
                                                     ent.prim_offset + ent.prim_count);
    for (size_t pi = prim_begin; pi < prim_end; ++pi) {
      const auto& prim = primitives[pi];
      const size_t mat_slot =
          (prim.material_id < 0 ||
           static_cast<size_t>(prim.material_id + 1) >= materials.size())
            ? 0u
            : static_cast<size_t>(prim.material_id + 1);
      const auto& gm = materials[mat_slot];
      if (!gm.desc_set) continue;
      cmd_->bindDescriptorSet(0u, gm.desc_set, layout);

      // 值初始化:PushConstants 尾部的新字段(emissive_params 等)不逐项
      // 赋值时必须为 0,否则栈垃圾直接进 shader(随机自发光)。
      PbrForwardPipeline::PushConstants pd{};
      pd.model         = ent.model;
      pd.mvp           = mvp;
      pd.base_color    = gm.base_color * ent.color_tint;
      pd.mr_params     = glm::vec4(gm.metallic, gm.roughness, 0.0f, 0.0f);
      pd.camera_pos_ws = glm::vec4(frame.eye_world, 0.0f);
      pd.light_dir_ws  = glm::vec4(glm::normalize(frame.sun_dir_world),
                                     frame.sun_intensity);
      pd.light_vp      = shadow_pass_.getLightViewProj();
      pd.env_params    = glm::vec4(frame.sky_scale,
                                     frame.ibl_diffuse_scale,
                                     frame.exposure,
                                     frame.shadow_strength);
      pd.env_params2   = glm::vec4(frame.skylight_bleed,
                                     static_cast<float>(frame.render_mode),
                                     frame.lod_bias, 0.0f);
      // Neutral HDR sky tint. MUST be set: the shader multiplies IBL+bleed
      // by this, and an uninitialized push struct fed garbage here — every
      // lighting param appeared "dead" while a huge bogus tint dominated.
      // .w = night_factor(窗灯)—— 此前误写 1.0 导致园区全天亮窗。
      pd.env_params3   = glm::vec4(1.0f, 1.0f, 1.0f, frame.night_factor);
      // 平地 demo:窗灯 ENU 基取 Z-up(零值 up 会在夜间 normalize(0) 出 NaN)。
      pd.geo_params    = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
      cmd_->pushConstants(layout,
          static_cast<ShaderStageFlags>(ShaderStageFlagBits::Vertex)
          | static_cast<ShaderStageFlags>(ShaderStageFlagBits::Fragment),
          0, sizeof(pd), &pd);
      cmd_->drawIndexed(prim.index_count, 1u, prim.index_offset);
    }
  }

  // External layers (vegetation, decals, …) draw into the same HDR pass with
  // their own pipelines. Runs after the entity loop so opaque depth is
  // already in place.
  if (external_hdr_draw_) external_hdr_draw_(cmd_);

  cmd_->endRenderPass();
  // Triangle count: rough estimate from primitives × visible entities.
  uint32_t main_draws = static_cast<uint32_t>(visible.size() * primitives.size())
                       + (frame.skybox_enabled ? 1u : 0u);
  profiler_.recordPass("main", elapsedMs(main_t0, Clock::now()),
                       main_draws, 0);

  // ---- SSAO pass: depth → AO texture --------------------------------------
  // Skipped when ssao_strength == 0 (compose still samples the 1×1 white
  // fallback, but we save the fullscreen tap).
  const auto ssao_t0 = Clock::now();
  if (frame.ssao_strength > 0.0f) {
    SsaoPass::PushData spc;
    spc.proj     = frame.proj;
    spc.inv_proj = glm::inverse(frame.proj);
    spc.params   = glm::vec4(frame.ssao_radius_m,
                              frame.ssao_intensity,
                              frame.ssao_bias,
                              0.0f);
    ssao_pass_.record(cmd_, spc);
  }
  profiler_.recordPass("ssao", elapsedMs(ssao_t0, Clock::now()),
                       frame.ssao_strength > 0.0f ? 1u : 0u, 0);

  // ---- Bloom pass: HDR → mip chain → half-res bloom RT --------------------
  // Skipped when bloom_intensity == 0 (compose samples the 1×1 black fallback
  // = additive zero, so the shader path is a no-op anyway).
  const auto bloom_t0 = Clock::now();
  if (frame.bloom_intensity > 0.0f) {
    BloomPass::Params bp;
    bp.threshold = frame.bloom_threshold;
    bp.intensity = frame.bloom_intensity;     // not used by the bloom shader,
                                              // applied in compose
    bp.radius    = frame.bloom_radius;
    bloom_pass_.record(cmd_, bp);
  }
  profiler_.recordPass("bloom", elapsedMs(bloom_t0, Clock::now()),
                       frame.bloom_intensity > 0.0f ? 1u : 0u, 0);

  // ---- compose pass: tonemap HDR → swapchain -------------------------------
  // ACES on in PBR mode (1), off for RAW / debug modes so source colours show.
  // ao_strength / bloom_intensity only take effect if the upstream pass ran;
  // otherwise the texture stays at its last value (1×1 fallback at init).
  const auto compose_t0 = Clock::now();
  {
    // Compute the sun's screen UV from sun_dir_world and the camera matrices.
    // proj already has the Vulkan Y-flip baked in (Y negated), so NDC Y maps
    // directly to vUv Y (0=top, 1=bottom) — no additional flip needed.
    // We treat sun_dir_world as a direction (w=0) so view translation is
    // cancelled, which is equivalent to projecting a far point along sun_dir
    // from the camera origin (correct for a directional/infinite light).
    glm::vec4 sclip = frame.proj * frame.view * glm::vec4(frame.sun_dir_world, 0.0f);
    glm::vec2 sun_uv(0.5f, 0.5f);
    if (sclip.w > 0.0f) {
      glm::vec2 ndc = glm::vec2(sclip) / sclip.w;
      sun_uv = ndc * 0.5f + 0.5f;
    }
    HdrComposeTarget::ComposeParams cp = frame.compose;
    cp.sun_uv = sun_uv;
    hdr_compose_.setComposeParams(cp);
  }
  hdr_compose_.recordCompose(cmd_, image_index,
                              frame.render_mode == 1,
                              frame.ssao_strength,
                              frame.bloom_intensity,
                              frame.fxaa_enabled);

  // 帧内捕获(云渲染):必须在 present 之前录拷贝,见 hpp 注释。
  if (capture_enabled_) device_->recordSwapchainCapture(cmd_, image_index);

  cmd_->end();
  device_->submit({cmd_}, {image_avail_}, {render_done_}, fence_);
  device_->present(render_done_);
  profiler_.recordPass("compose", elapsedMs(compose_t0, Clock::now()),
                       1u, 0);
  profiler_.endFrame(elapsedMs(frame_start, Clock::now()));
}

}  // namespace hongtu::renderer
