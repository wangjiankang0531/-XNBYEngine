#pragma once

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// ShockwavePass — fullscreen ray-vs-sphere silhouette effect for explosions.
///
/// Visualization: a bright thin shell expands radially from a 3D center.
/// Per pixel:
///   1. Unproject NDC → world ray.
///   2. Compute the ray's closest-point distance `d` to the shockwave center.
///   3. If `|d - radius| < thickness` → emit additive bright color with
///      smooth falloff(closer to the radius = brighter).
///
/// Caller advances `radius` over time externally(`ShockwaveEntity::
/// getCurrentRadius(now)` makes this trivial). Pass also supports a simple
/// chromatic-aberration offset by sampling the ring shell 3 times for
/// R / G / B at slightly different radii — but for ctest screenshot
/// regression we keep it monochrome (a single bright HDR ring).
class ShockwavePass {
 public:
  struct PushConstants {
    glm::mat4 inv_view_proj;     // 64
    glm::vec4 center_radius;     // 16 — .xyz center RTC, .w radius_m
    glm::vec4 color_intensity;   // 16 — .rgb color, .a intensity
    glm::vec4 thickness_chroma;  // 16 — .x thickness_m, .y chroma offset, .zw reserved
  };
  static_assert(sizeof(PushConstants) == 112,
                "ShockwavePass::PushConstants layout mismatch");

  struct Params {
    glm::mat4 view_proj;          ///< RTC view * proj
    glm::vec3 center_rtc;
    float     radius_m;
    glm::vec3 color = {1.0f, 0.85f, 0.6f};
    float     intensity   = 2.5f;
    float     thickness_m = 0.6f;
    float     chroma_offset = 0.0f;  ///< 0 = monochrome, >0 = chromatic aberration
  };

  bool initialize(RHIDevice* device, RHIRenderPassPtr render_pass);
  void record(const RHICommandBufferPtr& cmd, const Params& p);

 private:
  bool createPipeline(RHIRenderPassPtr render_pass);

  RHIDevice*           device_ = nullptr;
  RHIShaderPtr         vs_, fs_;
  RHIPipelineLayoutPtr pipeline_layout_;
  RHIPipelinePtr       pipeline_;
};

}  // namespace hongtu::renderer
