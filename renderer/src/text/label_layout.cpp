#include <hongtu/renderer/text/label_layout.hpp>

#include <algorithm>
#include <cmath>

namespace hongtu::renderer::text {

  float effectSize(int effect, float font_px_ui) {
    switch (effect) {
      case 1:
        return std::max(1.0f, font_px_ui * 0.06f);
      case 2:
        return std::max(1.5f, font_px_ui * 0.10f);
      case 3:
        return std::max(1.5f, font_px_ui * 0.08f);
      default:
        return 0.0f;
    }
  }

  BadgeLayout layoutBadge(const BadgeStyle& st, glm::vec2 anchor_px, float s, const MeasureFn& measure) {
    using C = BadgeConstants;
    BadgeLayout L;
    const float px_ui = std::clamp(C::kFontPerMeter * st.text_size_m, C::kFontMin, C::kFontMax);
    L.text = st.text;
    L.font_px = px_ui * s;
    L.text_size = measure ? measure(st.text, L.font_px) : glm::vec2(0.0f);
    const float pad = C::kPad * s;
    // 牌子悬于锚点正上方;图标标签额外加高,让竖直引线越过图标 billboard 不重叠。
    const float dy = (st.symbol_label ? C::kSymbolDyBase + C::kSymbolDyPerM * st.symbol_size_m : C::kTextDy) * s;
    const glm::vec2 pc(anchor_px.x, anchor_px.y + dy);
    L.text_origin = pc - L.text_size * 0.5f;
    L.bg_min = L.text_origin - glm::vec2(pad);
    L.bg_max = pc + L.text_size * 0.5f + glm::vec2(pad);
    L.fg = glm::vec4(st.color.r, st.color.g, st.color.b, 1.0f); // 标牌前景忽略 color.a(编辑器现状)
    L.effect = st.effect;
    L.effect_px = effectSize(st.effect, px_ui) * s;
    if (st.leader) { // 竖直引线:锚点 → 底板底边中点 + 锚点小圆
      L.has_leader = true;
      L.leader_from = anchor_px;
      L.leader_to = glm::vec2(anchor_px.x, L.bg_max.y);
      L.leader_width = C::kLeaderWidth * s;
      L.leader_color = glm::vec4(1.0f, 1.0f, 1.0f, 150.0f / 255.0f);
      L.dot_center = anchor_px;
      L.dot_radius = C::kDotRadius * s;
      L.dot_color = L.fg;
    }
    if (st.background) { // 半透明底板 + 白 α60 细边框
      L.has_bg = true;
      L.bg = st.bg_color;
      L.corner_radius = C::kCorner * s;
      L.has_border = true;
      L.border = glm::vec4(1.0f, 1.0f, 1.0f, 60.0f / 255.0f);
      L.border_width = C::kBorderWidth * s;
    }
    return L;
  }

  BadgeLayout layoutScreenText(const ScreenTextStyle& t, uint32_t w, uint32_t h, float s, const MeasureFn& measure) {
    using C = BadgeConstants;
    // 非有限(NaN / ±inf)的位置 / 字号按结构体缺省排版(坏档不出非有限几何;clamp 挡不住 NaN)。
    const ScreenTextStyle kDefault;
    const auto finiteOr = [](float v, float d) {
      return std::isfinite(v) ? v : d;
    };
    const float nx = finiteOr(t.nx, kDefault.nx), ny = finiteOr(t.ny, kDefault.ny);
    BadgeLayout L;
    const float px_ui = std::clamp(finiteOr(t.size_px, kDefault.size_px), C::kScreenMin, C::kScreenMax);
    L.text = t.text;
    L.font_px = px_ui * s;
    L.text_size = measure ? measure(t.text, L.font_px) : glm::vec2(0.0f);
    L.text_origin =
        glm::vec2(nx * static_cast<float>(w) - L.text_size.x * 0.5f, ny * static_cast<float>(h) - L.text_size.y * 0.5f);
    const float pad = C::kPad * s;
    L.bg_min = L.text_origin - glm::vec2(pad);
    L.bg_max = L.text_origin + L.text_size + glm::vec2(pad);
    L.fg = t.color; // 屏幕文字 alpha 生效
    L.effect = t.effect;
    L.effect_px = effectSize(t.effect, px_ui) * s;
    if (t.background) { // 底板圆角,无边框
      L.has_bg = true;
      L.bg = t.bg_color;
      L.corner_radius = C::kCorner * s;
    }
    return L;
  }

  std::optional<glm::vec2> projectAnchor(const glm::dmat4& vp, const glm::dvec3& p, double w, double h) {
    const glm::dvec4 c = vp * glm::dvec4(p, 1.0);
    if (!std::isfinite(c.w) || c.w <= 1e-6)
      return std::nullopt; // 背后 / 非有限
    const glm::dvec3 ndc = glm::dvec3(c) / c.w;
    // 取反写法:NaN / inf 的比较为假,一并落入"屏外丢"。
    if (!(std::abs(ndc.x) <= 1.3) || !(std::abs(ndc.y) <= 1.3))
      return std::nullopt; // 屏外
    // 远裁面外(含 NaN):工程无 GLM_FORCE_DEPTH_ZERO_TO_ONE,glm::perspective 的 NDC z ∈ [-1, 1],远裁面 z = 1。
    // 编辑器 local 视图里按原始 ECEF 落位的 Geo 类标注几何被远裁面裁掉,标牌也一并不画(否则只按方向投影会堆在天上)。
    if (!(ndc.z <= 1.0))
      return std::nullopt;
    return glm::vec2(static_cast<float>((ndc.x * 0.5 + 0.5) * w), static_cast<float>((ndc.y * 0.5 + 0.5) * h));
  }

  bool anchorAboveHorizon(const glm::dvec3& p, const glm::dvec3& eye) {
    // Cesium EllipsoidalOccluder::isScaledSpacePointVisible:把 WGS84 椭球缩成单位球后做视锥式地平线判定。
    constexpr double kA = 6378137.0;
    constexpr double kB = 6356752.314245179;
    auto finite3 = [](const glm::dvec3& v) {
      return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    };
    if (!finite3(p) || !finite3(eye))
      return false; // 非有限输入一律剔除
    const glm::dvec3 c(eye.x / kA, eye.y / kA, eye.z / kB);
    const glm::dvec3 q(p.x / kA, p.y / kA, p.z / kB);
    const glm::dvec3 vt = q - c;
    const double vt2 = glm::dot(vt, vt);
    if (!(vt2 > 1e-24))
      return true;                           // 相机与锚点重合(≈6 µm 内):不做除零,视为可见
    const double vh2 = glm::dot(c, c) - 1.0; // 相机到单位球切点距离的平方
    const double vt_dot_vc = -glm::dot(vt, c);
    const bool occluded = (vh2 < 0.0) ? (vt_dot_vc > 0.0) : (vt_dot_vc > vh2 && vt_dot_vc * vt_dot_vc / vt2 > vh2);
    return !occluded;
  }

  std::vector<BadgeLayout> layoutFrameText(const FrameProjection& fp,
                                           const std::vector<LabelAnchor>& labels,
                                           const std::vector<ScreenTextStyle>& screen_texts,
                                           uint32_t w,
                                           uint32_t h,
                                           float s,
                                           const MeasureFn& measure) {
    std::vector<BadgeLayout> out;
    out.reserve(labels.size() + screen_texts.size());
    for (const auto& l : labels) {
      if (l.style.text.empty())
        continue;
      const glm::dvec3 p(l.pos);
      // geo:绘制帧锚点 → ECEF,与相机 ECEF 做椭球地平线遮挡(参数序 = 锚点, 相机)。
      if (fp.geo && !anchorAboveHorizon(glm::dvec3(fp.frame_to_ecef * glm::dvec4(p, 1.0)), fp.eye_ecef))
        continue;
      const auto a = projectAnchor(fp.view_proj, p, static_cast<double>(w), static_cast<double>(h));
      if (!a)
        continue;
      out.push_back(layoutBadge(l.style, *a, s, measure));
    }
    for (const auto& t : screen_texts) {
      if (t.text.empty())
        continue;
      out.push_back(layoutScreenText(t, w, h, s, measure));
    }
    return out;
  }

} // namespace hongtu::renderer::text
