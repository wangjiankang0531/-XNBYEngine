#pragma once

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// HDR offscreen color RT + fullscreen-triangle "compose" pass that tonemaps
/// HDR → swapchain. Owns the half-float HDR target and per-swapchain-image
/// framebuffers for the compose pass. The main scene pass renders into
/// `getHdrView()`; the demo then calls `recordCompose()` to tonemap +
/// present.
///
/// ACES toggle: `recordCompose(cmd, image_index, tonemap_enabled=true)`
/// runs the Karis-Narkowicz ACES filmic curve; passing `false` outputs the
/// linear HDR values straight through (used by RAW-mode debug views so the
/// asset's native texture colours stay untouched).
class HdrComposeTarget {
 public:
  /// Full cinematic post-FX parameter set, ported from the editor's compose.
  /// Every field defaults to a NEUTRAL / OFF value so that not calling
  /// `setComposeParams` leaves the output bit-identical to the legacy compose
  /// (tonemap + AO + bloom + FXAA via `recordCompose`'s 4 args).
  struct ComposeParams {
    int   tonemap_mode = 1;          // 0 none / 1 ACES / 2 AgX / 3 Filmic / 4 Reinhard
    float exposure = 1.0f;
    float ao_strength = 0.0f;
    float bloom_intensity = 0.0f;
    bool  fxaa = false;
    float grade_saturation = 1.0f, grade_contrast = 1.0f, grade_temperature = 0.0f;
    float vignette = 0.0f, chromatic_aberration = 0.0f, sharpen = 0.0f, grain = 0.0f;
    float godray_intensity = 0.0f, godray_decay = 0.96f;
    glm::vec2 sun_uv{0.5f, 0.5f};
    float lensflare_intensity = 0.0f, lensflare_ghost = 0.0f, lensflare_halo = 0.0f;
    float dof_intensity = 0.0f, dof_range = 0.08f, dof_max_blur = 8.0f;
    float haze_intensity = 0.0f, haze_start = 0.6f, haze_power = 2.0f;
    float rain = 0.0f, snow = 0.0f;
    float time = 0.0f;
    float znear = 0.1f, zfar = 5000.0f;
    // The engine swapchain is an sRGB format, so the hardware performs the
    // linear→sRGB encode automatically — keep this false to avoid double
    // encoding. The editor renders into a UNORM target and sets it true so the
    // shader encodes sRGB itself.
    bool  srgb_encode = false;
  };

  /// Store the latest cinematic post-FX parameters. They flow into the next
  /// `recordCompose` (the extended fields; the 4 legacy args still override
  /// tonemap/ao/bloom/fxaa for backwards compatibility).
  void setComposeParams(const ComposeParams& p) { params_ = p; }

  bool initialize(RHIDevice* device);

  /// Offscreen variant: instead of composing into the swapchain, compose into a
  /// class-OWNED color texture of `output_format` (e.g. R8G8B8A8_UNORM for the
  /// editor's GL-upload path) at `width`×`height`. The compose render pass's
  /// color attachment uses `output_format` and ends in `ShaderReadOnlyOptimal`
  /// (so the result is sampleable / readback-able), NOT `PresentSrc`. The HDR
  /// intermediate RT is the same R16G16B16A16_SFLOAT as the swapchain path.
  ///
  /// After this, `recordCompose(cmd, /*image_index ignored*/0, ...)` renders
  /// into the single owned framebuffer; read the result via `getOutputTexture()`
  /// / `getOutputView()`.
  bool initializeOffscreen(RHIDevice* device, uint32_t width, uint32_t height,
                           Format output_format);

  /// Recreate the HDR RT + owned output texture + framebuffer at a new size
  /// (editor viewport resize). Re-points the descriptor set at the new HDR view.
  /// No-op / false if not in offscreen mode. Keeps `output_format` unchanged.
  bool resizeOffscreen(uint32_t width, uint32_t height);

  /// Class-owned compose OUTPUT (offscreen mode only). Null in swapchain mode.
  /// The texture is created with ColorAttachment | Sampled | TransferSrc so the
  /// caller can both sample it and read it back to a buffer.
  RHITexturePtr     getOutputTexture() const { return output_color_; }
  RHITextureViewPtr getOutputView() const { return output_view_; }

  /// Sampleable view into the HDR offscreen color RT. Attach this to the
  /// main scene-pass framebuffer's color attachment.
  RHITextureViewPtr getHdrView() const { return hdr_view_; }

  /// The HDR offscreen color RT texture (for explicit barriers / readback).
  RHITexturePtr getHdrTexture() const { return hdr_color_; }

  /// OFFSCREEN ONLY — bind an EXTERNAL HDR color view as the compose pass's
  /// binding-0 input instead of this class's own `hdr_view_`. The caller then
  /// renders the scene into its OWN HDR texture (and owns the
  /// barriers/lifetime); the compose just samples it. Pass null to revert to
  /// the class-owned HDR. Used by the editor so its scene→HDR→BloomPass path
  /// stays byte-identical to its legacy self-owned target. Re-binds the
  /// descriptor set. The owned `hdr_color_`/`hdr_view_` are still allocated but
  /// unused while an external view is set.
  void setExternalHdrView(RHITextureViewPtr v) {
    external_hdr_view_ = v;
    rebuildDescriptorSet();
  }

  /// Format the HDR RT was created with — useful when building the main
  /// render pass's color attachment description.
  Format getHdrFormat() const { return Format::R16G16B16A16_SFLOAT; }

  /// Optional: bind an SSAO `R8_UNORM` texture that the compose pass will
  /// multiply into the HDR colour before tonemap. Pass a null view to detach.
  /// Must be called AFTER `initialize` — re-creates the descriptor set so the
  /// AO sampler points at the new view.
  void setAoView(RHITextureViewPtr ao_view);

  /// Optional: bind a bloom RGBA half-res texture that the compose pass will
  /// **add** to the HDR colour (× `bloom_intensity` in recordCompose) before
  /// tonemap. Null detaches → 1×1 black fallback.
  void setBloomView(RHITextureViewPtr bloom_view);

  /// Optional: bind the scene depth view (`D32_SFLOAT`). The full compose uses
  /// it for the bilateral AO blur, depth-of-field auto-focus, and distance
  /// haze. Null detaches → 1×1 depth=1.0 fallback (so those effects degrade to
  /// no-ops rather than reading uninitialized memory).
  void setDepthView(RHITextureViewPtr depth_view);

  /// Optional: shockwave-style **screen-space ring distortion**. Each pixel
  /// inside the shell `|d - radius| < thickness` gets its sample UV pushed
  /// radially outward by `strength × shell_falloff`. Generic enough that
  /// other effects (heat haze halos, refraction)可以复用 — only the parameter
  /// names mention shockwave. Caller projects the 3D ring center via the
  /// view-projection matrix to screen-space UV (0..1) before passing in.
  ///
  /// Pass `strength == 0` to disable (default).
  void setShockwaveDistortion(glm::vec2 center_uv,
                              float     radius_uv,
                              float     thickness_uv,
                              float     strength);

  /// Heat haze around N hot point sources (fire / vent / hot pipe).
  ///   `sources[i].xy` — screen UV position of the source
  ///   `sources[i].z`  — radius_uv(扰动范围)
  ///   `sources[i].w`  — per-source intensity multiplier
  /// `time_seconds` 用来驱动 noise 时间相位(让 shimmer 抖动)。
  /// `global_strength == 0` 关闭(默认)。最多 4 个 source —— 超出截断。
  /// 与 shockwave distortion 独立工作(两个 UV 偏移叠加 sample HDR)。
  void setHeatHazeSources(const std::vector<glm::vec4>& sources,
                           float time_seconds,
                           float global_strength);

  /// Record the fullscreen-triangle compose pass into `cmd`. Assumes `cmd`
  /// has begun. Internally begins/ends the compose render pass.
  ///   `tonemap_enabled`  — true = ACES filmic, false = pass-through (RAW mode)
  ///   `ao_strength`      — 0.0 = AO disabled, 1.0 = full AO effect
  ///   `bloom_intensity`  — 0.0 = bloom off; 0.3-1.0 is the usual visible range
  ///   `fxaa_enabled`     — true = sample 5-tap + edge-directed blur (≈5-tap
  ///                         more samples, costs a few extra ms at 1080p)
  void recordCompose(const RHICommandBufferPtr& cmd,
                      uint32_t image_index,
                      bool  tonemap_enabled,
                      float ao_strength    = 0.0f,
                      float bloom_intensity = 0.0f,
                      bool  fxaa_enabled   = false);

 private:
  bool createHdrTarget();
  bool createHdrTarget(uint32_t width, uint32_t height);
  // Creates the 1×1 AO/bloom/depth fallback textures (shared by both paths).
  bool createFallbackTextures();
  // Builds the swapchain-format render pass + per-swapchain-image framebuffers.
  bool createSwapchainComposePass();
  // Builds the offscreen render pass (output_format, ShaderReadOnlyOptimal final
  // layout) + the owned output texture/view + single framebuffer.
  bool createOffscreenComposePass(Format output_format);
  // Shared: compiles shaders, builds sampler/descriptor/pipeline against the
  // already-created `compose_render_pass_`. Called by both paths.
  bool createComposePipelineState();
  // Legacy alias kept for the swapchain path entry point.
  bool createComposePipeline();
  void rebuildDescriptorSet();

  RHIDevice*                device_ = nullptr;

  // Offscreen mode gate. When true `recordCompose` uses `offscreen_fb_` /
  // `offscreen_extent_` and ignores `image_index`; the swapchain path is
  // untouched.
  bool                      offscreen_mode_ = false;
  Format                    output_format_ = Format::R8G8B8A8_UNORM;
  Extent2D                  offscreen_extent_{0u, 0u};
  RHITexturePtr             output_color_;     // class-owned compose output
  RHITextureViewPtr         output_view_;
  RHIFramebufferPtr         offscreen_fb_;     // single fb over output_view_

  RHITexturePtr             hdr_color_;
  RHITextureViewPtr         hdr_view_;
  // Offscreen: when non-null, compose binding-0 samples this caller-owned view
  // instead of `hdr_view_` (editor renders the scene into its own HDR target).
  RHITextureViewPtr         external_hdr_view_;

  RHIRenderPassPtr          compose_render_pass_;
  std::vector<RHIFramebufferPtr> compose_fbs_;
  RHIShaderPtr              compose_vs_;
  RHIShaderPtr              compose_fs_;
  RHIPipelinePtr            compose_pipeline_;
  RHIPipelineLayoutPtr      compose_pipeline_layout_;
  RHIDescriptorSetLayoutPtr compose_desc_layout_;
  RHIDescriptorSetPtr       compose_desc_set_;
  RHISamplerPtr             compose_sampler_;
  RHITextureViewPtr         ao_view_;          // optional — null until setAoView()
  RHITexturePtr             ao_default_tex_;   // 1×1 white fallback when no SSAO
  RHITextureViewPtr         ao_default_view_;
  RHITextureViewPtr         bloom_view_;       // optional — null until setBloomView()
  RHITexturePtr             bloom_default_tex_;   // 1×1 black fallback (no bloom)
  RHITextureViewPtr         bloom_default_view_;
  RHITextureViewPtr         depth_view_;       // optional — null until setDepthView()
  RHITexturePtr             depth_default_tex_;   // 1×1 depth=1.0 fallback
  RHITextureViewPtr         depth_default_view_;

  // Latest cinematic post-FX params (defaults = neutral/off → no regression).
  ComposeParams             params_;

  // Shockwave ring distortion(strength=0 → 关闭,push 入 push constants
  // 通过 `flags2.x` 启用)
  glm::vec2 shockwave_center_uv_ = {0.5f, 0.5f};
  float     shockwave_radius_uv_ = 0.0f;
  float     shockwave_thickness_uv_ = 0.0f;
  float     shockwave_strength_ = 0.0f;

  // Heat haze 多源(最多 4)+ 时间 + 全局 strength
  static constexpr int kMaxHeatHazeSources = 4;
  glm::vec4 heat_sources_[kMaxHeatHazeSources] = {};   // .xy uv, .z radius, .w intensity
  int       heat_source_count_  = 0;
  float     heat_time_seconds_  = 0.0f;
  float     heat_global_strength_ = 0.0f;
};

}  // namespace hongtu::renderer
