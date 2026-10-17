#pragma once

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// Skinned vertex layout — extends MeshVertex with per-vertex joint indices
/// and weights. Stride matches the glTF loader's MeshData output:
///   pos (12) + normal (12) + uv (8) + tangent (16) + joints (16) + weights (16) = 80
struct SkinnedMeshVertex {
  glm::vec3  position;     // 0
  glm::vec3  normal;       // 12
  glm::vec2  uv;           // 24
  glm::vec4  tangent;      // 32
  glm::uvec4 joint_indices;// 48
  glm::vec4  joint_weights;// 64
};
static_assert(sizeof(SkinnedMeshVertex) == 80,
              "SkinnedMeshVertex layout drift — check glTF skin extraction");

/// Sibling pipeline to PbrForwardPipeline that does skinning. Kept minimal:
/// directional light + base color + simple half-Lambert diffuse + ambient.
/// Full PBR (IBL / shadow / spec) can be layered on later by following the
/// same shader-mixin approach as PbrForwardPipeline.
///
/// Per-draw inputs:
///   - VBO binding 0: SkinnedMeshVertex array
///   - Index buffer: standard uint32 indices
///   - SSBO set=0 binding=0: `mat4 joint_matrices[]` — skin matrices from
///     `Skeleton::skinMatrices`. Per draw call (per character).
///   - Push constants: view_proj + model + light_dir + light_color + base_color
class SkinnedPbrPipeline {
 public:
  struct PushConstants {
    glm::mat4 view_proj;     // 64
    glm::mat4 model;         // 64
    glm::vec4 light_dir;     // 16 (xyz dir, w unused)
    glm::vec4 light_color;   // 16 (rgb intensity, a unused)
    glm::vec4 base_color;    // 16 (rgba albedo)
  };
  static_assert(sizeof(PushConstants) == 176,
                "SkinnedPbrPipeline::PushConstants layout mismatch — "
                "stays well under MoltenVK's 256B push constant limit");

  bool initialize(RHIDevice* device, RHIRenderPassPtr scene_render_pass);

  RHIPipelinePtr        getPipeline()       const { return pipeline_; }
  RHIPipelineLayoutPtr  getPipelineLayout() const { return pipeline_layout_; }
  RHIDescriptorSetLayoutPtr getDescriptorSetLayout() const { return desc_layout_; }

  /// Create a descriptor set bound to `joint_matrices_ssbo`. The caller owns
  /// the buffer (size = jointCount × 64B mat4) and is responsible for
  /// uploading skin matrices each frame (via map/memcpy). The albedo sampler
  /// (set=0 binding=1) is bound to a built-in 1×1 white texture, so the
  /// fragment result is `base_color` alone (capsule/procedural agents).
  RHIDescriptorSetPtr createDescriptorSet(const RHIBufferPtr& joint_matrices_ssbo,
                                          uint32_t buffer_range_bytes);

  /// Same, but binds a real albedo texture (set=0 binding=1). The shader
  /// multiplies `base_color × texture(albedo, uv)`, so pass base_color = white
  /// to show the texture verbatim. Null view/sampler falls back to white.
  RHIDescriptorSetPtr createDescriptorSet(const RHIBufferPtr& joint_matrices_ssbo,
                                          uint32_t buffer_range_bytes,
                                          const RHITextureViewPtr& albedo_view,
                                          const RHISamplerPtr& albedo_sampler);

 private:
  bool createPipeline(RHIRenderPassPtr render_pass);
  bool createDefaultWhiteTexture();

  RHIDevice*                 device_ = nullptr;
  RHIShaderPtr               vs_;
  RHIShaderPtr               fs_;
  RHIDescriptorSetLayoutPtr  desc_layout_;
  RHIPipelineLayoutPtr       pipeline_layout_;
  RHIPipelinePtr             pipeline_;
  // Fallback albedo (1×1 white) so the binding=1 sampler is always valid even
  // for untextured/procedural draws.
  RHITexturePtr              white_tex_;
  RHITextureViewPtr          white_view_;
  RHISamplerPtr              white_sampler_;
};

}  // namespace hongtu::renderer
