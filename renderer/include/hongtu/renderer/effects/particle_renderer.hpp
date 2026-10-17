#pragma once

#include <hongtu/renderer/effects/particle_system.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// ParticleRenderer — instance-rendered screen-aligned quads, one quad per
/// live particle. Twin of `BillboardRenderer` but consumes
/// `ParticleInstance` (pos + size + color) directly — no atlas texture,
/// just a soft-edged radial gradient generated in fragment shader.
///
/// Pipeline:
///   * binding=0 per-vertex: none — vertex shader uses gl_VertexIndex to
///     generate 4 quad corners
///   * binding=1 per-instance: `ParticleInstance` (32 bytes,
///     `vec3 world_pos + float size + vec4 color`)
///   * triangle-strip 4 verts × N instances
///   * additive blending(色调相加,粒子重叠越亮),depth test on / write off
///
/// Coordinate-agnostic: `ParticleInstance.world_pos` is in caller's chosen
/// space (typically RTC). `Params::camera_right / camera_up` define
/// screen-alignment axes.
class ParticleRenderer {
 public:
  /// Per-frame parameters needed to billboard-orient every quad.
  struct Params {
    glm::mat4 view_proj;          ///< RTC-relative `proj * view`
    glm::vec3 camera_right;       ///< column 0 of inv(view), unit
    glm::vec3 camera_up;          ///< column 1 of inv(view), unit
  };

  bool initialize(RHIDevice* device, RHIRenderPassPtr scene_render_pass);

  /// Record draws. Caller must have already begun the scene render pass +
  /// bound viewport. No-op if `instances` is empty.
  void record(const RHICommandBufferPtr& cmd, const Params& p,
              const std::vector<ParticleInstance>& instances);

  /// GPU-backed variant — instance data 已经在 GPU buffer 里(compute shader
  /// 直接写的,例如 `ParticleSystemGPU::getInstanceBuffer()`)。比 record()
  /// 省一次 CPU → GPU 上传。
  ///   `instance_buffer` — buffer 的前 `instance_count * stride` 字节是
  ///                       `ParticleInstance`(world_pos + size + color)
  ///   `instance_count`  — 要画多少 instance
  ///   `stride_bytes`    — 0 = 默认 sizeof(ParticleInstance);非零给
  ///                       ParticleGPU(64B,前 32B 是 ParticleInstance)
  void recordFromBuffer(const RHICommandBufferPtr& cmd, const Params& p,
                         const RHIBufferPtr& instance_buffer,
                         uint32_t instance_count,
                         uint32_t stride_bytes = 0);

 private:
  struct PushConstants {
    glm::mat4 view_proj;
    glm::vec4 camera_right;       ///< xyz + pad
    glm::vec4 camera_up;          ///< xyz + pad
  };
  static_assert(sizeof(PushConstants) == 96,
                "ParticleRenderer::PushConstants layout mismatch");

  bool createPipeline(RHIRenderPassPtr render_pass);
  void uploadInstances(const std::vector<ParticleInstance>& inst);

  RHIDevice*           device_ = nullptr;
  RHIShaderPtr         vs_, fs_;
  RHIPipelineLayoutPtr pipeline_layout_;
  RHIPipelinePtr       pipeline_;

  RHIBufferPtr         inst_vbo_;
  uint32_t             inst_vbo_capacity_ = 0;
};

}  // namespace hongtu::renderer
