#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>
#include <hongtu/renderer/globe/globe_camera.hpp>
#include <hongtu/renderer/picking/ray_caster.hpp> // 完整 PickHit 定义

#include <cmath>

using namespace hongtu;
using namespace hongtu::renderer;
using namespace hongtu::coordinate;

class GlobeCameraTest : public ::testing::Test {
protected:
  GlobeCamera camera;
};

TEST_F(GlobeCameraTest, LookAtSetsPositionNearTarget) {
  Geodetic target = Geodetic::fromDegrees(39.9, 116.4); // Beijing
  f64 distance = 10000.0;                               // 10 km
  camera.lookAt(target, distance);

  Geodetic cam_geo = camera.getPositionGeodetic();
  // Camera should be above the target area (within a few degrees)
  f64 lat_diff = std::abs(cam_geo.lat - target.lat);
  f64 lon_diff = std::abs(cam_geo.lon - target.lon);
  EXPECT_LT(lat_diff, glm::radians(5.0));
  EXPECT_LT(lon_diff, glm::radians(5.0));

  // Altitude should be roughly the distance (looking straight down)
  f64 alt = camera.getAltitude();
  EXPECT_GT(alt, distance * 0.5);
  EXPECT_LT(alt, distance * 1.5);
}

// 获取当前视角(captureOrbitPose) → 定位到默认视角(lookAt with the captured
// pose) must reproduce the SAME eye position — no added height, no drift.
// Tests several altitudes/pitches incl. after a wheel dolly (so eye_ecef_ is
// the source of truth, not the stale lookAt orbit state).
TEST_F(GlobeCameraTest, CaptureApplyRoundTripKeepsEye) {
  camera.setScreenSize(1920.0f, 1080.0f);
  struct Pose {
    double lat, lon, dist, heading, pitch;
    bool dolly;
  };
  const Pose poses[] = {
      {39.9, 116.4, 40000.0, -9.0, -88.0, false},   // near-nadir panorama
      {32.8, 104.8, 4159720.0, -9.0, -88.0, false}, // high China panorama
      {31.2, 121.5, 12000.0, 30.0, -35.0, false},   // oblique mid
      {22.5, 114.0, 6000.0, 120.0, -20.0, false},   // oblique low
      {39.9, 116.4, 30000.0, 0.0, -45.0, true},     // then wheel-dolly
  };
  for (const auto& p : poses) {
    camera.lookAt(Geodetic::fromDegrees(p.lat, p.lon, 0.0), p.dist, p.heading, p.pitch);
    if (p.dolly) {
      for (int i = 0; i < 4; ++i)
        camera.onMouseScroll(1.0f);
    }

    glm::dvec3 eye_before = camera.getEyeWorld();
    glm::dvec3 target_before = camera.getTargetWorld();
    glm::dvec3 up_before = camera.getUpWorld();
    // Exact snapshot → restore must reproduce eye AND look direction verbatim.
    auto pose = camera.capturePoseState();
    camera.lookAt(Geodetic::fromDegrees(0, 0, 0), 1.0e7, 0, -90); // scramble
    camera.restorePoseState(pose);
    glm::dvec3 eye_after = camera.getEyeWorld();
    glm::dvec3 target_after = camera.getTargetWorld();
    glm::dvec3 up_after = camera.getUpWorld();

    EXPECT_LT(glm::length(eye_after - eye_before), 1e-3)
        << "pose lat=" << p.lat << " pitch=" << p.pitch << " eye moved";
    // Forward direction (target is eye + fwd*1e7) and up must match too.
    glm::dvec3 fwd_b = glm::normalize(target_before - eye_before);
    glm::dvec3 fwd_a = glm::normalize(target_after - eye_after);
    EXPECT_GT(glm::dot(fwd_b, fwd_a), 0.99999) << "look dir drifted lat=" << p.lat;
    EXPECT_GT(glm::dot(glm::normalize(up_before), glm::normalize(up_after)), 0.99999);
  }
}

// The camera up vector must never point "down" (toward earth center) when the
// camera looks down at the globe — otherwise the view is upside-down. This
// regressed at shallow pitches (config-set home/lookAt at e.g. -20°).
TEST_F(GlobeCameraTest, UpVectorNeverFlipsAcrossPitch) {
  camera.setScreenSize(1920.0f, 1080.0f);
  for (double pitch : {-85.0, -60.0, -45.0, -30.0, -20.0, -10.0, -6.0}) {
    camera.lookAt(Geodetic::fromDegrees(34.0, 110.0, 0.0), 40000.0, 0.0, pitch);
    glm::dvec3 up = glm::normalize(camera.getUpWorld());
    glm::dvec3 radial_up = glm::normalize(camera.getEyeWorld());
    // up may be near-horizontal (dot≈0) but must NOT point downward.
    EXPECT_GT(glm::dot(up, radial_up), -0.05) << "up flipped downward at pitch " << pitch;
  }
}

TEST_F(GlobeCameraTest, FlyToAnimationStartsAndCompletes) {
  Geodetic start = Geodetic::fromDegrees(0.0, 0.0);
  Geodetic end = Geodetic::fromDegrees(40.0, 116.0);

  camera.lookAt(start, 1000000.0);
  EXPECT_FALSE(camera.isFlying());

  camera.flyTo(end, 50000.0, 2.0);
  EXPECT_TRUE(camera.isFlying());

  // Advance halfway
  camera.update(1.0f);
  EXPECT_TRUE(camera.isFlying());

  // Advance past duration
  camera.update(1.5f);
  EXPECT_FALSE(camera.isFlying());

  // Camera should now be near the destination
  Geodetic final_geo = camera.getPositionGeodetic();
  EXPECT_NEAR(final_geo.lat, end.lat, glm::radians(5.0));
  EXPECT_NEAR(final_geo.lon, end.lon, glm::radians(5.0));
}

TEST_F(GlobeCameraTest, ViewMatrixIsValid) {
  Geodetic target = Geodetic::fromDegrees(30.0, 120.0);
  camera.lookAt(target, 500000.0);

  glm::dmat4 view = camera.getViewMatrix();

  // Determinant of a valid view matrix should be ~1 (or -1 for reflection)
  f64 det = glm::determinant(view);
  EXPECT_NEAR(std::abs(det), 1.0, 0.01);
}

TEST_F(GlobeCameraTest, ProjectionMatrixNonZero) {
  Geodetic target = Geodetic::fromDegrees(0.0, 0.0);
  camera.lookAt(target, 100000.0);

  glm::mat4 proj = camera.getProjectionMatrix(16.0f / 9.0f);

  // Projection matrix should have non-zero diagonal elements
  EXPECT_NE(proj[0][0], 0.0f);
  EXPECT_NE(proj[1][1], 0.0f);
  EXPECT_NE(proj[2][2], 0.0f);
}

TEST_F(GlobeCameraTest, ProjectionOverrideMatchesSharedTransitionFrustum) {
  camera.lookAt(Geodetic::fromDegrees(22.74776854553, 114.599640461187), 12000000.0, 0.0, -89.0);
  constexpr float kAspect = 16.0f / 9.0f;
  constexpr float kFovDegrees = 47.0f;
  constexpr float kNearMeters = 1200.0f;
  constexpr float kFarMeters = 17000000.0f;

  camera.setProjectionOverride(kFovDegrees, kNearMeters, kFarMeters);
  ASSERT_TRUE(camera.hasProjectionOverride());
  glm::mat4 expected = glm::perspective(glm::radians(kFovDegrees), kAspect, kNearMeters, kFarMeters);
  expected[1][1] *= -1.0f;
  const glm::mat4 actual = camera.getProjectionMatrix(kAspect);
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      EXPECT_NEAR(actual[column][row], expected[column][row], 1.0e-6f);
    }
  }

  camera.clearProjectionOverride();
  EXPECT_FALSE(camera.hasProjectionOverride());
  const glm::mat4 automatic = camera.getProjectionMatrix(kAspect);
  EXPECT_NE(automatic[0][0], actual[0][0]);
}

TEST_F(GlobeCameraTest, ViewUpOverridePinsCompositeRendererRoll) {
  const Geodetic target = Geodetic::fromDegrees(22.74776854553, 114.599640461187);
  camera.lookAt(target, 12000000.0, 0.0, -80.0);
  const glm::dvec3 forward = glm::normalize(camera.getTargetWorld() - camera.getEyeWorld());
  const glm::dvec3 requestedUp = glm::normalize(glm::dvec3{0.25, -0.42, 0.87});
  camera.setViewUpOverride(requestedUp);

  const glm::dvec3 expectedUp = glm::normalize(requestedUp - forward * glm::dot(requestedUp, forward));
  EXPECT_GT(glm::dot(glm::normalize(camera.getUpWorld()), expectedUp), 0.999999);

  camera.clearViewUpOverride();
  EXPECT_LT(glm::dot(glm::normalize(camera.getUpWorld()), expectedUp), 0.999999);
}

TEST_F(GlobeCameraTest, MouseScrollChangesAltitude) {
  Geodetic target = Geodetic::fromDegrees(0.0, 0.0);
  camera.lookAt(target, 1000000.0);

  f64 initial_alt = camera.getAltitude();

  // Scroll in (positive delta = zoom in = decrease distance)
  camera.onMouseScroll(1.0f);

  f64 new_alt = camera.getAltitude();
  EXPECT_LT(new_alt, initial_alt);
}

TEST_F(GlobeCameraTest, RTCViewMatrixIsValid) {
  Geodetic target = Geodetic::fromDegrees(35.0, 139.0); // Tokyo
  camera.lookAt(target, 200000.0);

  glm::mat4 rtc_view = camera.getRTCViewMatrix();

  // RTC view matrix should have non-zero determinant
  f32 det = glm::determinant(rtc_view);
  EXPECT_NE(det, 0.0f);
}

TEST_F(GlobeCameraTest, InteractiveDisableBlocksInput) {
  Geodetic target = Geodetic::fromDegrees(0.0, 0.0);
  camera.lookAt(target, 1000000.0);

  f64 alt_before = camera.getAltitude();

  camera.setInteractive(false);
  camera.onMouseScroll(5.0f);

  f64 alt_after = camera.getAltitude();
  EXPECT_DOUBLE_EQ(alt_before, alt_after);
}

// ============================================================================
// PICK-* / WHEEL-* / NO-FLIP 不变量(geo 模式交互标准)
// ============================================================================

namespace {
  // 世界点投影回屏幕像素(与 GlobeCamera 渲染同一套矩阵,含 Y-flip)。
  glm::dvec2 projectToScreen(const GlobeCamera& cam, double w, double h, const glm::dvec3& world) {
    glm::dmat4 view = cam.getViewMatrix();
    glm::dmat4 proj(cam.getProjectionMatrix(static_cast<float>(w / h)));
    glm::dvec4 clip = proj * view * glm::dvec4(world, 1.0);
    glm::dvec3 ndc = glm::dvec3(clip) / clip.w;
    return {(ndc.x * 0.5 + 0.5) * w, (ndc.y * 0.5 + 0.5) * h};
  }

  struct Pose {
    double lat, lon, alt, heading, pitch;
  };
  const Pose kPoses[] = {
      {33.5, 104.6, 50.0, 0.0, -89.0},
      {33.5, 104.6, 5000.0, 30.0, -60.0},
      {0.0, 0.0, 500000.0, 0.0, -89.0},
      {-33.9, 151.2, 5000.0, -45.0, -50.0},
      {80.0, -45.0, 50000.0, 10.0, -70.0},
  };
} // namespace

// PICK-ROUNDTRIP: pickWorld 后投影回屏幕 ≈ 原屏幕点。
TEST_F(GlobeCameraTest, PickRoundTripWithinPixel) {
  const double w = 1280, h = 720;
  camera.setScreenSize(static_cast<float>(w), static_cast<float>(h));
  for (const auto& p : kPoses) {
    camera.lookAt(Geodetic::fromDegrees(p.lat, p.lon, p.alt), p.alt + 10.0, p.heading, p.pitch);
    camera.update(0.016f);
    for (double sy : {h * 0.4, h * 0.5, h * 0.6}) {
      for (double sx : {w * 0.4, w * 0.5, w * 0.6}) {
        auto hit = camera.pickWorld(sx, sy, w, h);
        ASSERT_TRUE(hit.has_value()) << p.lat << "," << p.lon;
        glm::dvec2 reproj = projectToScreen(camera, w, h, hit->point);
        EXPECT_NEAR(reproj.x, sx, 1.5) << "lat=" << p.lat;
        EXPECT_NEAR(reproj.y, sy, 1.5) << "lat=" << p.lat;
      }
    }
  }
}

// PICK-ABOVE-SURFACE: 命中点不在地下(geodetic alt ≈ 0)。
TEST_F(GlobeCameraTest, PickHitOnSurfaceNotUnderground) {
  const double w = 1280, h = 720;
  camera.setScreenSize(static_cast<float>(w), static_cast<float>(h));
  for (const auto& p : kPoses) {
    camera.lookAt(Geodetic::fromDegrees(p.lat, p.lon, p.alt), p.alt + 10.0, p.heading, p.pitch);
    camera.update(0.016f);
    auto hit = camera.pickWorld(w * 0.5, h * 0.5, w, h);
    ASSERT_TRUE(hit.has_value());
    auto g = ecefToGeodetic(hit->point);
    EXPECT_GT(g.alt, -2.0) << "lat=" << p.lat;
    EXPECT_LT(g.alt, 2.0) << "lat=" << p.lat;
  }
}

// PICK-NEAR-SIDE: 命中点与相机 nadir 同半球(非对跖点)。
TEST_F(GlobeCameraTest, PickHitsNearSideNotAntipode) {
  const double w = 1280, h = 720;
  camera.setScreenSize(static_cast<float>(w), static_cast<float>(h));
  for (const auto& p : kPoses) {
    camera.lookAt(Geodetic::fromDegrees(p.lat, p.lon, p.alt), p.alt + 10.0, p.heading, p.pitch);
    camera.update(0.016f);
    auto hit = camera.pickWorld(w * 0.5, h * 0.5, w, h);
    ASSERT_TRUE(hit.has_value());
    auto g = ecefToGeodetic(hit->point);
    EXPECT_LT(std::abs(glm::degrees(g.lat) - p.lat), 5.0) << "lat flip @ " << p.lat;
    double dlon = std::abs(glm::degrees(g.lon) - p.lon);
    if (dlon > 180.0)
      dlon = 360.0 - dlon;
    EXPECT_LT(dlon, 5.0) << "lon flip @ " << p.lon;
  }
}

// WHEEL-RADIAL: 滚轮只改高度,nadir 经纬不变。
TEST_F(GlobeCameraTest, WheelKeepsNadirLatLon) {
  camera.setScreenSize(1280, 720);
  camera.lookAt(Geodetic::fromDegrees(33.5, 104.6, 40000.0), 40000.0, 0.0, -89.0);
  camera.update(0.016f);
  auto before = ecefToGeodetic(camera.getPositionECEF());
  for (int i = 0; i < 5; ++i) {
    camera.onMouseScroll(1.0f);
    camera.update(0.016f);
  }
  auto after = ecefToGeodetic(camera.getPositionECEF());
  EXPECT_NEAR(after.lat, before.lat, 1e-6);
  EXPECT_NEAR(after.lon, before.lon, 1e-6);
  EXPECT_LT(after.alt, before.alt);
}

// WHEEL-BOUNDED: 高度始终在 [1, 3e7];低空单档比率 < 高空单档比率。
TEST_F(GlobeCameraTest, WheelBoundedAndAltitudeAware) {
  camera.setScreenSize(1280, 720);
  camera.lookAt(Geodetic::fromDegrees(33.5, 104.6, 100000.0), 100000.0, 0.0, -89.0);
  camera.update(0.016f);
  f64 hi0 = camera.getAltitude();
  camera.onMouseScroll(1.0f);
  camera.update(0.016f);
  f64 hi_ratio = camera.getAltitude() / hi0;

  camera.lookAt(Geodetic::fromDegrees(33.5, 104.6, 100.0), 100.0, 0.0, -89.0);
  camera.update(0.016f);
  f64 lo0 = camera.getAltitude();
  camera.onMouseScroll(1.0f);
  camera.update(0.016f);
  f64 lo_ratio = camera.getAltitude() / lo0;

  EXPECT_GT(lo_ratio, hi_ratio) << "low-alt should zoom finer";

  for (int i = 0; i < 100; ++i) {
    camera.onMouseScroll(-1.0f);
    camera.update(0.016f);
  }
  EXPECT_LE(camera.getAltitude(), 3.0e7 + 1.0);
  for (int i = 0; i < 200; ++i) {
    camera.onMouseScroll(1.0f);
    camera.update(0.016f);
  }
  EXPECT_GE(camera.getAltitude(), 1.0);
}

// NO-FLIP: 连续小幅 right-drag,视线方向连续(无 180° 翻转)。
TEST_F(GlobeCameraTest, RightDragNoViewFlip) {
  camera.setScreenSize(1280, 720);
  camera.lookAt(Geodetic::fromDegrees(33.5, 104.6, 5000.0), 5000.0, 0.0, -60.0);
  camera.update(0.016f);
  glm::dvec3 prev = glm::normalize(camera.getTargetWorld() - camera.getEyeWorld());
  for (int i = 0; i < 30; ++i) {
    camera.onRightDrag(2.0f, 1.0f);
    camera.update(0.016f);
    glm::dvec3 fwd = glm::normalize(camera.getTargetWorld() - camera.getEyeWorld());
    EXPECT_GT(glm::dot(prev, fwd), 0.9) << "view jumped at step " << i;
    EXPECT_FALSE(std::isnan(fwd.x));
    prev = fwd;
  }
}
