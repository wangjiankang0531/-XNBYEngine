#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>
#include <vector>

namespace hongtu::renderer {

class GlobeCamera;

using namespace HongTu::RHI;

/// Renders the atmospheric Fresnel glow around the globe.
///
/// A sphere mesh slightly larger than Earth (atmosphere shell, ~160 km) is
/// drawn with additive blending and a Fresnel-based fragment shader. The
/// Fresnel factor concentrates the blue-white glow at grazing (limb) angles,
/// creating the characteristic thin blue halo visible from space.
///
/// Rendering without depth test and with additive blending ensures the glow
/// correctly composites over both the globe surface (near-zero at the poles,
/// bright at the rim) and the space background.
class AtmosphereRenderer {
 public:
  AtmosphereRenderer() = default;
  ~AtmosphereRenderer();

  AtmosphereRenderer(const AtmosphereRenderer&) = delete;
  AtmosphereRenderer& operator=(const AtmosphereRenderer&) = delete;

  bool init(RHIDevice* device, RHIRenderPassPtr render_pass);

  /// Rebuild the sphere mesh when the RTC origin has changed.
  void update(const GlobeCamera& camera);

  /// Record atmosphere draw commands. Call after globe rendering.
  void render(RHICommandBufferPtr cmd, const GlobeCamera& camera, f32 aspect);

  void cleanup();

 private:
  struct AtmoVertex {
    glm::vec3 position;  // position in RTC space
    glm::vec3 normal;    // outward unit normal (normalized ECEF position)
  };

  struct AtmoPushData {
    glm::mat4 mvp;          // 64 bytes
    glm::vec4 cam_pos_rtc;  // 16 bytes (xyz = camera pos in RTC, w = unused)
  };

  void rebuildMesh(const glm::dvec3& rtc_origin);

  RHIDevice* device_ = nullptr;

  RHIShaderPtr vertex_shader_;
  RHIShaderPtr fragment_shader_;
  RHIPipelineLayoutPtr pipeline_layout_;
  RHIPipelinePtr pipeline_;

  RHIBufferPtr vertex_buffer_;
  RHIBufferPtr index_buffer_;
  uint32_t index_count_ = 0;

  glm::dvec3 cached_rtc_origin_{0.0};
  bool mesh_dirty_ = true;
  bool initialized_ = false;
};

}  // namespace hongtu::renderer
