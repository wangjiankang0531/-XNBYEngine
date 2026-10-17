// Tests for the abstract CameraBase. Uses a minimal mock subclass that just
// returns the three world-space primitives from constructor args — no globe,
// no local-camera state — so the tests exercise the base's math in isolation.

#include <gtest/gtest.h>
#include <hongtu/core/common.hpp>
#include <hongtu/renderer/camera/camera_base.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

using hongtu::f32;
using hongtu::f64;

using namespace hongtu::renderer;
using namespace hongtu::coordinate;

namespace {

class MockCamera : public CameraBase {
 public:
  MockCamera(glm::dvec3 eye, glm::dvec3 target, glm::dvec3 up)
      : eye_(eye), target_(target), up_(up) {}
  glm::dvec3 getEyeWorld()    const override { return eye_; }
  glm::dvec3 getTargetWorld() const override { return target_; }
  glm::dvec3 getUpWorld()     const override { return up_; }
 private:
  glm::dvec3 eye_, target_, up_;
};

}  // namespace

TEST(CameraBaseTest, ViewMatrixIsLookAtOfPureVirtuals) {
  MockCamera cam({0, 0, 10}, {0, 0, 0}, {0, 1, 0});

  glm::dmat4 v = cam.getViewMatrix();
  glm::dmat4 expected = glm::lookAt(glm::dvec3{0, 0, 10},
                                     glm::dvec3{0, 0, 0},
                                     glm::dvec3{0, 1, 0});

  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      EXPECT_DOUBLE_EQ(v[c][r], expected[c][r]);
}

TEST(CameraBaseTest, ProjectionMatrixDefaultsToPerspective) {
  MockCamera cam({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
  cam.setFovYDegrees(60.0f);
  cam.setNearFar(0.1f, 1000.0f);

  glm::mat4 p = cam.getProjectionMatrix(16.0f / 9.0f);
  glm::mat4 expected = glm::perspective(glm::radians(60.0f),
                                         16.0f / 9.0f,
                                         0.1f, 1000.0f);
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      EXPECT_FLOAT_EQ(p[c][r], expected[c][r]);
}

TEST(CameraBaseTest, FovNearFarStateIsPreserved) {
  MockCamera cam({0, 0, 1}, {0, 0, 0}, {0, 1, 0});
  cam.setFovYDegrees(75.0f);
  cam.setNearFar(0.5f, 5000.0f);
  EXPECT_FLOAT_EQ(cam.getFovYDegrees(), 75.0f);
  EXPECT_FLOAT_EQ(cam.getNear(), 0.5f);
  EXPECT_FLOAT_EQ(cam.getFar(), 5000.0f);
}

TEST(CameraBaseTest, RTCViewMatrixAtIdentityOriginEqualsViewMatrix) {
  // With origin at (0,0,0), worldToLocal is identity, so the RTC view matrix
  // is just the float32 cast of the f64 view matrix.
  MockCamera cam({0, 0, 10}, {0, 0, 0}, {0, 1, 0});
  PrecisionOrigin po;
  po.setOrigin({0, 0, 0});

  glm::mat4  rtc_view  = cam.getRTCViewMatrix(po);
  glm::dmat4 full_view = cam.getViewMatrix();

  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      EXPECT_NEAR(rtc_view[c][r], static_cast<f32>(full_view[c][r]), 1e-5f);
}

TEST(CameraBaseTest, RTCViewMatrixUsesOriginForFloat32Precision) {
  // World eye/target are huge (ECEF-scale); a naive lookAt would lose
  // precision. The RTC view matrix uses the origin to recover it.
  MockCamera cam({6378137.0 + 10.0, 0.0, 0.0},
                  {6378137.0, 0.0, 0.0},
                  {0, 0, 1});

  PrecisionOrigin po;
  po.setOrigin({6378137.0, 0.0, 0.0});

  glm::mat4 rtc_view = cam.getRTCViewMatrix(po);

  // After centering, the eye is at (10, 0, 0) looking at (0, 0, 0). The
  // translation column of the resulting view matrix should reflect this
  // small offset — NOT something on the order of 6e6.
  // Column 3 of a view matrix is the translation (in camera space).
  // |translation| should be small (= 10 m magnitude in this setup).
  glm::vec3 cam_translation(rtc_view[3][0], rtc_view[3][1], rtc_view[3][2]);
  EXPECT_LT(glm::length(cam_translation), 100.0f);
}
