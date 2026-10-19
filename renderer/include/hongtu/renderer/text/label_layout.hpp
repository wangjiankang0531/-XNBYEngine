#pragma once

// 标牌 / 屏幕文字版式与投影剔除纯函数(标注与文字批)。无 GPU、无 ImGui;编辑器与云端共用。
// 数值规则照搬编辑器旧 ImGui 叠加(master@19a06517 editor_app.cpp:7266-7305 / :7311-7348 / :504-526),
// 常量是 ImGui 单位,排版时乘 pixel_scale(编辑器 = 视口 2× 超采样 → 2;云端 = CRS_TEXT_SCALE,默认 1)。
// 像素坐标:左上原点、y 向下(投影矩阵已含 Vulkan Y 翻转,与帧缓冲行序一致)。

#include <glm/glm.hpp>
#include <hongtu/renderer/text/label_types.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace hongtu::renderer::text {

  /// 量字:文字 + 字号(像素)→ 文字块宽高(像素;高 = 行数 × 字号)。空函数 = 量出 (0,0)。
  using MeasureFn = std::function<glm::vec2(const std::string& text, float px)>;

  /// 编辑器标牌常量(ImGui 单位)。
  struct BadgeConstants {
    static constexpr float kFontPerMeter = 14.0f; ///< font_px = clamp(14·text_size_m, 10, 48)
    static constexpr float kFontMin = 10.0f;
    static constexpr float kFontMax = 48.0f;
    static constexpr float kPad = 5.0f;            ///< 底板外扩
    static constexpr float kTextDy = -30.0f;       ///< Text:标牌中心在锚点上方 30
    static constexpr float kSymbolDyBase = -40.0f; ///< 图标标签:-(40 + symbol_size_m·18)
    static constexpr float kSymbolDyPerM = -18.0f;
    static constexpr float kCorner = 4.0f;      ///< 底板圆角
    static constexpr float kLeaderWidth = 1.5f; ///< 引线粗(白 α150)
    static constexpr float kDotRadius = 3.0f;   ///< 锚点实心圆(前景色)
    static constexpr float kBorderWidth = 1.0f; ///< 底板边框(白 α60)
    static constexpr float kScreenMin = 8.0f;   ///< 屏幕文字 size_px 夹 [8, 200]
    static constexpr float kScreenMax = 200.0f;
  };

  /// 一条要画的文字(标牌或屏幕文字),全部是像素量(已乘 pixel_scale)。TextOverlayPass 按它出图元:
  /// 引线 → 锚点圆 → 底板 → 底板边框 → 文字效果层 → 主字。
  struct BadgeLayout {
    std::string text;
    float font_px = 0.0f;
    glm::vec2 text_origin{0.0f}; ///< 文字块左上
    glm::vec2 text_size{0.0f};
    glm::vec4 fg{1.0f};     ///< 显示空间颜色
    int effect = 0;         ///< 0 无 1 描边 2 发光 3 阴影
    float effect_px = 0.0f; ///< 描边宽 / 发光半径 / 阴影偏移
    bool has_bg = false;
    glm::vec2 bg_min{0.0f}, bg_max{0.0f};
    glm::vec4 bg{0.0f};
    float corner_radius = 0.0f;
    bool has_border = false;
    glm::vec4 border{0.0f};
    float border_width = 0.0f;
    bool has_leader = false;
    glm::vec2 leader_from{0.0f}, leader_to{0.0f};
    float leader_width = 0.0f;
    glm::vec4 leader_color{0.0f};
    glm::vec2 dot_center{0.0f};
    float dot_radius = 0.0f; ///< > 0 才画
    glm::vec4 dot_color{0.0f};
  };

  /// 效果尺寸(ImGui 单位,未乘 pixel_scale):1 描边 max(1, px·0.06);2 发光 max(1.5, px·0.10);
  /// 3 阴影偏移 max(1.5, px·0.08);其它 0。
  float effectSize(int effect, float font_px_ui);

  BadgeLayout layoutBadge(const BadgeStyle& style, glm::vec2 anchor_px, float pixel_scale, const MeasureFn& measure);
  /// 屏幕文字:nx / ny / size_px 非有限 → 按 ScreenTextStyle 缺省值排版。
  BadgeLayout layoutScreenText(const ScreenTextStyle& item,
                               uint32_t viewport_w,
                               uint32_t viewport_h,
                               float pixel_scale,
                               const MeasureFn& measure);

  /// 锚点投屏(照搬编辑器 local 剔除):clip.w ≤ 1e-6 → 背后丢;NDC |x| 或 |y| > 1.3 → 屏外丢;
  /// NDC z > 1(远裁面外,GL 口径 z ∈ [-1, 1])→ 丢。非有限(NaN / inf)的 clip.w 或 NDC 一律丢。
  std::optional<glm::vec2> projectAnchor(const glm::dmat4& view_proj, const glm::dvec3& p, double w, double h);

  /// 椭球地平线遮挡(可见返回 true;计划裁定 2):WGS84 椭球缩成单位球后的 EllipsoidalOccluder 判定
  /// (同 Cesium isScaledSpacePointVisible)。相机比锚点低、锚点在近处山头等情形照常可见;南北半球对称。
  /// 参数序 = (锚点 ECEF, 相机 ECEF)。相机与锚点重合 → 可见;任一输入非有限 → 不可见(剔除)。
  bool anchorAboveHorizon(const glm::dvec3& p_ecef, const glm::dvec3& eye_ecef);

  /// 一帧文字的投影上下文。view_proj = 与标注几何同一绘制帧的投影(编辑器 local proj·view / geo RTC vp_anno;
  /// 云端场景系 proj·view);geo 时 frame_to_ecef 把绘制帧点换成 ECEF(编辑器 T(rtc 原点);云端 scene_base)。
  struct FrameProjection {
    glm::dmat4 view_proj{1.0};
    bool geo = false;
    glm::dmat4 frame_to_ecef{1.0};
    glm::dvec3 eye_ecef{0.0};
  };

  /// 一帧:标牌(空文字 / 背后 / 屏外 / geo 地平线外 剔除)按输入序,然后屏幕文字(空文字跳过)按输入序。
  /// 本函数不看 visible / 图层:调用方须先过滤隐藏的标注与屏幕文字,再传入 labels / screen_texts。
  std::vector<BadgeLayout> layoutFrameText(const FrameProjection& fp,
                                           const std::vector<LabelAnchor>& labels,
                                           const std::vector<ScreenTextStyle>& screen_texts,
                                           uint32_t w,
                                           uint32_t h,
                                           float pixel_scale,
                                           const MeasureFn& measure);

} // namespace hongtu::renderer::text
