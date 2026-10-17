#pragma once

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

namespace hongtu::renderer {

using namespace HongTu::RHI;

/// InfiniteGridPass — fullscreen ray-traced ground-plane grid (z=0).
///
/// Why this instead of polyline meshes: world-width lines (eg PolylineRenderer)
/// look heavy when the camera is close (a 0.07 m wide line takes 7% of a 1 m
/// cell), then vanish when the camera is high (sub-pixel). This pass keeps
/// every line ~1 pixel wide regardless of distance / angle, and smoothly
/// switches between fine (1 m) and major (10 m) grids as the user zooms.
///
/// Per pixel:
///   1. Unproject NDC → world ray.
///   2. Intersect with the z=0 plane → world point P.
///   3. Compute fwidth(P.xy) — the world-size of one screen pixel here.
///   4. Two grid layers: fine at `fine_scale`, major at `major_scale`.
///      For each: alpha = 1 - min(distance_to_nearest_line / fwidth, 1).
///   5. Fade fine layer to 0 when cells become sub-pixel.
///   6. X-axis (P.y==0) and Y-axis (P.x==0) get distinct accent colors.
///   7. Write gl_FragDepth at the plane intersection so opaque geometry
///      occludes the grid (and the grid occludes things behind it).
class InfiniteGridPass {
 public:
  struct PushConstants {
    glm::mat4 view_proj;       // 64 — for writing gl_FragDepth
    glm::mat4 inv_view_proj;   // 64 — for unprojecting screen → world
    glm::vec4 camera_world;    // 16 — .xyz camera world pos, .w fade_distance
    glm::vec4 fine_color;      // 16 — minor lines (rgba)
    glm::vec4 major_color;     // 16 — major lines (rgba)
    glm::vec4 x_axis_color;    // 16 — X-axis accent (rgba)
    glm::vec4 y_axis_color;    // 16 — Y-axis accent (rgba)
    glm::vec4 scales;          // 16 — .x fine_scale_m, .y major_scale_m
                               //      .z fine_fade_start_px, .w fine_fade_end_px
  };
  static_assert(sizeof(PushConstants) == 224,
                "InfiniteGridPass::PushConstants layout mismatch");

  struct Params {
    glm::mat4 view_proj;          ///< RTC view * proj (same one used for meshes)
    glm::vec3 camera_world_pos;   ///< RTC camera position (eye)
    float     fade_distance = 200.0f;
    glm::vec4 fine_color   {0.42f, 0.45f, 0.50f, 1.0f};
    glm::vec4 major_color  {0.28f, 0.30f, 0.36f, 1.0f};
    glm::vec4 x_axis_color {0.85f, 0.35f, 0.35f, 1.0f};
    glm::vec4 y_axis_color {0.35f, 0.80f, 0.40f, 1.0f};
    float     fine_scale_m  = 1.0f;
    float     major_scale_m = 10.0f;
    /// Smoothly fade the fine grid out when one cell projects to fewer than
    /// `fine_fade_end_px` screen pixels; fully visible above
    /// `fine_fade_start_px`.
    float     fine_fade_start_px = 4.0f;
    float     fine_fade_end_px   = 2.0f;
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
