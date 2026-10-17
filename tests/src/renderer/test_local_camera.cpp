// Tests for LocalCamera — exercise FreeFly translation, Orbit math, mouse
// handlers. The camera has no earth dependencies; tests use plain meters.

#include <gtest/gtest.h>
#include <hongtu/core/common.hpp>
#include <hongtu/renderer/camera/local_camera.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cmath>

using hongtu::f32;
using hongtu::f64;
using hongtu::renderer::LocalCamera;

TEST(LocalCameraTest, DefaultsAreSane) {
  LocalCamera cam;
  EXPECT_EQ(cam.getMode(), LocalCamera::Mode::Orbit);
  EXPECT_GT(cam.getFar(), cam.getNear());
  EXPECT_GT(cam.getFovYDegrees(), 0.0f);
}

TEST(LocalCameraTest, OrbitEyeIsTargetMinusForwardTimesDistance) {
  LocalCamera cam;
  cam.lookAtTarget({0.0, 0.0, 0.0}, 10.0,
                    /*yaw_rad*/ 0.0, /*pitch_rad*/ 0.0);

  glm::dvec3 eye = cam.getEyeWorld();
  // pitch=0, yaw=0 → forward = +X. Eye = target - forward*distance = (-10, 0, 0).
  EXPECT_NEAR(eye.x, -10.0, 1e-9);
  EXPECT_NEAR(eye.y,   0.0, 1e-9);
  EXPECT_NEAR(eye.z,   0.0, 1e-9);
}

TEST(LocalCameraTest, OrbitLooksAtTarget) {
  LocalCamera cam;
  cam.lookAtTarget({5.0, 6.0, 7.0}, 3.0);
  EXPECT_NEAR(cam.getTargetWorld().x, 5.0, 1e-9);
  EXPECT_NEAR(cam.getTargetWorld().y, 6.0, 1e-9);
  EXPECT_NEAR(cam.getTargetWorld().z, 7.0, 1e-9);
}

TEST(LocalCameraTest, FreeFlyMoveForwardTranslatesInFacingDirection) {
  LocalCamera cam;
  cam.setMode(LocalCamera::Mode::FreeFly);
  cam.setPosition({0.0, 0.0, 0.0});
  cam.setYawPitch(0.0, 0.0);   // facing +X

  cam.moveForward(5.0);
  EXPECT_NEAR(cam.getPosition().x, 5.0, 1e-9);
  EXPECT_NEAR(cam.getPosition().y, 0.0, 1e-9);
  EXPECT_NEAR(cam.getPosition().z, 0.0, 1e-9);
}

TEST(LocalCameraTest, FreeFlyMoveRightIsCrossProductOfForwardAndUp) {
  LocalCamera cam;
  cam.setMode(LocalCamera::Mode::FreeFly);
  cam.setPosition({0.0, 0.0, 0.0});
  cam.setYawPitch(0.0, 0.0);   // forward = +X, up = +Z → right = X × Z = -Y? Check.

  cam.moveRight(2.0);
  // glm::cross(forward, up) = cross(+X, +Z) = -Y. So moveRight goes to -Y.
  EXPECT_NEAR(cam.getPosition().x,  0.0, 1e-9);
  EXPECT_NEAR(cam.getPosition().y, -2.0, 1e-9);
  EXPECT_NEAR(cam.getPosition().z,  0.0, 1e-9);
}

TEST(LocalCameraTest, FreeFlyMoveUpTranslatesAlongUp) {
  LocalCamera cam;
  cam.setMode(LocalCamera::Mode::FreeFly);
  cam.setPosition({1.0, 2.0, 3.0});
  cam.moveUp(7.0);
  EXPECT_NEAR(cam.getPosition().z, 10.0, 1e-9);
}

TEST(LocalCameraTest, MouseDragInOrbitRotatesYawAndPitch) {
  LocalCamera cam;
  cam.lookAtTarget({0, 0, 0}, 10.0, /*yaw*/ 0.0, /*pitch*/ 0.0);
  cam.onMouseDown(100.0f, 100.0f);
  cam.onMouseMove(200.0f, 150.0f);   // dx=+100, dy=+50

  // Orbit drag subtracts dx*rate from yaw, dy*rate from pitch.
  EXPECT_LT(cam.getYaw(), 0.0);
  EXPECT_LT(cam.getPitch(), 0.0);
  cam.onMouseUp();
}

TEST(LocalCameraTest, MouseScrollInOrbitAdjustsDistance) {
  LocalCamera cam;
  cam.lookAtTarget({0, 0, 0}, 10.0);
  f64 d0 = cam.getDistance();
  cam.onMouseScroll(+1.0f);          // zoom in
  EXPECT_LT(cam.getDistance(), d0);
  cam.onMouseScroll(-2.0f);          // zoom out past starting
  EXPECT_GT(cam.getDistance(), d0);
}

TEST(LocalCameraTest, ViewMatrixFromOrbitMatchesGlmLookAt) {
  LocalCamera cam;
  cam.lookAtTarget({0, 0, 0}, 10.0,
                    /*yaw*/ 0.0, /*pitch*/ 0.0);

  glm::dmat4 v_actual = cam.getViewMatrix();
  glm::dmat4 v_expected = glm::lookAt(
      glm::dvec3{-10.0, 0.0, 0.0},
      glm::dvec3{ 0.0, 0.0, 0.0},
      glm::dvec3{ 0.0, 0.0, 1.0});

  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      EXPECT_NEAR(v_actual[c][r], v_expected[c][r], 1e-9);
}

TEST(LocalCameraTest, PitchIsClampedAwayFromPoles) {
  LocalCamera cam;
  cam.setYawPitch(0.0, 10.0);   // way past +π/2
  EXPECT_LT(cam.getPitch(), glm::pi<f64>() / 2.0);

  cam.setYawPitch(0.0, -10.0);
  EXPECT_GT(cam.getPitch(), -glm::pi<f64>() / 2.0);
}

TEST(LocalCameraTest, FreeFlyEyeIsCameraPosition) {
  LocalCamera cam;
  cam.setMode(LocalCamera::Mode::FreeFly);
  cam.setPosition({1.0, 2.0, 3.0});
  cam.setYawPitch(0.0, 0.0);

  glm::dvec3 eye = cam.getEyeWorld();
  EXPECT_NEAR(eye.x, 1.0, 1e-9);
  EXPECT_NEAR(eye.y, 2.0, 1e-9);
  EXPECT_NEAR(eye.z, 3.0, 1e-9);
}
