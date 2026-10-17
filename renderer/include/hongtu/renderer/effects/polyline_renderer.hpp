#pragma once

#include <hongtu/rhi/RHIDevice.h>
#include <hongtu/rhi/RHIResources.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace hongtu::renderer {

using namespace HongTu::RHI;

// =============================================================================
// Pure ribbon math — no Vulkan dependency. Lives in this header so unit tests
// can include it without dragging in the RHI library.
// =============================================================================

/// A single vertex of an expanded polyline ribbon. Position is in the same
/// space as the input points (caller's choice — typically RTC). Color is
/// per-vertex so a downstream renderer can fade individual ribbons.
///
/// `arc_length` carries cumulative distance from the polyline's start to
/// this vertex (in the same units as `position`). The fragment shader uses
/// this to compute dash patterns when `PolylineDraw::dashed` is true.
/// **Sentinel**: `arc_length < 0` marks an arrow-head vertex, which the
/// fragment shader treats as always-solid (arrow heads ignore the dash
/// pattern so they read as solid markers regardless of the rest of the
/// line's style).
struct RibbonVertex {
  glm::vec3 position;
  glm::vec4 color;
  float     arc_length;
  /// 横向坐标:+1=左缘、-1=右缘、0=中线/箭头。描边线按 |side| 分芯/缘,
  /// 发光线按 |side| 做边缘衰减。普通/虚线忽略。
  float     side = 0.0f;
};

/// Expand a polyline into a triangle list of `RibbonVertex` pairs. For each
/// input point we emit two vertices, offset by ±`half_width` along the side
/// direction. The side direction is computed by:
///
///   side(i) = normalize( cross( avg_dir(i), view_dir(i) ) )
///
/// where `avg_dir(i)` is the average of the incoming and outgoing segment
/// directions and `view_dir(i)` is the unit vector from the camera to the
/// vertex. The result is a "view-aligned" ribbon that always faces the
/// camera roughly head-on — good enough for road / pipe / route overlays
/// without needing a geometry shader.
///
/// If `closed` is true, the polyline is treated as a loop: `points[N-1]` is
/// considered to be followed by `points[0]`, and an extra closing pair of
/// triangles is emitted.
///
/// Returns vertices in **triangle-list** order: for each segment `[i, i+1]`
/// we emit 6 vertices forming two triangles:
///   (Li, Ri, Li+1)   (Ri, Ri+1, Li+1)
/// where `Li`/`Ri` are the left/right offsets at point `i`.
///
/// Returns empty if `points.size() < 2`.
///
/// `arrow_head`/`arrow_tail` append a 3-vertex triangle at the last / first
/// point pointing outward along the polyline direction. Arrows are ignored
/// for closed polylines (loops have no head/tail). `arrow_size` is the
/// edge length of the equilateral-ish arrowhead in the same units as
/// `half_width`; pass 0 to default to `6 * half_width` (≈ 3× the line
/// width).
/// 飞线几何:把折线每个相邻段抬成抛物弧(OD/连接/迁徙图)。每段在 +Z
/// 方向隆起,峰高 = 段跨度 × `height_ratio`,采样 `seg_per_span` 段。配
/// 发光线即得数字孪生招牌飞线。配普通/贴图/流动线也成立。
///
/// `height_ratio <= 0` 或点数 < 2 时原样返回(退化为直线)。
/// +Z 抬升:local 模式正好朝上;geo RTC 模式近似(中国一带够用),真·
/// 径向待 DEM/地理 up 接入。
std::vector<glm::vec3>
buildArcPolyline(const std::vector<glm::vec3>& points,
                 float height_ratio,
                 int   seg_per_span = 24);

std::vector<RibbonVertex>
expandPolylineRibbon(const std::vector<glm::vec3>& points,
                     float half_width,
                     const glm::vec3& camera_pos,
                     const glm::vec4& color,
                     bool closed,
                     bool  arrow_head = false,
                     bool  arrow_tail = false,
                     float arrow_size = 0.0f,
                     bool  flat       = false);

// =============================================================================
// PolylineRenderer — GPU draw of one or more polylines into the main scene
// pass. Built against the same render pass format the PBR pipeline uses so it
// can run inside `beginRenderPass`/`endRenderPass`.
// =============================================================================
//
// Coordinate-agnostic: all input points are float32 vec3 in whatever space the
// caller wants (typically RTC). The caller is responsible for the geodetic →
// RTC conversion *before* passing points in. Both global (digital earth) and
// local (flat-world) demos use this same renderer; only the points and
// camera vary.

class PolylineRenderer {
 public:
  /// One polyline to draw. `points` is a copy because we batch + expand on
  /// the CPU into a per-frame ribbon VBO.
  struct PolylineDraw {
    std::vector<glm::vec3> points;
    glm::vec4              color    = {1.0f, 1.0f, 1.0f, 1.0f};
    float                  width_m  = 1.0f;   ///< full width (not half)
    bool                   closed   = false;

    /// Arrow at the polyline's tail (last point) pointing along travel dir.
    /// Use for evacuation / patrol routes where direction matters.
    bool                   arrow_head = false;
    /// Arrow at the polyline's head (first point) pointing AWAY from travel
    /// — i.e., both arrows point outward, marking entry + exit clearly.
    bool                   arrow_tail = false;
    /// Edge length of the arrowhead in meters. 0 → 3× `width_m`.
    float                  arrow_size_m = 0.0f;

    /// Render as a dashed pattern instead of a solid ribbon. The fragment
    /// shader discards `dash_gap_ratio` of every `dash_size_m` period along
    /// the arc length. Arrowhead triangles are always drawn solid
    /// (sentinel `arc_length < 0` skips the dash test).
    bool                   dashed         = false;
    float                  dash_size_m    = 1.0f;
    float                  dash_gap_ratio = 0.5f;

    /// Draw without depth-test against the scene. Used for gizmo axes /
    /// selection highlights that must remain visible even when partially
    /// occluded by terrain or models. Routed through a second pipeline
    /// at record() time; otherwise identical geometry.
    bool                   on_top         = false;

    // ---- 线效果(P5 线要素系统)。可叠加:位掩码 bit0=描边 bit1=发光
    //      bit2=图案/流动。`has_fx=true` 时用 `fx_bits`;否则从旧 `line_type`
    //      映射(向后兼容 cloud / 旧调用)。弧线是几何(调用方建好抛物线点)、
    //      虚线见 dashed,均不在位掩码里。----
    bool                   has_fx         = false;
    uint32_t               fx_bits        = 0;
    // 旧单选类型(0普通 1描边 2发光 3贴图 4流动 5飞线),仅 has_fx=false 时用。
    int                    line_type      = 0;
    glm::vec4              outline_color  = {0.05f, 0.05f, 0.08f, 1.0f}; ///< 描边外缘
    float                  glow_power     = 0.45f;  ///< 发光芯宽占比
    float                  flow_speed     = 1.0f;   ///< 流动 m/s(配 time)
    float                  tex_tiling     = 0.2f;   ///< 贴图/流动 每米平铺
    int                    texture_slot   = -1;     ///< 内置图案 atlas 行,-1=无
    /// 自定义贴图槽(ensureCustomPattern 返回值)。>=0 时用任意导入贴图替代
    /// 内置图案 atlas:V 跨线宽采全图、U 沿弧长平铺/流动,RGB×线色。
    int                    custom_pattern_slot = -1;

    /// 横铺(flat):ribbon 在水平面内展开(side = dir × worldUp)而非朝向
    /// 相机。贴地的折线/箭头用它平躺在地面,掠射角下也不会一半埋进地里。
    /// false=朝相机(空中发光线 / 飞线),true=水平躺地(贴地标绘)。
    bool                   flat           = false;
  };

  /// Per-frame parameters.
  struct Params {
    glm::mat4 view_proj;          ///< RTC-relative `proj * view`
    glm::vec3 camera_pos_rtc;     ///< for view-aligned ribbon expansion
    float     time_s = 0.0f;      ///< 流动线 UV 滚动时基(秒)
  };

  bool initialize(RHIDevice* device, RHIRenderPassPtr scene_render_pass);

  /// Record draws into `cmd`. The caller must have already begun the scene
  /// render pass and bound the viewport/scissor. Internally:
  ///   1. Expand all polylines into a single ribbon-vertex buffer (CPU).
  ///   2. Upload to a per-frame VBO via host-visible staging.
  ///   3. Bind the polyline pipeline + push mvp + draw.
  /// No-op when `lines` is empty.
  void record(const RHICommandBufferPtr& cmd, const Params& p,
              const std::vector<PolylineDraw>& lines);

  /// 注册一张自定义线贴图(RGBA8 像素),返回槽号(缓存 by key,重复调用返回
  /// 原槽)。失败返回 -1。PolylineDraw::custom_pattern_slot 用它。
  int ensureCustomPattern(const std::string& key, const uint8_t* rgba,
                          int width, int height);
  int getCustomPatternSlot(const std::string& key) const;

 private:
  // Push constants — view-projection + per-polyline dash params. Color is
  // baked per-vertex during ribbon expansion; dash params change per draw
  // call (one polyline at a time), so they live in push constants where
  // we can rebind cheaply between draws.
  //
  //   dash_params.x = dash_size_m   (period in arc-length units)
  //   dash_params.y = dash_enabled  (1.0 = on, 0.0 = off)
  //   dash_params.z = dash_gap_ratio (fraction of period that's gap, 0..1)
  //   dash_params.w = reserved
  struct PushConstants {
    glm::mat4 view_proj;
    glm::vec4 dash_params;    // x=period y=enabled z=gap_ratio
    glm::vec4 line_params;    // x=line_type y=glow_power z=time w=flow_speed
    glm::vec4 outline_color;  // 描边线外缘色
    glm::vec4 tex_params;     // x=tex_tiling(每米平铺) yzw=reserved(P3)
  };
  static_assert(sizeof(PushConstants) == 128,
                "PolylineRenderer::PushConstants layout mismatch");

  bool createPipeline(RHIRenderPassPtr render_pass);
  /// 程序化图案 atlas(贴图/流动线):256×256,4 行 ×64px —— 行0 箭头流、
  /// 行1 虚块流、行2 圆点流、行3 实心光带。texture_slot 选行,U 沿弧长(流动
  /// 时减 time×flow_speed),V 跨线宽映射到该行子区。零外部资产。
  bool createPatternAtlas();
  /// Uploads into the next pool slot and returns that buffer (null on OOM).
  RHIBufferPtr uploadRibbon(const std::vector<RibbonVertex>& ribbon);

  RHIDevice*           device_ = nullptr;
  RHIShaderPtr         vs_;
  RHIShaderPtr         fs_;
  RHIPipelineLayoutPtr pipeline_layout_;
  RHITexturePtr             pattern_tex_;
  RHITextureViewPtr         pattern_view_;
  RHISamplerPtr             pattern_sampler_;
  RHIDescriptorSetLayoutPtr pattern_set_layout_;
  RHIDescriptorSetPtr       pattern_set_;
  // 自定义线贴图槽(任意导入贴图)。各持有 texture/view/set 保活;
  // 复用 pattern_set_layout_ + pattern_sampler_。index_ 按 key 去重。
  std::vector<RHITexturePtr>      custom_tex_;
  std::vector<RHITextureViewPtr>  custom_view_;
  std::vector<RHIDescriptorSetPtr> custom_set_;
  std::unordered_map<std::string, int> custom_index_;
  RHIPipelinePtr       pipeline_;
  // Identical to pipeline_ except depthTestEnable = false. Built on
  // demand so callers that never use `on_top` don't pay for it.
  RHIPipelinePtr       pipeline_on_top_;

  // Ribbon VBO pool, round-robin per record() call. record() memcpy-s
  // vertices at RECORD time but the command buffer reads them at SUBMIT —
  // a single shared buffer corrupts every earlier record() of the same
  // frame (the editor records scene lines AND gizmo lines separately:
  // scene draw ended up reading gizmo data + stale tail = giant green
  // garbage ribbons at the gizmo origin). One buffer per record() call,
  // pool size covers the max records-per-frame with headroom.
  // 16:同帧最多 2 次 record × 数帧 GPU 延迟余量(无逐帧 fence,槽位过早
  // 复用仍会跨帧别名 —— 池子大到覆盖在飞帧数即可)。
  static constexpr int kRibbonPool = 16;
  RHIBufferPtr         ribbon_vbo_pool_[kRibbonPool];
  uint32_t             ribbon_cap_pool_[kRibbonPool] = {};
  int                  ribbon_pool_next_ = 0;
};

}  // namespace hongtu::renderer
