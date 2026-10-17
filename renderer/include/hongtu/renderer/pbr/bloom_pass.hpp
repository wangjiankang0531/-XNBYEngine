#pragma once

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// Multi-mip bloom — threshold → downsample chain → upsample chain → blend.
///
/// Pattern is the standard Unreal/Unity "physically-based bloom":
///   1. From HDR colour, write only the brightness above `threshold` into mip 0
///      of the chain (also halving resolution)
///   2. Downsample N times (each ÷2) with a 4-tap box filter — captures
///      progressively wider glow
///   3. Upsample back from smallest mip to largest, additively blending into
///      the next-larger mip with a bilinear lift filter
///   4. The largest mip (mip 0, half the HDR resolution) is then sampled by
///      the compose pass and added on top of HDR before tonemap
///
/// Output is fed to `HdrComposeTarget::setBloomView()`; compose's fragment
/// shader does `final = hdr + bloom * intensity` before ACES.
class BloomPass {
 public:
  struct Params {
    float threshold = 1.0f;   ///< Brightness gate — only HDR above this glows
    float intensity = 0.5f;   ///< Final mix factor in the compose pass
    float radius    = 1.0f;   ///< Upsample blur radius (≈ Karis "filterRadius")
  };

  /// Build the mip chain, the threshold pipeline, and the down/up pipelines
  /// against the caller's HDR view (which the threshold pass samples).
  bool initialize(RHIDevice* device,
                   RHITextureViewPtr hdr_view,
                   uint32_t hdr_width,
                   uint32_t hdr_height);

  /// Sampleable view of the final (largest) bloom mip — half the HDR
  /// resolution. Compose pass binds this.
  RHITextureViewPtr getBloomView() const;

  /// Record threshold + downsample + upsample. Caller has already begun the
  /// command buffer; this call internally manages its own render passes.
  void record(const RHICommandBufferPtr& cmd, const Params& params);

 private:
  bool createMipChain(uint32_t hdr_w, uint32_t hdr_h);
  bool createPipelines();

  struct MipLevel {
    uint32_t            w{}, h{};
    RHITexturePtr       tex;
    RHITextureViewPtr   view;
    RHIFramebufferPtr   fb;
    RHIDescriptorSetPtr sampler_desc;   // bound to prev-mip view (for down/up)
  };

  RHIDevice*                device_ = nullptr;
  RHITextureViewPtr         hdr_view_;
  uint32_t                  hdr_w_ = 0;
  uint32_t                  hdr_h_ = 0;

  static constexpr uint32_t kMipCount = 5;        // 1/2, 1/4, 1/8, 1/16, 1/32
  std::vector<MipLevel>     mips_;                 // size = kMipCount

  // Three pipelines share the same render pass and descriptor layout
  // (single CombinedImageSampler binding pointing at the previous mip / HDR).
  RHIRenderPassPtr          render_pass_;
  RHISamplerPtr             sampler_;
  RHIDescriptorSetLayoutPtr desc_layout_;
  RHIPipelineLayoutPtr      pipeline_layout_;
  RHIShaderPtr              vs_fullscreen_;
  RHIShaderPtr              fs_threshold_;
  RHIShaderPtr              fs_downsample_;
  RHIShaderPtr              fs_upsample_;
  RHIPipelinePtr            pipeline_threshold_;
  RHIPipelinePtr            pipeline_downsample_;
  RHIPipelinePtr            pipeline_upsample_;

  // Descriptor set bound to the HDR view (used by the threshold pass).
  RHIDescriptorSetPtr       hdr_desc_;
};

}  // namespace hongtu::renderer
