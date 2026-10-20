// lane ⑫ T2:geo 运动插值(renderer/motion/geo_motion.hpp)与相机跟随数学(renderer/motion/follow_camera.hpp)。
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include <hongtu/coordinate/enu_frame.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/renderer/motion/follow_camera.hpp>
#include <hongtu/renderer/motion/geo_motion.hpp>

#include <cmath>

using namespace hongtu::renderer::motion;
namespace c = hongtu::coordinate;

namespace {

  constexpr double kDt = 1.0 / 30.0;

  struct SceneFrame {
    glm::dmat4 base{1.0}, inv{1.0};
    SceneFrame(double lat, double lon, double alt, double heading) {
      // Same T(ECEF) * ENU * Rz convention as the upstream anchorBase fixture.
      const auto frame = c::enuFrameAt(c::Geodetic::fromDegrees(lat, lon, alt));
      base[0] = glm::dvec4(frame.east, 0.0);
      base[1] = glm::dvec4(frame.north, 0.0);
      base[2] = glm::dvec4(frame.up, 0.0);
      base[3] = glm::dvec4(frame.origin_ecef, 1.0);
      base = glm::rotate(base, glm::radians(heading), glm::dvec3(0, 0, 1));
      inv = glm::inverse(base);
    }
    double altOf(const glm::dvec3& p) const {
      return c::ecefToGeodetic(glm::dvec3(base * glm::dvec4(p, 1.0))).alt;
    }
  };

  FollowTarget tgt(double x, double y, double z, double fwd, bool has = true) {
    FollowTarget t;
    t.pos = {x, y, z};
    t.forward_compass_deg = fwd;
    t.has_forward = has;
    return t;
  }

} // namespace

TEST(GeoMotion, SmallStepMatchesLinear) {
  const SceneFrame f(22.6, 114.0, 5.0, 30.0);
  const glm::dvec3 a(10.0, -3.0, 1.5), b(16.0, 5.0, 2.5);
  for (double s : {0.0, 0.25, 0.5, 0.9, 1.0}) {
    const glm::dvec3 g = geoLerpScene(f.base, f.inv, a, b, s, false, false, nullptr);
    const glm::dvec3 l = a + (b - a) * s;
    EXPECT_LT(glm::length(g - l), 1e-4) << "s=" << s;
  }
}

// 红证类:长段线性弦会穿地(50 km 弦中点比椭球面低约 49 m);geo 插值按两端椭球高插值高度。
TEST(GeoMotion, LongSegmentFollowsEllipsoidHeight) {
  const SceneFrame f(22.6, 114.0, 0.0, 0.0);
  // 两端都在椭球高 100 m:东西各 25 km。
  const auto onAlt = [&](double east_m) {
    const c::EnuFrame e = c::enuFrameAt(c::Geodetic::fromDegrees(22.6, 114.0, 0.0));
    glm::dvec3 ecef = e.localToEcef({east_m, 0.0, 0.0});
    c::Geodetic g = c::ecefToGeodetic(ecef);
    g.alt = 100.0;
    return glm::dvec3(f.inv * glm::dvec4(c::geodeticToECEF(g), 1.0));
  };
  const glm::dvec3 a = onAlt(-25000.0), b = onAlt(25000.0);
  const glm::dvec3 mid = geoLerpScene(f.base, f.inv, a, b, 0.5, false, false, nullptr);
  EXPECT_NEAR(f.altOf(mid), 100.0, 1e-3);
  const glm::dvec3 chord = (a + b) * 0.5;
  EXPECT_LT(f.altOf(chord), 100.0 - 40.0); // 线性弦确实低了几十米(本测试的意义)
}

TEST(GeoMotion, GroundClampedEndpointsFollowTerrain) {
  const SceneFrame f(22.6, 114.0, 0.0, 0.0);
  const MotionTerrainFn terrain = [](double, double) {
    return 30.0;
  };
  const glm::dvec3 a(0.0, 0.0, 30.0), b(100.0, 0.0, 30.0);
  // 两端贴地:中点贴地形(30 m 椭球高),与两端线性无关。
  const glm::dvec3 m = geoLerpScene(f.base, f.inv, a, b, 0.5, true, true, terrain);
  EXPECT_NEAR(f.altOf(m), 30.0, 1e-4);
  // 一端贴地、一端椭球高 80 m:中点 = (30 + 80) / 2。
  glm::dvec3 b2 = b;
  b2.z = 80.0;
  const double b2_alt = f.altOf(b2);
  const glm::dvec3 m2 = geoLerpScene(f.base, f.inv, a, b2, 0.5, true, false, terrain);
  EXPECT_NEAR(f.altOf(m2), 0.5 * (30.0 + b2_alt), 1e-4);
  // 钩子形态。
  const MotionLerpFn hook = geoMotionLerp(f.base, f.inv, terrain);
  EXPECT_LT(glm::length(hook(a, b, 0.5, true, true) - m), 1e-9);
}

TEST(GeoMotion, SceneYawToCompass) {
  const SceneFrame f0(22.6, 114.0, 0.0, 0.0);
  EXPECT_NEAR(sceneYawToCompassDeg(f0.base, glm::dvec3(0.0), 0.0), 90.0, 1e-6); // +X = 东
  EXPECT_NEAR(sceneYawToCompassDeg(f0.base, glm::dvec3(0.0), 90.0), 0.0, 1e-6); // +Y = 北
  EXPECT_NEAR(localYawToCompassDeg(0.0), 90.0, 1e-12);
  EXPECT_NEAR(localYawToCompassDeg(180.0), 270.0, 1e-12);
  // 工程 heading 30°:场景 +X 的罗盘方位 = anchorBase 的 +X 列投到 ENU。
  const SceneFrame f30(22.6, 114.0, 0.0, 30.0);
  const glm::dvec3 x_ecef = glm::dvec3(f30.base[0]);
  const c::EnuFrame e = c::enuFrameAt(c::Geodetic::fromDegrees(22.6, 114.0, 0.0));
  const double want = wrap360(glm::degrees(std::atan2(glm::dot(x_ecef, e.east), glm::dot(x_ecef, e.north))));
  EXPECT_NEAR(sceneYawToCompassDeg(f30.base, glm::dvec3(0.0), 0.0), want, 1e-6);
  EXPECT_GT(std::fabs(want - 90.0), 1.0); // 确实转了
}

TEST(GeoMotion, FollowEyeGeoMatchesEnuGeometry) {
  const SceneFrame f(22.6, 114.0, 0.0, 0.0);
  FollowPose p;
  p.target = {0.0, 0.0, 0.0};
  p.distance_m = std::sqrt(2.0);
  p.heading_deg = 0.0;
  p.pitch_deg = -45.0;
  const glm::dvec3 eye = followEyeGeo(p, f.base, f.inv);
  EXPECT_NEAR(eye.x, 0.0, 1e-6);
  EXPECT_NEAR(eye.y, -1.0, 1e-6); // 朝北看 → 眼在南
  EXPECT_NEAR(eye.z, 1.0, 1e-6);
}

TEST(FollowCamera, EyeLocalGeometry) {
  FollowPose p;
  p.target = {5.0, 5.0, 0.0};
  p.distance_m = std::sqrt(2.0);
  p.heading_deg = 0.0;
  p.pitch_deg = -45.0;
  const glm::dvec3 e = followEyeLocal(p);
  EXPECT_NEAR(e.x, 5.0, 1e-12);
  EXPECT_NEAR(e.y, 4.0, 1e-12);
  EXPECT_NEAR(e.z, 1.0, 1e-12);
  p.heading_deg = 90.0; // 朝东看 → 眼在西
  const glm::dvec3 e2 = followEyeLocal(p);
  EXPECT_NEAR(e2.x, 4.0, 1e-12);
  EXPECT_NEAR(e2.y, 5.0, 1e-12);
}

TEST(FollowCamera, ChaseFollowsTargetForwardWithDamping) {
  FollowCamera cam;
  FollowSpec s;
  s.mode = FollowMode::kChase;
  s.heading_deg = 0.0;
  s.smooth_s = 0.35;
  cam.start(s, tgt(0, 0, 0, 0.0));
  EXPECT_NEAR(cam.pose().heading_deg, 0.0, 1e-12); // 首帧到位
  // 目标转向东(90°):方位单调逼近、指数收敛。
  double prev_err = 90.0;
  for (int i = 0; i < 90; ++i) {
    const FollowPose p = cam.step(tgt(0, 0, 0, 90.0), kDt);
    const double err = std::fabs(std::remainder(90.0 - p.heading_deg, 360.0));
    EXPECT_LT(err, prev_err + 1e-12);
    prev_err = err;
  }
  EXPECT_LT(prev_err, 90.0 * std::exp(-3.0 / 0.35) * 1.01);
  // 解析:一步 α = 1 − exp(−dt/τ)。
  FollowCamera c2;
  c2.start(s, tgt(0, 0, 0, 0.0));
  const FollowPose p1 = c2.step(tgt(0, 0, 0, 90.0), kDt);
  EXPECT_NEAR(p1.heading_deg, 90.0 * (1.0 - std::exp(-kDt / 0.35)), 1e-9);
}

TEST(FollowCamera, ChaseKeepsForwardWhenTargetHasNone) {
  FollowCamera cam;
  FollowSpec s;
  s.smooth_s = 0.0;
  cam.start(s, tgt(0, 0, 0, 45.0));
  EXPECT_NEAR(cam.step(tgt(1, 0, 0, 0.0, /*has=*/false), kDt).heading_deg, 45.0, 1e-12);
}

TEST(FollowCamera, FixedIgnoresTargetForwardAndTranslatesWithDamping) {
  FollowCamera cam;
  FollowSpec s;
  s.mode = FollowMode::kFixed;
  s.heading_deg = 200.0;
  s.smooth_s = 0.5;
  s.height_m = 1.2;
  cam.start(s, tgt(0, 0, 0, 0.0));
  const FollowPose p = cam.step(tgt(10, 0, 0, 77.0), kDt);
  EXPECT_NEAR(p.heading_deg, 200.0, 1e-12);
  const double a = 1.0 - std::exp(-kDt / 0.5);
  EXPECT_NEAR(p.target.x, 10.0 * a, 1e-9);
  EXPECT_NEAR(p.target.z, 1.2, 1e-12);
  // 收敛:注视点 → 目标(静止目标,3 s 后残差 < 1e-2 m)。
  FollowPose q = p;
  for (int i = 0; i < 90; ++i)
    q = cam.step(tgt(10, 0, 0, 0.0), kDt);
  EXPECT_NEAR(q.target.x, 10.0, 10.0 * std::exp(-3.0 / 0.5) * 1.01);
}

TEST(FollowCamera, RigidWhenSmoothIsZero) {
  FollowCamera cam;
  FollowSpec s;
  s.mode = FollowMode::kChase;
  s.heading_deg = 10.0;
  s.smooth_s = 0.0;
  cam.start(s, tgt(0, 0, 0, 0.0));
  const FollowPose p = cam.step(tgt(3, 4, 5, 100.0), kDt);
  EXPECT_DOUBLE_EQ(p.target.x, 3.0);
  EXPECT_DOUBLE_EQ(p.target.y, 4.0);
  EXPECT_DOUBLE_EQ(p.target.z, 5.0);
  EXPECT_NEAR(p.heading_deg, 110.0, 1e-12);
}

TEST(FollowCamera, UserOrbitAndZoomAdjustOffsets) {
  FollowCamera cam;
  FollowSpec s;
  s.mode = FollowMode::kChase;
  s.distance_m = 30.0;
  s.pitch_deg = -25.0;
  s.smooth_s = 0.35;
  cam.start(s, tgt(0, 0, 0, 0.0));
  cam.orbit(100.0, 0.0); // +28.65°(0.005 rad / 像素)
  EXPECT_NEAR(cam.spec().heading_deg, 28.64788975654116, 1e-9);
  EXPECT_NEAR(cam.pose().heading_deg, 28.64788975654116, 1e-9); // 不经阻尼,立即跟手
  const FollowPose p = cam.step(tgt(0, 0, 0, 0.0), kDt);
  EXPECT_NEAR(p.heading_deg, 28.64788975654116, 1e-9); // 期望方位也跟着偏移 → 不回弹
  cam.orbit(0.0, 1000.0);                              // 俯仰夹到 −89
  EXPECT_DOUBLE_EQ(cam.spec().pitch_deg, -89.0);
  cam.orbit(0.0, -100000.0); // 夹到 −5
  EXPECT_DOUBLE_EQ(cam.spec().pitch_deg, -5.0);
  cam.zoom(-100.0); // 前滚一格 → 拉近 ×0.75
  EXPECT_NEAR(cam.spec().distance_m, 22.5, 1e-9);
  cam.zoom(100.0);
  EXPECT_NEAR(cam.spec().distance_m, 30.0, 1e-9);
  for (int i = 0; i < 40; ++i)
    cam.zoom(-300.0);
  EXPECT_DOUBLE_EQ(cam.spec().distance_m, 2.0);
}

TEST(FollowCamera, HeadingWrapsAlongShortestArc) {
  FollowCamera cam;
  FollowSpec s;
  s.mode = FollowMode::kChase;
  s.smooth_s = 0.35;
  cam.start(s, tgt(0, 0, 0, 359.0));
  const FollowPose p = cam.step(tgt(0, 0, 0, 1.0), kDt);
  // 359 → 1 走 +2°:一步之后在 (359, 360) ∪ [0, 1) 内,不会反向走 358°。
  const double d = std::remainder(p.heading_deg - 359.0, 360.0);
  EXPECT_GT(d, 0.0);
  EXPECT_LT(d, 2.0);
}

TEST(FollowCamera, SpecClamp) {
  FollowSpec s;
  s.distance_m = 0.1;
  s.pitch_deg = 30.0;
  s.smooth_s = 100.0;
  const FollowSpec c = clampFollowSpec(s);
  EXPECT_DOUBLE_EQ(c.distance_m, 2.0);
  EXPECT_DOUBLE_EQ(c.pitch_deg, -5.0);
  EXPECT_DOUBLE_EQ(c.smooth_s, 5.0);
}
