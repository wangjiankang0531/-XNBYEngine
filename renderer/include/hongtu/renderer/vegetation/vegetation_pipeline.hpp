#pragma once

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// Vegetation sibling of `InstancedPbrPipeline` (same instancing contract:
/// MeshVertex at binding=0, InstanceVertex at binding=1, material set=0,
/// shadow set=1). Two vegetation-specific additions:
///
///   * VS wind sway — vertices displace horizontally with an animated,
///     per-instance-phased gust. Weight grows quadratically with the
///     vertex's normalized height in the ASSET's local Y (glTF up), so
///     trunks stay planted while canopies move.
///   * FS alpha cutoff — leaf/grass cards and photoscan foliage use
///     alphaMode MASK/BLEND; a 0.5 discard keeps silhouettes without the
///     sorting cost of true blending. Opaque materials (alpha = 1) are
///     unaffected.
///
/// Like the parent, this draws with cull None (foliage is double-sided).
class VegetationPipeline {
 public:
  /// Identical to InstancedPbrPipeline::InstanceVertex — the vegetation
  /// layer feeds the same 96-byte stream.
  struct InstanceVertex {
    glm::mat4 model;
    glm::vec4 base_color;  // rgb tint, a opacity
    glm::vec4 mr_params;   // .x metallic, .y roughness
  };
  static_assert(sizeof(InstanceVertex) == 96, "layout mismatch");

  /// InstancedPbrPipeline::PushConstants + 2 vegetation vec4s.
  struct PushConstants {
    glm::mat4 view_proj;      // 64
    glm::mat4 light_vp;       // 64
    glm::vec4 camera_pos_ws;  // 16
    glm::vec4 light_dir_ws;   // 16 .xyz light dir, .w intensity
    glm::vec4 env_params;     // 16 .x sky, .y IBL, .z exposure, .w shadow strength
    glm::vec4 env_params2;    // 16 .x bleed, .y unused, .z mip bias, .w tonemap
    glm::vec4 wind;           // 16 .xy dir·strength (world XY), .z time, .w frequency
    glm::vec4 archetype;      // 16 .x asset height (m), .y sway amplitude (m)
  };
  static_assert(sizeof(PushConstants) == 224, "layout mismatch");

  /// `ibl_set_layout`(可选,set=2 双 cube):提供时用真 cube IBL(与
  /// PbrForwardPipeline 的 has_ibl_ 路径同款),否则回退解析天空 env。
  /// 编辑器传它(植被与网格吃同一环境光),demo 不传。
  bool initialize(RHIDevice* device,
                  RHIDescriptorSetLayoutPtr material_set_layout,
                  RHIDescriptorSetLayoutPtr shadow_set_layout,
                  RHIRenderPassPtr           render_pass,
                  RHIDescriptorSetLayoutPtr ibl_set_layout = nullptr);

  bool hasIbl() const { return has_ibl_; }

  RHIPipelinePtr       getPipeline()       const { return pipeline_; }
  RHIPipelineLayoutPtr getPipelineLayout() const { return pipeline_layout_; }

 private:
  bool createShaders();
  bool createPipeline(RHIDescriptorSetLayoutPtr material_set_layout,
                      RHIDescriptorSetLayoutPtr shadow_set_layout,
                      RHIRenderPassPtr           render_pass,
                      RHIDescriptorSetLayoutPtr ibl_set_layout);
  bool has_ibl_ = false;

  RHIDevice*           device_ = nullptr;
  RHIShaderPtr         vert_;
  RHIShaderPtr         frag_;
  RHIPipelinePtr       pipeline_;
  RHIPipelineLayoutPtr pipeline_layout_;
};

}  // namespace hongtu::renderer
