#pragma once

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// Instanced sibling of `PbrForwardPipeline`. Draws many copies of a single
/// primitive in a single `drawIndexed(instances=N)` call, with per-instance
/// model matrix + color + material params pulled from a vertex-rate VBO at
/// binding=1.
///
/// Why a sibling instead of refactoring `PbrForwardPipeline`:
///   * The existing PBR path drives all 3 working PBR demos
///     (local_demo / geo_pbr_demo / earth_demo); per-instance push-constant
///     model/mvp/color is baked into their hot loops. A breaking refactor
///     risks bricking those.
///   * Effects pipelines in the engine (`BloomPass`, `SsaoPass`, `SkyboxPass`,
///     `BillboardRenderer`) follow the same "sibling pipeline" pattern —
///     adding one more is the cheap, low-blast-radius move.
///
/// Inputs the caller binds before `drawIndexed(idx, N, ...)`:
///   * vertex VBO at binding=0 (standard `MeshVertex` — pos/normal/uv)
///   * instance VBO at binding=1 (`InstanceVertex` — see below)
///   * index buffer
///   * descriptor set=0 (material — same layout as `PbrForwardPipeline`)
///   * descriptor set=1 (shadow map — same layout as `PbrForwardPipeline`)
///   * push constants (view_proj + light_vp + camera_pos + sun_dir + env)
class InstancedPbrPipeline {
 public:
  /// Per-instance vertex data — pulled from a vertex-rate VBO at binding=1.
  /// Lays out tightly so the host upload is a single memcpy.
  struct InstanceVertex {
    glm::mat4 model;        // 64 — per-instance world transform
    glm::vec4 base_color;   // 16 — albedo tint (rgb) + opacity (a)
    glm::vec4 mr_params;    // 16 — .x metallic, .y roughness, .z/.w reserved
  };
  static_assert(sizeof(InstanceVertex) == 96,
                "InstancedPbrPipeline::InstanceVertex layout mismatch");

  /// Per-frame push constants. Strictly state that's the SAME for every
  /// instance in a batch — view + sun + camera + tonemap dials.
  struct PushConstants {
    glm::mat4 view_proj;      // 64
    glm::mat4 light_vp;       // 64
    glm::vec4 camera_pos_ws;  // 16
    glm::vec4 light_dir_ws;   // 16  .xyz=light, .w=intensity
    glm::vec4 env_params;     // 16  .x sky, .y IBL diff, .z exposure, .w shadow strength
    glm::vec4 env_params2;    // 16  .x bleed, .y reserved, .z mip-bias, .w inline_tonemap
  };
  static_assert(sizeof(PushConstants) == 192,
                "InstancedPbrPipeline::PushConstants layout mismatch");

  /// Initialize against an existing render pass (typically the same HDR
  /// pass the non-instanced PBR pipeline writes to). Shares the caller's
  /// material + shadow descriptor set layouts so material slots and shadow
  /// maps work the same.
  bool initialize(RHIDevice* device,
                  RHIDescriptorSetLayoutPtr material_set_layout,
                  RHIDescriptorSetLayoutPtr shadow_set_layout,
                  RHIRenderPassPtr           render_pass);

  /// Build a `VertexInputBindingDescription` for the per-instance VBO at
  /// binding=1. Exposed so callers can pre-flight their pipeline state if
  /// they ever want to share this binding with a different program.
  static VertexInputBindingDescription getInstanceVertexBinding();
  /// Matching attribute descriptions — locations 3..6 (model mat4 = 4 vec4s)
  /// + 7 (base_color) + 8 (mr_params). Locations 0..2 are reserved for the
  /// standard mesh vertex (pos / normal / uv) at binding=0.
  static std::vector<VertexInputAttributeDescription> getInstanceVertexAttributes();

  RHIPipelinePtr       getPipeline()       const { return pipeline_; }
  RHIPipelineLayoutPtr getPipelineLayout() const { return pipeline_layout_; }

 private:
  bool createShaders();
  bool createPipeline(RHIDescriptorSetLayoutPtr material_set_layout,
                      RHIDescriptorSetLayoutPtr shadow_set_layout,
                      RHIRenderPassPtr           render_pass);

  RHIDevice*           device_ = nullptr;
  RHIShaderPtr         vert_;
  RHIShaderPtr         frag_;
  RHIPipelinePtr       pipeline_;
  RHIPipelineLayoutPtr pipeline_layout_;
};

}  // namespace hongtu::renderer
