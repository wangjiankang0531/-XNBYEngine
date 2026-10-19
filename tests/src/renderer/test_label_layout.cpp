// 标牌 / 屏幕文字版式与投影剔除纯函数(renderer/text/label_layout)。数值逐个对照编辑器旧 ImGui 叠加
// (editor_app.cpp:7266-7305 标牌、:7311-7348 屏幕文字、:504-526 效果尺寸,master@19a06517),所有像素量 × pixel_scale。
// Review Focus #4:锚点在相机背后 / 地球背面不画。
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/renderer/text/label_layout.hpp>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

using namespace hongtu::renderer::text;
using hongtu::coordinate::Geodetic;
using hongtu::coordinate::geodeticToECEF;

namespace {

  // 假量字:每个字节宽 0.5·px,高 = px(只为数值可算)。
  glm::vec2 fakeMeasure(const std::string& t, float px) {
    return glm::vec2(0.5f * px * static_cast<float>(t.size()), px);
  }

  // clip.w = p.z 的"透视"矩阵:x、y 原样,w = z(z ≤ 1e-6 即相机背后)。
  glm::dmat4 wFromZ() {
    glm::dmat4 m(0.0);
    m[0][0] = 1.0;
    m[1][1] = 1.0;
    m[2][3] = 1.0; // w = z
    return m;
  }

} // namespace

TEST(LabelLayout, EffectSizesMatchEditor) {
  EXPECT_FLOAT_EQ(effectSize(1, 10.0f), 1.0f);  // max(1, 0.6)
  EXPECT_FLOAT_EQ(effectSize(1, 48.0f), 2.88f); // 48·0.06
  EXPECT_FLOAT_EQ(effectSize(2, 10.0f), 1.5f);  // max(1.5, 1.0)
  EXPECT_FLOAT_EQ(effectSize(2, 48.0f), 4.8f);
  EXPECT_FLOAT_EQ(effectSize(3, 28.0f), 2.24f); // 28·0.08
  EXPECT_FLOAT_EQ(effectSize(3, 10.0f), 1.5f);
  EXPECT_FLOAT_EQ(effectSize(0, 30.0f), 0.0f);
  EXPECT_FLOAT_EQ(effectSize(7, 30.0f), 0.0f); // 其它取值 = 无效果
}

TEST(LabelLayout, TextBadgeNumbersTimesPixelScale) {
  BadgeStyle st;
  st.text = "ABCD";
  st.text_size_m = 2.0f; // px_ui = 28
  st.color = {0.2f, 0.4f, 0.6f, 0.3f};
  st.effect = 1;
  const float s = 2.0f;
  const BadgeLayout L = layoutBadge(st, {500.0f, 400.0f}, s, fakeMeasure);
  EXPECT_FLOAT_EQ(L.font_px, 56.0f);
  EXPECT_EQ(L.text_size, glm::vec2(112.0f, 56.0f));
  // 标牌中心 = 锚点上方 30·s;文字块居中;底板外扩 pad 5·s
  EXPECT_EQ(L.text_origin, glm::vec2(500.0f - 56.0f, 340.0f - 28.0f));
  EXPECT_EQ(L.bg_min, glm::vec2(500.0f - 56.0f - 10.0f, 340.0f - 28.0f - 10.0f));
  EXPECT_EQ(L.bg_max, glm::vec2(500.0f + 56.0f + 10.0f, 340.0f + 28.0f + 10.0f));
  EXPECT_EQ(L.fg, glm::vec4(0.2f, 0.4f, 0.6f, 1.0f)); // 标牌前景忽略 color.a
  EXPECT_EQ(L.effect, 1);
  EXPECT_FLOAT_EQ(L.effect_px, effectSize(1, 28.0f) * 2.0f);
  EXPECT_TRUE(L.has_leader);
  EXPECT_EQ(L.leader_from, glm::vec2(500.0f, 400.0f));
  EXPECT_EQ(L.leader_to, glm::vec2(500.0f, L.bg_max.y)); // 竖直到底板底边
  EXPECT_FLOAT_EQ(L.leader_width, 3.0f);
  EXPECT_EQ(L.leader_color, glm::vec4(1.0f, 1.0f, 1.0f, 150.0f / 255.0f));
  EXPECT_EQ(L.dot_center, glm::vec2(500.0f, 400.0f));
  EXPECT_FLOAT_EQ(L.dot_radius, 6.0f);
  EXPECT_EQ(L.dot_color, L.fg);
  EXPECT_TRUE(L.has_bg);
  EXPECT_EQ(L.bg, glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));
  EXPECT_FLOAT_EQ(L.corner_radius, 8.0f);
  EXPECT_TRUE(L.has_border);
  EXPECT_EQ(L.border, glm::vec4(1.0f, 1.0f, 1.0f, 60.0f / 255.0f));
  EXPECT_FLOAT_EQ(L.border_width, 2.0f);
}

TEST(LabelLayout, FontSizeClampAndSymbolLabelOffset) {
  BadgeStyle st;
  st.text = "x";
  st.text_size_m = 0.1f;
  EXPECT_FLOAT_EQ(layoutBadge(st, {0, 0}, 1.0f, fakeMeasure).font_px, 10.0f);
  st.text_size_m = 10.0f;
  EXPECT_FLOAT_EQ(layoutBadge(st, {0, 0}, 1.0f, fakeMeasure).font_px, 48.0f);
  st.text_size_m = 1.0f; // px 14
  st.symbol_label = true;
  st.symbol_size_m = 6.0f; // dy = -(40 + 6·18) = -148
  const BadgeLayout L = layoutBadge(st, {100.0f, 400.0f}, 1.0f, fakeMeasure);
  EXPECT_FLOAT_EQ((L.bg_min.y + L.bg_max.y) * 0.5f, 400.0f - 148.0f);
}

TEST(LabelLayout, BackgroundAndLeaderSwitches) {
  BadgeStyle st;
  st.text = "x";
  st.background = false;
  st.leader = false;
  const BadgeLayout L = layoutBadge(st, {0, 0}, 1.0f, fakeMeasure);
  EXPECT_FALSE(L.has_bg);
  EXPECT_FALSE(L.has_border);
  EXPECT_FALSE(L.has_leader);
  EXPECT_FLOAT_EQ(L.dot_radius, 0.0f);
}

TEST(LabelLayout, ScreenTextCenteredNormalizedNoBorderAlphaKept) {
  ScreenTextStyle t;
  t.text = "HUD";
  t.nx = 0.5f;
  t.ny = 0.06f;
  t.size_px = 32.0f;
  t.color = {1.0f, 0.5f, 0.25f, 0.5f};
  t.background = true;
  t.effect = 3;
  const BadgeLayout L = layoutScreenText(t, 2560, 1440, 2.0f, fakeMeasure);
  EXPECT_FLOAT_EQ(L.font_px, 64.0f);
  EXPECT_EQ(L.text_size, glm::vec2(96.0f, 64.0f));
  EXPECT_FLOAT_EQ(L.text_origin.x, 1280.0f - 48.0f);
  EXPECT_FLOAT_EQ(L.text_origin.y, 0.06f * 1440.0f - 32.0f);
  EXPECT_EQ(L.bg_min, L.text_origin - glm::vec2(10.0f));
  EXPECT_EQ(L.bg_max, L.text_origin + L.text_size + glm::vec2(10.0f));
  EXPECT_TRUE(L.has_bg);
  EXPECT_FALSE(L.has_border); // 屏幕文字底板无边框
  EXPECT_FALSE(L.has_leader);
  EXPECT_EQ(L.fg, t.color); // alpha 生效
  EXPECT_FLOAT_EQ(L.effect_px, effectSize(3, 32.0f) * 2.0f);
  t.size_px = 3.0f;
  EXPECT_FLOAT_EQ(layoutScreenText(t, 100, 100, 1.0f, fakeMeasure).font_px, 8.0f);
  t.size_px = 500.0f;
  EXPECT_FLOAT_EQ(layoutScreenText(t, 100, 100, 1.0f, fakeMeasure).font_px, 200.0f);
}

TEST(LabelLayout, ProjectAnchorRejectsBehindCameraAndOffscreen) {
  const glm::dmat4 m = wFromZ();
  const auto a = projectAnchor(m, {0.0, 0.0, 1.0}, 200.0, 100.0);
  ASSERT_TRUE(a.has_value());
  EXPECT_EQ(*a, glm::vec2(100.0f, 50.0f));
  EXPECT_FALSE(projectAnchor(m, {0.0, 0.0, -1.0}, 200.0, 100.0).has_value()); // 背后
  EXPECT_FALSE(projectAnchor(m, {0.0, 0.0, 1e-7}, 200.0, 100.0).has_value()); // w ≤ 1e-6
  EXPECT_FALSE(projectAnchor(m, {1.31, 0.0, 1.0}, 200.0, 100.0).has_value()); // NDC x > 1.3
  EXPECT_FALSE(projectAnchor(m, {0.0, -1.31, 1.0}, 200.0, 100.0).has_value());
  EXPECT_TRUE(projectAnchor(m, {1.29, -1.29, 1.0}, 200.0, 100.0).has_value()); // 屏外 30% 内仍画(编辑器口径)
}

TEST(LabelLayout, GeoAnchorBeyondHorizonIsCulled) {
  const double R = 6378137.0;
  const glm::dvec3 eye(R + 300.0, 0.0, 0.0);
  EXPECT_TRUE(anchorAboveHorizon({R, 10.0, 0.0}, eye)); // 脚下
  // 地表点 R·(cosθ, sinθ):可见 ⇔ cosθ > R/(R+300) ⇔ 弧长 < ~62 km。
  const double t50 = 50000.0 / R, t80 = 80000.0 / R;
  EXPECT_TRUE(anchorAboveHorizon({R * std::cos(t50), R * std::sin(t50), 0.0}, eye));  // 50 km:地平线内
  EXPECT_FALSE(anchorAboveHorizon({R * std::cos(t80), R * std::sin(t80), 0.0}, eye)); // 80 km:地平线下
  const double a = glm::radians(10.0);
  EXPECT_FALSE(anchorAboveHorizon({R * std::cos(a), R * std::sin(a), 0.0}, eye)); // 1100 km 外:地球背面
  EXPECT_FALSE(anchorAboveHorizon({-R, 0.0, 0.0}, eye));                          // 对跖点
}

TEST(LabelLayout, FrameTextCullsBehindAndBeyondHorizon) {
  BadgeStyle st;
  st.text = "A";
  std::vector<LabelAnchor> labels = {{{0.0f, 0.0f, 1.0f}, st},            // 可见
                                     {{0.0f, 0.0f, -1.0f}, st},           // 背后
                                     {{0.0f, 0.0f, 1.0f}, BadgeStyle{}}}; // 空文字跳过
  ScreenTextStyle hud;
  hud.text = "H";
  FrameProjection fp;
  fp.view_proj = wFromZ();
  auto out = layoutFrameText(fp, labels, {hud, ScreenTextStyle{}}, 200, 100, 1.0f, fakeMeasure);
  ASSERT_EQ(out.size(), 2u); // 1 个标牌 + 1 条屏幕文字(空文字的屏幕文字跳过)
  EXPECT_EQ(out[0].text, "A");
  EXPECT_TRUE(out[0].has_leader);
  EXPECT_EQ(out[1].text, "H"); // 屏幕文字排在标牌之后(画在上面,同编辑器绘制顺序)
  // geo:绘制帧 → ECEF 平移到地球另一面 → 地平线剔除,即使投影在屏内
  fp.geo = true;
  fp.eye_ecef = glm::dvec3(6378437.0, 0.0, 0.0);
  fp.frame_to_ecef = glm::dmat4(1.0);
  fp.frame_to_ecef[3] = glm::dvec4(-6378137.0, 0.0, 0.0, 1.0);
  out = layoutFrameText(fp, {labels[0]}, {}, 200, 100, 1.0f, fakeMeasure);
  EXPECT_TRUE(out.empty());
  fp.frame_to_ecef[3] = glm::dvec4(6378137.0, 0.0, 0.0, 1.0); // 同一点在脚下 → 画
  out = layoutFrameText(fp, {labels[0]}, {}, 200, 100, 1.0f, fakeMeasure);
  EXPECT_EQ(out.size(), 1u);
}

// ---- 椭球地平线遮挡(计划裁定 2 的落地:Cesium EllipsoidalOccluder 口径)----

TEST(LabelLayout, GeoAnchorHigherThanEyeNearbyIsVisible) {
  // 低空相机看略高的近处锚点:切平面 + 地心法线的旧判据会把它误剔,椭球遮挡不会。
  const double lon = glm::radians(100.0);
  const glm::dvec3 east(-std::sin(lon), std::cos(lon), 0.0);
  const glm::dvec3 eye = geodeticToECEF(Geodetic::fromDegrees(30.0, 100.0, 5.0));
  const glm::dvec3 p1 = geodeticToECEF(Geodetic::fromDegrees(30.0, 100.0, 30.0)) + 200.0 * east; // 高 30 m,东 200 m
  EXPECT_TRUE(anchorAboveHorizon(p1, eye));
  const glm::dvec3 eye2 = geodeticToECEF(Geodetic::fromDegrees(30.0, 100.0, 100.0));
  const glm::dvec3 p2 =
      geodeticToECEF(Geodetic::fromDegrees(30.0, 100.0, 150.0)) + 1000.0 * east; // 山头 POI 150 m,东 1 km
  EXPECT_TRUE(anchorAboveHorizon(p2, eye2));
}

TEST(LabelLayout, GeoHorizonSymmetricAtMidLatitudes) {
  // 相机 300 m 高:真实地平线 ≈ 62 km。南北半球对称:50 km 内可见,100 km 外剔除。
  const double kMeridianRadiusM = 6367000.0; // 45° 纬度处子午圈曲率半径(近似即可,离 62 km 门槛很远)
  for (const double lat : {45.0, -45.0}) {
    const glm::dvec3 eye = geodeticToECEF(Geodetic::fromDegrees(lat, 10.0, 300.0));
    auto ground = [&](double north_m) {
      return geodeticToECEF(Geodetic::fromDegrees(lat + glm::degrees(north_m / kMeridianRadiusM), 10.0, 0.0));
    };
    EXPECT_TRUE(anchorAboveHorizon(ground(50000.0), eye)) << "lat " << lat << " 北 50 km";
    EXPECT_TRUE(anchorAboveHorizon(ground(-50000.0), eye)) << "lat " << lat << " 南 50 km";
    EXPECT_FALSE(anchorAboveHorizon(ground(100000.0), eye)) << "lat " << lat << " 北 100 km";
    EXPECT_FALSE(anchorAboveHorizon(ground(-100000.0), eye)) << "lat " << lat << " 南 100 km";
  }
}

TEST(LabelLayout, GeoAntipodeIsCulled) {
  const glm::dvec3 eye = geodeticToECEF(Geodetic::fromDegrees(30.0, 100.0, 300.0));
  EXPECT_FALSE(anchorAboveHorizon(geodeticToECEF(Geodetic::fromDegrees(-30.0, -80.0, 0.0)), eye)); // 对跖点
  EXPECT_FALSE(anchorAboveHorizon(geodeticToECEF(Geodetic::fromDegrees(-30.0, -80.0, 100.0)), eye));
}

TEST(LabelLayout, GeoEyeAtAnchorIsVisible) {
  const glm::dvec3 p = geodeticToECEF(Geodetic::fromDegrees(30.0, 100.0, 10.0));
  EXPECT_TRUE(anchorAboveHorizon(p, p)); // 相机与锚点重合:不做除零,视为可见
}

TEST(LabelLayout, NonFiniteInputsAreCulled) {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  const glm::dmat4 m = wFromZ();
  EXPECT_FALSE(projectAnchor(m, {nan, 0.0, 1.0}, 200.0, 100.0).has_value());
  EXPECT_FALSE(projectAnchor(m, {0.0, nan, 1.0}, 200.0, 100.0).has_value());
  EXPECT_FALSE(projectAnchor(m, {0.0, 0.0, nan}, 200.0, 100.0).has_value());
  EXPECT_FALSE(projectAnchor(m, {inf, 0.0, 1.0}, 200.0, 100.0).has_value());
  glm::dmat4 bad = m;
  bad[2][3] = nan; // clip.w = NaN
  EXPECT_FALSE(projectAnchor(bad, {0.0, 0.0, 1.0}, 200.0, 100.0).has_value());
  const glm::dvec3 eye = geodeticToECEF(Geodetic::fromDegrees(30.0, 100.0, 300.0));
  const glm::dvec3 ok = geodeticToECEF(Geodetic::fromDegrees(30.0, 100.0, 0.0));
  EXPECT_FALSE(anchorAboveHorizon({nan, 0.0, 0.0}, eye));
  EXPECT_FALSE(anchorAboveHorizon({inf, 0.0, 0.0}, eye));
  EXPECT_FALSE(anchorAboveHorizon(ok, {nan, 0.0, 0.0}));
  EXPECT_TRUE(anchorAboveHorizon(ok, eye)); // 对照:有限输入照常可见
}

// 终审修复波 M3:锚点在远裁面之外(编辑器 local 视图里按原始 ECEF 落位的 Geo 类标注)不画 —— 否则只按方向投影,
// 会在天上堆成一团。工程无 GLM_FORCE_DEPTH_ZERO_TO_ONE:glm::perspective 的 NDC z ∈ [-1, 1],远裁面 z = 1。
TEST(LabelLayout, ProjectAnchorRejectsBeyondFarPlane) {
  const glm::dmat4 proj = glm::perspective(glm::radians(45.0), 16.0 / 9.0, 0.1, 1000.0);
  const glm::dmat4 view = glm::lookAt(glm::dvec3(0.0), glm::dvec3(0.0, 1.0, 0.0), glm::dvec3(0.0, 0.0, 1.0));
  const glm::dmat4 vp = proj * view;
  EXPECT_TRUE(projectAnchor(vp, {0.0, 999.0, 0.0}, 1600.0, 900.0).has_value());   // 远裁面内
  EXPECT_TRUE(projectAnchor(vp, {0.0, 0.05, 0.0}, 1600.0, 900.0).has_value());    // 近裁面前(z < -1)照常画
  EXPECT_FALSE(projectAnchor(vp, {0.0, 1001.0, 0.0}, 1600.0, 900.0).has_value()); // 远裁面外
  EXPECT_FALSE(projectAnchor(vp, {0.0, 6.4e6, 0.0}, 1600.0, 900.0).has_value());  // ECEF 量级
  glm::dmat4 bad = vp;
  bad[1][2] = std::numeric_limits<double>::quiet_NaN(); // clip.z = NaN(x、y、w 有限)
  EXPECT_FALSE(projectAnchor(bad, {0.0, 999.0, 0.0}, 1600.0, 900.0).has_value());
}

// 终审修复波(T7 遗留):屏幕文字 nx / ny / size_px 非有限 → 按缺省值(0.5 / 0.08 / 28)排版,输出全有限。
TEST(LabelLayout, ScreenTextNonFiniteParamsFallBackToDefaults) {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  ScreenTextStyle ref;
  ref.text = "HUD";
  const BadgeLayout R = layoutScreenText(ref, 800, 600, 2.0f, fakeMeasure);
  for (int field = 0; field < 3; ++field) {
    for (float bad : {nan, inf, -inf}) {
      ScreenTextStyle t = ref;
      (field == 0 ? t.nx : field == 1 ? t.ny : t.size_px) = bad;
      const BadgeLayout L = layoutScreenText(t, 800, 600, 2.0f, fakeMeasure);
      EXPECT_EQ(L.text_origin, R.text_origin) << field << " " << bad;
      EXPECT_FLOAT_EQ(L.font_px, R.font_px) << field << " " << bad;
      EXPECT_EQ(L.bg_min, R.bg_min) << field << " " << bad;
      EXPECT_EQ(L.bg_max, R.bg_max) << field << " " << bad;
    }
  }
}
