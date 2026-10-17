#pragma once

#include <hongtu/renderer/pbr/pbr_material.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>

#include <glm/glm.hpp>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// Dedicated ground-plane pass (lawn / terrain pad) with anti-tiling:
///
///   * Dual-scale albedo — the texture is sampled at a DETAIL tiling (~4 m)
///     and a MACRO tiling (~32 m) and blended by fragment distance. Close-up
///     keeps blade detail; at distance the macro sample takes over, so the
///     repeat pattern doesn't read as a dense grid ("密密麻麻").
///   * Macro variation — low-frequency value noise modulates brightness so
///     hundreds of metres of lawn don't look like one stamped tile.
///
/// Lighting matches the PBR forward path (sun + PCF shadow + cube-IBL when a
/// layout is supplied, analytic sky otherwise), so grass and the meshes
/// standing on it agree. One textured PbrMaterial at set=0 (lawn diff /
/// normal / arm), shadow at set=1, optional IBL cubes at set=2.
class GroundPlanePass {
 public:
  struct PushConstants {
    glm::mat4 view_proj;      // 64
    glm::mat4 light_vp;       // 64
    glm::vec4 camera_pos_ws;  // 16
    glm::vec4 light_dir_ws;   // 16 .xyz light dir, .w intensity
    glm::vec4 env_params;     // 16 .x sky, .y IBL, .z exposure, .w shadow
    glm::vec4 env_params2;    // 16 .x bleed, .y reserved, .z reserved, .w reserved
    glm::vec4 tiling;         // 16 .x detail uv/m, .y macro uv/m,
                              //    .z blend start (m), .w blend end (m)
  };
  static_assert(sizeof(PushConstants) == 208, "layout mismatch");

  bool initialize(RHIDevice* device,
                  RHIDescriptorSetLayoutPtr material_set_layout,
                  RHIDescriptorSetLayoutPtr shadow_set_layout,
                  RHIRenderPassPtr           render_pass,
                  RHIDescriptorSetLayoutPtr ibl_set_layout    = nullptr,
                  RHIDescriptorSetLayoutPtr lights_set_layout = nullptr);

  /// (Re)build the plane mesh. `half_size` metres from center to edge.
  bool buildPlane(float half_size, int divisions = 16);

  /// Record the draw. Caller binds nothing — the pass binds its pipeline,
  /// the supplied material/shadow/IBL sets and its own VBO/IBO.
  void record(const RHICommandBufferPtr& cmd, const PushConstants& pc,
              const PbrMaterial& material,
              const RHIDescriptorSetPtr& shadow_set,
              const RHIDescriptorSetPtr& ibl_set    = nullptr,
              const RHIDescriptorSetPtr& lights_set = nullptr);

  /// 同一管线画调用方持有的网格(路径条带等)—— 着色器按世界坐标铺 UV,
  /// 对路面这类各向同性纹理曲线段也成立。
  void recordMesh(const RHICommandBufferPtr& cmd, const PushConstants& pc,
                  const PbrMaterial& material,
                  const RHIDescriptorSetPtr& shadow_set,
                  const RHIBufferPtr& vbo, const RHIBufferPtr& ibo,
                  uint32_t index_count,
                  const RHIDescriptorSetPtr& ibl_set    = nullptr,
                  uint32_t index_offset                 = 0,
                  const RHIDescriptorSetPtr& lights_set = nullptr);

  float halfSize() const { return built_half_; }

 private:
  bool createShaders();

  RHIDevice*           device_ = nullptr;
  bool                 has_ibl_    = false;
  bool                 has_lights_ = false;  // set=3 点光源 UBO(仅 IBL+lights 同时非空时)
  RHIShaderPtr         vs_, fs_;
  RHIPipelineLayoutPtr layout_;
  RHIPipelinePtr       pipeline_;
  RHIBufferPtr         vbo_, ibo_;
  uint32_t             index_count_ = 0;
  float                built_half_  = 0.0f;
};

}  // namespace hongtu::renderer
