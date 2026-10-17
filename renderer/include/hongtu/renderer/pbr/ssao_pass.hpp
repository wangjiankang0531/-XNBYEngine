#pragma once

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// Screen-space ambient occlusion — depth-only variant.
///
/// Samples the main pass's depth buffer (no separate G-buffer normal pass)
/// and writes a single-channel R8 AO factor texture. Normals are
/// reconstructed in the fragment shader from view-space-position derivatives
/// (cross of dFdx/dFdy), so we don't need a normal G-buffer either.
///
/// Sampling pattern: 12-tap spiral inside a sphere around the current pixel,
/// re-rotated per pixel via a small noise. Roughly free at 1080p on Apple M-series.
///
/// Outputs are consumed by `HdrComposeTarget::recordCompose()` which
/// multiplies the AO factor into the HDR colour before tonemap.
class SsaoPass {
 public:
  /// Push constants forwarded to the SSAO fragment shader.
  struct PushData {
    glm::mat4 proj;        // 64 — same projection used by the main pass
    glm::mat4 inv_proj;    // 64 — inverse, for view-space reconstruction
    glm::vec4 params;      // 16 — .x radius (view-space meters)
                           //      .y intensity (1.0 = full strength)
                           //      .z bias (depth-equality tolerance)
                           //      .w reserved
  };
  static_assert(sizeof(PushData) == 144, "SsaoPass::PushData layout mismatch");

  /// `depth_view` is the sampleable view into the main pass's depth target
  /// (the main pass must transition it to ShaderReadOnlyOptimal at finalLayout).
  /// AO target is sized to the device's swapchain extent — apps with a real
  /// swapchain use this overload.
  bool initialize(RHIDevice* device, RHITextureViewPtr depth_view);

  /// Same but with explicit AO target dimensions — for headless / editor
  /// paths that don't have a swapchain (getSwapchainExtent returns 0).
  bool initialize(RHIDevice* device, RHITextureViewPtr depth_view,
                   uint32_t width, uint32_t height);

  /// Re-bind to a new depth view (e.g. swapchain resize). Re-creates the
  /// AO target sized to the device's current swapchain extent.
  bool resize(RHITextureViewPtr depth_view);

  /// Re-bind to a new depth view with explicit AO target dimensions.
  bool resize(RHITextureViewPtr depth_view, uint32_t width, uint32_t height);

  RHITextureViewPtr getAoView() const { return ao_view_; }

  /// Record the fullscreen triangle into `cmd`. Assumes cmd has begun; this
  /// call begins+ends its own render pass.
  void record(const RHICommandBufferPtr& cmd, const PushData& pc);

 private:
  bool createTargetAndFramebuffer();
  bool createPipeline();

  RHIDevice*                device_ = nullptr;

  RHITextureViewPtr         depth_view_;
  RHISamplerPtr             depth_sampler_;
  RHITexturePtr             ao_tex_;
  RHITextureViewPtr         ao_view_;
  RHIRenderPassPtr          render_pass_;
  RHIFramebufferPtr         framebuffer_;
  RHIShaderPtr              vs_;
  RHIShaderPtr              fs_;
  RHIPipelineLayoutPtr      pipeline_layout_;
  RHIPipelinePtr            pipeline_;
  RHIDescriptorSetLayoutPtr desc_layout_;
  RHIDescriptorSetPtr       desc_set_;
  /// Optional explicit dimensions. When non-zero,
  /// createTargetAndFramebuffer uses these instead of
  /// getSwapchainExtent. Set by the (device, view, w, h) overload.
  uint32_t                  explicit_w_ = 0u;
  uint32_t                  explicit_h_ = 0u;
};

}  // namespace hongtu::renderer
