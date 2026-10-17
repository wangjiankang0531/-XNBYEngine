#pragma once

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// Single directional-light shadow map.
///
/// Owns the depth render target, render pass, framebuffer, depth-only
/// pipeline, sampler and the `set=1 binding=0` descriptor consumed by the
/// main PBR pipeline. Builds a tight ortho light projection over a bbox
/// provided per-frame — sufficient for the small (city/CAD/indoor) scenes
/// the forward path targets; CSM lives outside this class.
///
/// Shadow-pass push constant is `mat4 lightMVP`; vertex layout is
/// `MeshVertex` (position attribute consumed, others ignored).
class ShadowPass {
 public:
  /// Push constant layout for the shadow vertex shader. Exposed so the
  /// caller's main pipeline can read `light_view_proj` and forward
  /// `light_view_proj * model` if it wants to render extra entities.
  struct PushData {
    glm::mat4 light_mvp;
  };
  static_assert(sizeof(PushData) == 64, "ShadowPass::PushData layout mismatch");

  /// One primitive's slice of the shared IBO — `(index_offset, index_count)`.
  using PrimitiveSlice = std::pair<uint32_t, uint32_t>;

  bool initialize(RHIDevice* device, uint32_t resolution = 2048u);

  /// Layout consumed by the main PBR pipeline as `set = 1`. Single
  /// CombinedImageSampler binding at `binding = 0`.
  RHIDescriptorSetLayoutPtr getDescriptorSetLayout() const { return desc_layout_; }
  RHIDescriptorSetPtr       getDescriptorSet()       const { return desc_set_; }

  /// Light-space view×proj matrix from the last `render()`. Forward this
  /// to the main pipeline so the fragment shader can project surface
  /// positions into the shadow map.
  const glm::mat4& getLightViewProj() const { return light_view_proj_; }

  /// Depth-only render pass — external casters (vegetation instancing, …)
  /// build their own depth pipelines against it.
  RHIRenderPassPtr getRenderPass() const { return render_pass_; }

  /// Extra shadow casters with their own pipelines, recorded inside the
  /// depth pass after the entity draws (viewport/scissor already set).
  using ExternalShadowDraw =
      std::function<void(const RHICommandBufferPtr& cmd, const glm::mat4& light_vp)>;
  void setExternalShadowDraw(ExternalShadowDraw fn) {
    external_draw_ = std::move(fn);
  }

  /// Render the depth pass.
  ///   light_dir       — unit vector from scene toward the sun
  ///   center, extent  — sphere around the scene that the ortho frustum covers
  ///   vbo, ibo        — shared scene geometry (MeshVertex layout)
  ///   entity_models   — one model matrix per entity (already RTC-collapsed)
  ///   primitives      — slices of the IBO drawn for every entity
  void render(const glm::vec3& light_dir,
              const glm::vec3& center,
              float extent,
              const RHIBufferPtr& vbo,
              const RHIBufferPtr& ibo,
              const std::vector<glm::mat4>& entity_models,
              const std::vector<PrimitiveSlice>& primitives);

  /// One vbo/ibo bundle drawn during a batched shadow render.
  struct Batch {
    RHIBufferPtr                vbo;
    RHIBufferPtr                ibo;
    std::vector<glm::mat4>      entity_models;
    std::vector<PrimitiveSlice> primitives;
  };

  /// Render shadow from many independent meshes (each with its own
  /// vbo/ibo) in a single depth pass. Editor uses this — every imported
  /// .glb has its own buffers, so the original `render(vbo, ibo, ...)`
  /// signature can't capture the full scene without batching.
  void renderBatched(const glm::vec3& light_dir,
                      const glm::vec3& center,
                      float extent,
                      const std::vector<Batch>& batches);

 private:
  RHIDevice*                device_ = nullptr;
  uint32_t                  resolution_ = 2048u;

  RHITexturePtr             depth_tex_;
  RHITextureViewPtr         depth_view_;
  RHIRenderPassPtr          render_pass_;
  RHIFramebufferPtr         framebuffer_;
  RHIShaderPtr              vs_;
  RHIShaderPtr              fs_;
  RHIPipelinePtr            pipeline_;
  RHIPipelineLayoutPtr      pipeline_layout_;
  RHIDescriptorSetLayoutPtr desc_layout_;
  RHIDescriptorSetPtr       desc_set_;
  RHISamplerPtr             sampler_;
  RHICommandBufferPtr       cmd_;
  glm::mat4                 light_view_proj_{1.0f};
  ExternalShadowDraw        external_draw_;
};

}  // namespace hongtu::renderer
