// Tests for LocalEntity — TRS composition, RTC model matrix at non-zero origin.

#include <gtest/gtest.h>
#include <hongtu/core/common.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/scene/local_entity.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>

using hongtu::f32;
using hongtu::f64;
using hongtu::scene::LocalEntity;
using hongtu::coordinate::PrecisionOrigin;

TEST(LocalEntityTest, DefaultIsIdentity) {
  LocalEntity e;
  glm::dmat4 m = e.getModelMatrix();
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      EXPECT_DOUBLE_EQ(m[c][r], (c == r) ? 1.0 : 0.0);
}

TEST(LocalEntityTest, PositionShowsInTranslationColumn) {
  LocalEntity e;
  e.setPosition({10.0, 20.0, 30.0});
  glm::dmat4 m = e.getModelMatrix();
  EXPECT_DOUBLE_EQ(m[3][0], 10.0);
  EXPECT_DOUBLE_EQ(m[3][1], 20.0);
  EXPECT_DOUBLE_EQ(m[3][2], 30.0);
  EXPECT_DOUBLE_EQ(m[3][3],  1.0);
}

TEST(LocalEntityTest, UniformScale) {
  LocalEntity e;
  e.setScale(3.0f);
  glm::dmat4 m = e.getModelMatrix();
  EXPECT_DOUBLE_EQ(m[0][0], 3.0);
  EXPECT_DOUBLE_EQ(m[1][1], 3.0);
  EXPECT_DOUBLE_EQ(m[2][2], 3.0);
}

TEST(LocalEntityTest, NonUniformScale) {
  LocalEntity e;
  e.setScale(glm::vec3{2.0f, 3.0f, 4.0f});
  glm::dmat4 m = e.getModelMatrix();
  EXPECT_DOUBLE_EQ(m[0][0], 2.0);
  EXPECT_DOUBLE_EQ(m[1][1], 3.0);
  EXPECT_DOUBLE_EQ(m[2][2], 4.0);
}

TEST(LocalEntityTest, QuaternionRotationAroundZ) {
  LocalEntity e;
  // 90° rotation around +Z — should map +X to +Y.
  e.setRotation(glm::angleAxis(glm::pi<f32>() * 0.5f, glm::vec3{0, 0, 1}));

  glm::dmat4 m = e.getModelMatrix();
  glm::dvec4 x_axis = m * glm::dvec4(1.0, 0.0, 0.0, 0.0);
  EXPECT_NEAR(x_axis.x, 0.0, 1e-6);
  EXPECT_NEAR(x_axis.y, 1.0, 1e-6);
}

TEST(LocalEntityTest, TRSOrderIsCorrect) {
  // Verify M = T * R * S: a point first scaled, then rotated, then translated.
  LocalEntity e;
  e.setScale(2.0f);
  e.setRotation(glm::angleAxis(glm::pi<f32>() * 0.5f, glm::vec3{0, 0, 1}));
  e.setPosition({100.0, 0.0, 0.0});

  glm::dmat4 m = e.getModelMatrix();
  glm::dvec4 unit_x = m * glm::dvec4(1.0, 0.0, 0.0, 1.0);
  // Unit X scaled to (2,0,0), rotated 90° about Z to (0,2,0), translated to (100,2,0).
  EXPECT_NEAR(unit_x.x, 100.0, 1e-6);
  EXPECT_NEAR(unit_x.y,   2.0, 1e-6);
  EXPECT_NEAR(unit_x.z,   0.0, 1e-6);
}

TEST(LocalEntityTest, RTCModelMatrixSubtractsOrigin) {
  LocalEntity e;
  e.setPosition({1000.0, 2000.0, 3000.0});

  PrecisionOrigin po;
  po.setOrigin({990.0, 1990.0, 2990.0});

  glm::mat4 rtc = e.getRTCModelMatrix(po);
  // Translation column should be position - origin = (10, 10, 10).
  EXPECT_NEAR(rtc[3][0], 10.0f, 1e-3f);
  EXPECT_NEAR(rtc[3][1], 10.0f, 1e-3f);
  EXPECT_NEAR(rtc[3][2], 10.0f, 1e-3f);
  EXPECT_FLOAT_EQ(rtc[3][3], 1.0f);
}

TEST(LocalEntityTest, RTCModelPreservesRotationAndScale) {
  LocalEntity e;
  e.setPosition({5000.0, 0.0, 0.0});
  e.setScale(2.0f);
  e.setRotation(glm::angleAxis(glm::pi<f32>() * 0.5f, glm::vec3{0, 0, 1}));

  PrecisionOrigin po;
  po.setOrigin({5000.0, 0.0, 0.0});

  glm::mat4  rtc = e.getRTCModelMatrix(po);
  glm::dmat4 model = e.getModelMatrix();

  // Upper-left 3x3 should be the same as model's upper-left 3x3 cast to f32.
  for (int c = 0; c < 3; ++c)
    for (int r = 0; r < 3; ++r)
      EXPECT_NEAR(rtc[c][r], static_cast<f32>(model[c][r]), 1e-5f);

  // Translation is centered.
  EXPECT_NEAR(rtc[3][0], 0.0f, 1e-3f);
  EXPECT_NEAR(rtc[3][1], 0.0f, 1e-3f);
  EXPECT_NEAR(rtc[3][2], 0.0f, 1e-3f);
}

TEST(LocalEntityTest, SettersAndGettersRoundTrip) {
  LocalEntity e;
  e.setPosition({1.5, 2.5, 3.5});
  e.setRotation(glm::angleAxis(0.5f, glm::vec3{1, 0, 0}));
  e.setScale(glm::vec3{1.0f, 2.0f, 3.0f});

  EXPECT_DOUBLE_EQ(e.getPosition().x, 1.5);
  EXPECT_DOUBLE_EQ(e.getPosition().y, 2.5);
  EXPECT_DOUBLE_EQ(e.getPosition().z, 3.5);
  EXPECT_FLOAT_EQ(e.getScale().y, 2.0f);
  // Quat round-trip via angle-axis: w of rotation about X by 0.5 = cos(0.25).
  EXPECT_NEAR(e.getRotation().w, std::cos(0.25f), 1e-6f);
}
