#pragma once

#include <glm/glm.hpp>
#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>

#include <cstdint>
#include <vector>

namespace hongtu::renderer {

  using namespace HongTu::RHI;

  // =============================================================================
  // Pure fan triangulation — no Vulkan dependency. Lives in header so unit
  // tests can include it without RHI.
  // =============================================================================

  /// Single vertex of a polygon fill. Position is in the same space as the
  /// input points (typically RTC). Color is per-vertex so a downstream
  /// renderer can fade individual polygons.
  struct PolygonVertex {
    glm::vec3 position;
    glm::vec4 color;
    /// 质心归一坐标(中心≈0,边界≈±1):扫描线/网格/脉冲等填充效果用。
    glm::vec2 uv = {0.0f, 0.0f};
  };

  /// Triangulate an outer ring into a fan from `points[0]`.
  ///
  /// Works for convex polygons and simple concave polygons that are "mostly
  /// convex around the first vertex". For arbitrary concave / self-intersecting
  /// rings, swap in an ear-clipping pass (P0 keeps it simple — see spec §11).
  ///
  /// Returns vertices in **triangle-list** order: triangles (v0, v1, v2),
  /// (v0, v2, v3), (v0, v3, v4), ... Empty if `points.size() < 3`.
  std::vector<PolygonVertex> triangulateFan(const std::vector<glm::vec3>& points, const glm::vec4& color);

  // =============================================================================
  // PolygonRenderer — GPU draw of filled polygons into the scene pass.
  // Built against the same render pass format the PBR pipeline uses so it can
  // run inside `beginRenderPass`/`endRenderPass`.
  //
  // For outlines (closed polylines around a polygon's perimeter), feed the
  // vertices through PolylineRenderer with `closed = true` separately.
  // =============================================================================

  class PolygonRenderer {
  public:
    struct PolygonDraw {
      std::vector<glm::vec3> points;
      glm::vec4 color = {1.0f, 1.0f, 1.0f, 0.5f};
      /// 填充效果:0=纯色 1=扫描·雷达 2=网格·科技格 3=脉冲·呼吸。
      int fill_type = 0;
      /// 效果参数:.x = 主参(扫描转速 / 网格密度 / 脉冲频率)。
      glm::vec4 fill_params = {1.0f, 0.0f, 0.0f, 0.0f};
    };

    struct Params {
      glm::mat4 view_proj; ///< RTC-relative `proj * view`
      float time_s = 0.0f; ///< 扫描/脉冲动画时基
    };

    bool initialize(RHIDevice* device, RHIRenderPassPtr scene_render_pass);

    /// Record polygon draws. The caller must already be inside a render pass
    /// with viewport/scissor set. No-op when `polygons` is empty.
    void record(const RHICommandBufferPtr& cmd, const Params& p, const std::vector<PolygonDraw>& polygons);

    /// Already triangulated annotation geometry (including concave rings and
    /// screen-width ribbons). Same blend/depth state as normal polygon fills.
    void recordTriangles(const RHICommandBufferPtr& cmd, const Params& p, const std::vector<PolygonVertex>& triangles);

  private:
    struct PushConstants {
      glm::mat4 view_proj;
      glm::vec4 fx; // x=fill_type y=time z=param_a w=reserved
    };
    static_assert(sizeof(PushConstants) == 80, "PolygonRenderer::PushConstants layout mismatch");

    bool createPipeline(RHIRenderPassPtr render_pass);
    void uploadFan(const std::vector<PolygonVertex>& fan);

    RHIDevice* device_ = nullptr;
    RHIShaderPtr vs_;
    RHIShaderPtr fs_;
    RHIPipelineLayoutPtr pipeline_layout_;
    RHIPipelinePtr pipeline_;

    RHIBufferPtr fan_vbo_;
    uint32_t fan_vbo_capacity_ = 0;
  };

} // namespace hongtu::renderer
