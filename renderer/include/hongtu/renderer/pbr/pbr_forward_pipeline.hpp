#pragma once

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// PBR forward-pass pipeline — the heart of the metallic-roughness path.
///
/// Owns:
///   * The forward render pass (HDR color attachment + depth)
///   * The Cook-Torrance + IBL + shadow shader pair (built from
///     `effects/shading_glsl.hpp` snippets so global / local modes share
///     a single shading model)
///   * The graphics pipeline + pipeline layout (two descriptor sets:
///     set=0 material, set=1 shadow map)
///
/// The render target (HDR colour view, depth view, framebuffer) and command
/// buffer remain caller-owned — they're per-swapchain plumbing the demo /
/// renderer-orchestrator already manages.
class PbrForwardPipeline {
 public:
  /// Per-draw push constants. Layout must exactly match the GLSL block
  /// composed in `pbr_forward_pipeline.cpp`. 320 bytes — fits inside
  /// MoltenVK's small push range and the 4 KiB Vulkan minimum maxPushConstantsSize.
  struct PushConstants {
    glm::mat4 mvp;            // 64
    glm::mat4 model;          // 64
    glm::vec4 base_color;     // 16  .rgb = baseColorFactor
    glm::vec4 mr_params;      // 16  .x = metallic factor, .y = roughness factor
    glm::vec4 camera_pos_ws;  // 16  .xyz = camera world position
    glm::vec4 light_dir_ws;   // 16  .xyz = light dir toward camera, .w = intensity
    glm::mat4 light_vp;       // 64  world → light clip-space (shadow lookup)
    glm::vec4 env_params;     // 16  .x sky scale, .y IBL diffuse mul, .z exposure, .w shadow strength
    glm::vec4 env_params2;    // 16  .x skylight bleed, .y debug mode, .z mip-bias,
                              //     .w inline_tonemap (1 = ACES inside shader; for
                              //         SRGB framebuffers without an HDR compose pass)
    glm::vec4 env_params3;    // 16  .rgb = HDR sky tint multiplier on IBL diffuse +
                              //              skylight bleed (1,1,1) = neutral / no HDR.
                              //     .w   = night_factor(0 白天…1 深夜,驱动程序化窗灯)
    glm::vec4 emissive_params;// 16  .rgb = emissiveFactor×strength, .w 保留
    glm::vec4 geo_params;     // 16  .xyz = 世界"上"方向(本地 (0,0,1);geo RTC
                              //      = 锚点 ENU up),.w = 高度偏置:窗灯的
                              //      离地高度 = dot(wp, up) + w(RTC 下 wp 是
                              //      眼相对坐标,偏置补回真实地面高)。
  };
  static_assert(sizeof(PushConstants) == 336, "PbrForwardPipeline::PushConstants layout mismatch");

  /// HDR-mode init: builds an offscreen HDR render pass + matching pipeline.
  /// Caller draws into an HDR target and runs ACES inside a later compose
  /// pass (this is what `ForwardSceneRenderer` does).
  bool initialize(RHIDevice* device,
                   RHIDescriptorSetLayoutPtr material_set_layout,
                   RHIDescriptorSetLayoutPtr shadow_set_layout,
                   Format hdr_color_format);

  /// Embedded-mode init: skips building a render pass and instead builds the
  /// pipeline against the caller's existing render pass (typical use: drop
  /// the PBR draw straight into an app's main pass that writes the swapchain
  /// directly). Caller must set `PushConstants::env_params2.w = 1` per draw
  /// so the shader applies ACES before output (otherwise HDR values blow
  /// out the SRGB framebuffer).
  bool initializeWithRenderPass(RHIDevice* device,
                                  RHIDescriptorSetLayoutPtr material_set_layout,
                                  RHIDescriptorSetLayoutPtr shadow_set_layout,
                                  RHIRenderPassPtr existing_render_pass);

  /// Embedded-mode init + IBL cubemap support. When `ibl_set_layout` is
  /// non-null,the shader replaces the analytic env sample with
  /// `samplerCube envDiffuse` (set=2 binding=0) + `samplerCube envSpec`
  /// (set=2 binding=1). Caller binds the env cubes via set 2 each draw.
  /// When `ibl_set_layout` is null, falls back to analyticEnv (same path
  /// as initializeWithRenderPass).
  /// `instanced = true` builds an INSTANCED variant: the per-instance model
  /// matrix is read from instanced vertex attributes (binding=1, locations
  /// 3-6) and `PushConstants::mvp` is reused as the shared view_proj. Lets the
  /// editor draw many same-mesh instances in one `drawIndexed(count, N)`.
  bool initializeWithIbl(RHIDevice* device,
                          RHIDescriptorSetLayoutPtr material_set_layout,
                          RHIDescriptorSetLayoutPtr shadow_set_layout,
                          RHIDescriptorSetLayoutPtr ibl_set_layout,
                          RHIRenderPassPtr existing_render_pass,
                          RHIDescriptorSetLayoutPtr lights_set_layout = nullptr,
                          bool instanced = false);

  /// HDR-mode init (creates its own offscreen HDR render pass like
  /// `initialize`) + IBL cube support. Used by `ForwardSceneRenderer` (cloud):
  /// it owns the framebuffer against `getRenderPass()` and wants the same warm
  /// cube IBL the editor binds, but — unlike the editor — has no pre-built HDR
  /// render pass to feed `initializeWithIbl`. Equivalent to
  /// `initialize(format)` followed by binding set=2 each draw.
  bool initializeWithIblOwnedPass(RHIDevice* device,
                                   RHIDescriptorSetLayoutPtr material_set_layout,
                                   RHIDescriptorSetLayoutPtr shadow_set_layout,
                                   RHIDescriptorSetLayoutPtr ibl_set_layout,
                                   Format hdr_color_format);

  /// Returns true if the pipeline was built with IBL cube binding active.
  /// Editor checks this to decide whether to bind set=2 each draw.
  bool hasIbl() const { return has_ibl_; }

  RHIRenderPassPtr     getRenderPass()     const { return render_pass_; }
  RHIPipelinePtr       getPipeline()       const { return pipeline_; }
  RHIPipelineLayoutPtr getPipelineLayout() const { return pipeline_layout_; }

 private:
  bool createRenderPass(Format hdr_color_format);
  bool createShaders();
  bool createPipeline(RHIDescriptorSetLayoutPtr material_set_layout,
                       RHIDescriptorSetLayoutPtr shadow_set_layout);

  RHIDevice*           device_ = nullptr;
  RHIRenderPassPtr     render_pass_;
  RHIShaderPtr         vert_;
  RHIShaderPtr         frag_;
  RHIPipelinePtr       pipeline_;
  RHIPipelineLayoutPtr pipeline_layout_;
  bool                 has_ibl_ = false;
  // 点光源 UBO(set=3)是否在 pipeline layout 里。shader 对 set=3 的声明 +
  // evalPointLights 调用必须与此一致 —— 否则 MoltenVK 给 shader 引用但
  // layout 缺失的 argument buffer 资源定不出 base type,createGraphicsPipelines
  // 直接失败(CRS/earth_demo 等非 IBL 路径无 set=3,实测崩在这)。
  bool                 has_lights_ = false;
  // Instanced 变体:per-instance 模型矩阵走实例顶点属性(binding=1 loc3-6),
  // pc.mvp 当 view_proj 用。编辑器批量画同 mesh 实例时启用。
  bool                 instanced_ = false;
};

}  // namespace hongtu::renderer
