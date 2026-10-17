#pragma once

#include <hongtu/coordinate/rtc_transform.hpp>

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace hongtu::scene {

// =============================================================================
// Enums
// =============================================================================

enum class AnnotationKind {
  Polyline,
  Polygon,
  Symbol,
  Text,
  Arrow,
};

enum class AnnotationMode {
  Geo,    ///< vertices stored as Geodetic (lat/lon/alt)
  Local,  ///< vertices stored as flat-world meters (dvec3)
};

/// 折线外观类型(下拉主选项)。虚线/箭头/圆滑是与之正交的修饰,不在此列。
enum class LineType : uint8_t {
  Normal   = 0,  ///< 纯色实线(配 dash/arrow/圆滑)
  Outline  = 1,  ///< 描边线:芯色 + 外描边色
  Glow     = 2,  ///< 发光线:中间亮、边缘柔(Cesium Glow)
  Textured = 3,  ///< 贴图线:沿弧长铺纹理条
  Flow     = 4,  ///< 流动线:贴图 + 时间驱动 UV 滚动(能量/数据流)
  Arc      = 5,  ///< 飞线:相邻点间抛物弧 + 可选流动光点(OD/连接)
};

/// 区域填充效果(智慧城市/园区):纯色 / 扫描·雷达 / 网格·科技格 / 脉冲·呼吸。
enum class FillType : uint8_t {
  Solid = 0,
  Scan  = 1,
  Grid  = 2,
  Pulse = 3,
};

/// 贴合方式(与外观正交):悬浮=深度测试、顶层=关深度恒可见、贴地=贴表面。
/// 真·DEM 跟随起伏待 DEM 接入;当前"贴地"= 贴面 + 深度处理。
enum class LinePlacement : uint8_t {
  Float  = 0,
  OnTop  = 1,
  Ground = 2,
};

// =============================================================================
// Style — shared by all annotation kinds. Fields are selectively used per kind;
// reading an unused field is harmless (default value).
// =============================================================================

struct AnnotationStyle {
  glm::vec4   color           { 1.0f, 1.0f, 0.0f, 1.0f };
  float       stroke_width_m  = 0.2f;
  float       symbol_size_m   = 1.0f;
  uint32_t    symbol_index    = 0;       ///< billboard atlas slot(内置 16 图标)
  /// 自定义图标:资产库里的贴图(相对路径)。非空时覆盖 `symbol_index`,用整张
  /// 贴图当图标(概念上等同贴图,编辑器经 ensureBillboardTexture 上传)。
  std::string symbol_texture_id;
  /// Symbol only:POI 特效。0=静态 1=脉冲 2=跳动 3=扩散波涟漪。`symbol_freq`
  /// = 动画频率(次/秒)。
  int         symbol_effect   = 0;
  float       symbol_freq     = 1.0f;
  bool        filled          = false;   ///< polygon only
  /// Polygon only:填充效果。Solid 走常规填充;Scan/Grid/Pulse 走 unlit 发光
  /// 叠加(智慧城市/园区面效果)。`fill_param` = 扫描转速/网格密度/脉冲频率。
  FillType    fill_type       = FillType::Solid;
  float       fill_param      = 1.0f;
  /// Polygon only:电子围栏 / 光墙 —— 沿边界向上拉起发光墙(数字孪生园区
  /// 招牌)。`wall_height` 墙高(米),`wall_flow` 竖向流动速度(0=静态)。
  bool        wall            = false;
  float       wall_height     = 8.0f;
  float       wall_flow       = 0.0f;
  /// Polygon 围栏:流动方向。false=横向(沿边界周长滚动,默认),
  /// true=竖向(自底向上滚动)。
  bool        wall_flow_vertical = false;
  /// Polygon only, geo mode: render this polygon as a local water body
  /// (lake / flood zone) via GlobeRenderer::addWaterBody. The polygon's
  /// per-vertex alt is the water level. Replaces the flat `filled` fill.
  bool        water           = false;
  /// Polygon only: Chaikin corner-rounding iterations [0..5] applied at
  /// RENDER time (water surface / fill / outline). Control vertices stay
  /// untouched, so editing keeps operating on the original corners. Each
  /// iteration doubles the boundary vertex count and rounds harder.
  int         corner_smooth   = 0;
  float       dash_period_m   = 0.0f;    ///< 0 = solid line
  /// Polygon only:此多边形是「圆形区域」—— 顶点是按 `circle_radius_m` 在
  /// 中心(当前顶点的形心)周围生成的环。编辑器据此显示半径滑杆并重建环;
  /// 渲染/填充/围栏全部复用多边形管线。
  bool        circle          = false;
  float       circle_radius_m = 5.0f;

  // ---- 折线扩展(P5 线要素系统)。Polyline/Arrow 用;其余 kind 忽略 ----
  // 线效果【可叠加】(替代旧的单选 line_type):描边/发光/图案流动 各自独立
  // 开关,任意组合;弧线是几何(抬成抛物弧),也作独立开关。dash 见下方
  // dash_period_m。line_type 仅作旧工程迁移用(load 时映射到下列开关)。
  bool          fx_outline     = false;  ///< 描边(outline_color)
  bool          fx_glow        = false;  ///< 发光(glow_power 芯宽)
  bool          fx_pattern     = false;  ///< 图案/流动(line_texture + tiling + flow_speed,0=静态)
  bool          fx_arc         = false;  ///< 弧线/飞线(arc_height_ratio,几何)
  LineType      line_type      = LineType::Normal;  ///< 仅迁移用,新代码看 fx_*
  LinePlacement line_placement = LinePlacement::Float;
  bool          arrow_head     = false;  ///< 折线末端箭头(方向指示;箭头工具默认开)
  bool          arrow_tail     = false;  ///< 折线首端箭头
  glm::vec4     outline_color  { 0.05f, 0.05f, 0.08f, 1.0f }; ///< 描边线外缘色
  float         glow_power     = 0.45f;  ///< 发光线:芯宽占比(越小芯越细)
  float         flow_speed     = 1.0f;   ///< 流动线:沿线滚动 m/s(负=反向)
  float         line_tex_tiling = 0.2f;  ///< 贴图/流动:每米平铺数(纹理密度)
  float         arc_height_ratio = 0.3f; ///< 飞线:弧高 = 两点跨度 × 此比
  std::string   line_texture;            ///< 图案流动:内置图案名(arrow/dash/dots/band)
  std::string   line_texture_asset;      ///< 图案流动:自定义贴图资产 id(非空则替代内置图案)

  // text-specific
  std::string text;
  float       text_size_m     = 1.0f;
  /// Text 标牌(编辑器 ImGui 投影叠加):半透明底板 + 到锚点引线。
  bool        text_background = true;
  bool        text_leader     = true;
  glm::vec4   text_bg_color   { 0.0f, 0.0f, 0.0f, 0.55f };
  /// 屏幕文字效果(编辑器叠加层):0=无 1=描边 2=发光 3=阴影。Text 与
  /// Symbol 标签共用。
  int         text_effect     = 0;
  /// Symbol only:在图标下方显示 `text` 文字标签(复用文字标牌样式)。
  bool        symbol_label    = false;
};

// =============================================================================
// Render payload — the renderer-facing view of an annotation. Already lowered
// to RTC float32 against the supplied origin; PolylineRenderer / PolygonRenderer
// / BillboardRenderer consume this directly.
// =============================================================================

struct AnnotationRenderPayload {
  AnnotationKind          kind;
  std::vector<glm::vec3>  rtc_vertices;
  AnnotationStyle         style;
  uint64_t                id      = 0;
  bool                    visible = true;
};

// =============================================================================
// Annotation — abstract base
// =============================================================================

/// Abstract drawing annotation. Two parallel hierarchies hang off this:
///
///   Annotation
///     ↑
///   {Polyline,Polygon,Symbol,Text,Arrow}Annotation   ← per-kind base
///     ↑                                ↑
///   Geo*Annotation                    Local*Annotation   [DUAL]
///
/// Subclass contract:
///   - getKind / getMode : trivially identify
///   - toRenderPayload   : lower vertices to RTC + emit payload
///   - toJson / fromJson : compact custom schema (see §5 of design doc)
///
/// Style + visibility + id live on the base; subclasses only worry about
/// coordinate storage.
class Annotation {
 public:
  virtual ~Annotation() = default;

  Annotation(const Annotation&)            = delete;
  Annotation& operator=(const Annotation&) = delete;
  Annotation(Annotation&&) noexcept            = default;
  Annotation& operator=(Annotation&&) noexcept = default;

  virtual AnnotationKind getKind() const = 0;
  virtual AnnotationMode getMode() const = 0;

  /// Lower the annotation's vertices into RTC float32 against `origin` and
  /// return a render-ready payload. Derived classes implement the
  /// coordinate-specific lowering.
  virtual AnnotationRenderPayload
      toRenderPayload(const coordinate::PrecisionOrigin& origin) const = 0;

  /// Compact JSON form. See `docs/superpowers/specs/2026-05-20-annotation-timeline-design.md` §5.
  virtual nlohmann::json toJson() const = 0;
  virtual void           fromJson(const nlohmann::json& j) = 0;

  // ---------------------------------------------------------------------------
  // Common state (id / visibility / style) — lives here so AnnotationManager
  // can read them uniformly across kinds.
  // ---------------------------------------------------------------------------

  uint64_t  id() const                 { return id_; }
  void      setId(uint64_t i)          { id_ = i; }
  bool      visible() const            { return visible_; }
  void      setVisible(bool v)         { visible_ = v; }
  const AnnotationStyle& style() const { return style_; }
  AnnotationStyle&       mutableStyle() { return style_; }
  void      setStyle(const AnnotationStyle& s) { style_ = s; }

  /// Editor-side layer assignment. Renderer-agnostic — the editor consults
  /// its LayerManager to decide visible / locked status per layer_id. 0
  /// means "no assignment" (treated as always visible / unlocked). Default
  /// is 2 = the "标注" layer created at LayerManager::resetToDefaults.
  uint32_t  layerId() const            { return layer_id_; }
  void      setLayerId(uint32_t lid)   { layer_id_ = lid; }

 protected:
  Annotation() = default;

  uint64_t        id_       = 0;
  bool            visible_  = true;
  uint32_t        layer_id_ = 2;
  AnnotationStyle style_;
};

}  // namespace hongtu::scene
