#pragma once

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// Procedural deep-space star-field skybox.
///
/// Lifted from globe/skybox_renderer.cpp so both global (earth) and local
/// (flat) demos share the same look. No cubemap, no texture asset — the
/// fragment shader hashes a celestial-sphere grid for star positions and
/// adds a faint Milky Way band. Output is intrinsically LDR (deep-space
/// background ≈ 0, stars ≤ ~1.0), so the same shader works whether or not
/// the compose pass runs ACES afterwards.
///
/// Setup: built against the main scene render pass (compatible color + depth
/// attachments). Recorded inside the main pass between `beginRenderPass`
/// and the entity draws. Writes colour at NDC z = 1.0 (far plane) with
/// `LessOrEqual` depth test against the cleared 1.0, depth-write disabled —
/// any subsequent geometry overwrites the sky.
class SkyboxPass {
 public:
  struct PushData {
    glm::mat4 inv_view_proj;   // 64 — built per-frame, projection should be
                               //      the same Vulkan-flipped one the main pass uses
    glm::vec4 sky_params;      // 16 — .x mode: 0 = deep-space starfield,
                               //      1 = daylight (analyticEnv + sun disc —
                               //      matches the IBL so sky and lighting agree),
                               //      2 = 昼夜混合(.z = night 0 昼…1 夜:
                               //      白天渐变(含黄昏调色板)↔ 星空)
                               //      .y = display brightness(模式 1/2)
    glm::vec4 sun_dir_ws;      // 16 — .xyz sun direction (daylight mode),
                               //      .w sun disc intensity multiplier
    glm::vec4 extra;           // 16 — .x inline_tonemap(1 = 着色器内做 ACES,供
                               //      原始贴图模式用,那时 compose 不 tonemap、
                               //      HDR 天空会过曝)。time(云飘移)仍走 sky_params.w
  };
  static_assert(sizeof(PushData) == 112, "SkyboxPass::PushData layout mismatch");

  bool initialize(RHIDevice* device, RHIRenderPassPtr scene_render_pass);

  /// Record one fullscreen-triangle draw into `cmd`. Caller is responsible
  /// for having already begun the scene render pass and bound the viewport.
  void record(const RHICommandBufferPtr& cmd, const PushData& pc);

 private:
  RHIDevice*           device_ = nullptr;
  RHIShaderPtr         vs_;
  RHIShaderPtr         fs_;
  RHIPipelineLayoutPtr pipeline_layout_;
  RHIPipelinePtr       pipeline_;
};

}  // namespace hongtu::renderer
