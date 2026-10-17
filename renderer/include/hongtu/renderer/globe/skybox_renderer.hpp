#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// Renders a procedural star field as the space background.
///
/// Uses a full-screen triangle (no vertex buffer, no texture) with a procedural
/// GLSL star field fragment shader. Reconstructs the view ray from NDC via the
/// inverse view-projection matrix. Depth write is disabled; depth compare
/// LESS_OR_EQUAL so the sky stays behind all geometry.
class SkyboxRenderer {
 public:
  SkyboxRenderer() = default;
  ~SkyboxRenderer();

  SkyboxRenderer(const SkyboxRenderer&) = delete;
  SkyboxRenderer& operator=(const SkyboxRenderer&) = delete;

  bool init(RHIDevice* device, RHIRenderPassPtr render_pass);
  /// `cam_pos_ecef` + `sun_dir` drive an atmospheric blue-sky gradient that
  /// blends in as the camera descends into the atmosphere (and fades back to
  /// the star field in space). Pass the real ECEF camera position.
  void render(RHICommandBufferPtr cmd, const glm::mat4& view_rot, const glm::mat4& proj,
              const glm::vec3& cam_pos_ecef, const glm::vec3& sun_dir);
  void cleanup();

 private:
  RHIDevice* device_ = nullptr;

  RHIShaderPtr vertex_shader_;
  RHIShaderPtr fragment_shader_;
  RHIPipelineLayoutPtr pipeline_layout_;
  RHIPipelinePtr pipeline_;

  bool initialized_ = false;

  bool createPipeline(RHIRenderPassPtr render_pass);
};

}  // namespace hongtu::renderer
