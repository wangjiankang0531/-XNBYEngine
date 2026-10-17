// Tests for the generic (coordinate-agnostic) PrecisionOrigin API.
// The earth-flavoured surface is covered by test_rtc.cpp; this file focuses
// on local-mode usage: non-ECEF world origins, plain-meters offsets, and the
// generic computeViewMatrix that takes any up vector.
//
// PrecisionOrigin and RTCOrigin are the same class (typedef alias), so these
// tests also defend the rename's backward compatibility.

#include <gtest/gtest.h>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace hongtu::coordinate;

TEST(PrecisionOriginTest, SetOriginAcceptsArbitraryWorld) {
  PrecisionOrigin po;
  glm::dvec3 origin{1000.0, 2000.0, 3000.0};
  po.setOrigin(origin);

  EXPECT_DOUBLE_EQ(po.getOrigin().x, 1000.0);
  EXPECT_DOUBLE_EQ(po.getOrigin().y, 2000.0);
  EXPECT_DOUBLE_EQ(po.getOrigin().z, 3000.0);
}

TEST(PrecisionOriginTest, WorldToLocalIsZeroAtOrigin) {
  PrecisionOrigin po;
  po.setOrigin({1.0e6, 2.0e6, 3.0e6});

  glm::vec3 local = po.worldToLocal({1.0e6, 2.0e6, 3.0e6});
  EXPECT_NEAR(local.x, 0.0f, 1e-3f);
  EXPECT_NEAR(local.y, 0.0f, 1e-3f);
  EXPECT_NEAR(local.z, 0.0f, 1e-3f);
}

TEST(PrecisionOriginTest, WorldToLocalRoundTrip) {
  PrecisionOrigin po;
  po.setOrigin({500.0, -200.0, 75.0});

  glm::dvec3 world{510.0, -180.0, 80.0};
  glm::vec3  local = po.worldToLocal(world);
  glm::dvec3 back  = po.localToWorld(local);

  // 10m offset from origin — float32 precision is way better than 1mm here.
  EXPECT_NEAR(back.x, world.x, 1e-4);
  EXPECT_NEAR(back.y, world.y, 1e-4);
  EXPECT_NEAR(back.z, world.z, 1e-4);
}

TEST(PrecisionOriginTest, WorldToLocalSubtractsOrigin) {
  PrecisionOrigin po;
  po.setOrigin({100.0, 200.0, 300.0});

  glm::vec3 local = po.worldToLocal({105.0, 210.0, 315.0});
  EXPECT_NEAR(local.x, 5.0f,  1e-4f);
  EXPECT_NEAR(local.y, 10.0f, 1e-4f);
  EXPECT_NEAR(local.z, 15.0f, 1e-4f);
}

TEST(PrecisionOriginTest, GenericAndECEFAliasShareState) {
  // setOrigin and setOriginECEF write to the same underlying field; the
  // aliases are interchangeable for callers that only need world-space.
  PrecisionOrigin po;
  po.setOrigin({42.0, 43.0, 44.0});

  EXPECT_DOUBLE_EQ(po.getOriginECEF().x, 42.0);
  EXPECT_DOUBLE_EQ(po.getOriginECEF().y, 43.0);
  EXPECT_DOUBLE_EQ(po.getOriginECEF().z, 44.0);

  // ecefToRTC alias forwards to worldToLocal.
  glm::vec3 a = po.ecefToRTC({42.0, 43.0, 44.0});
  glm::vec3 b = po.worldToLocal({42.0, 43.0, 44.0});
  EXPECT_FLOAT_EQ(a.x, b.x);
  EXPECT_FLOAT_EQ(a.y, b.y);
  EXPECT_FLOAT_EQ(a.z, b.z);
}

TEST(PrecisionOriginTest, ComputeViewMatrixWithYUp) {
  // Local-mode usage: camera at (0, 0, 10) looking down at origin with Y-up.
  PrecisionOrigin po;
  po.setOrigin({0.0, 0.0, 0.0});

  glm::dvec3 eye{0.0, 0.0, 10.0};
  glm::dvec3 target{0.0, 0.0, 0.0};
  glm::vec3  up{0.0f, 1.0f, 0.0f};

  glm::mat4 v_generic  = po.computeViewMatrix(eye, target, up);
  glm::mat4 v_expected = glm::lookAt(glm::vec3(0, 0, 10),
                                      glm::vec3(0, 0, 0),
                                      glm::vec3(0, 1, 0));

  // Bit-exact: PrecisionOrigin just calls glm::lookAt internally.
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      EXPECT_FLOAT_EQ(v_generic[c][r], v_expected[c][r]);
}

TEST(PrecisionOriginTest, ComputeViewMatrixWithOriginOffset) {
  // World coords are far from machine origin but close to the precision origin —
  // the resulting view matrix should match a "near-origin" lookAt exactly.
  PrecisionOrigin po;
  po.setOrigin({1.0e6, 2.0e6, 3.0e6});

  glm::dvec3 eye    = po.getOrigin() + glm::dvec3{0.0, 0.0, 10.0};
  glm::dvec3 target = po.getOrigin();
  glm::vec3  up{0.0f, 1.0f, 0.0f};

  glm::mat4 v_actual   = po.computeViewMatrix(eye, target, up);
  glm::mat4 v_expected = glm::lookAt(glm::vec3(0, 0, 10),
                                      glm::vec3(0, 0, 0),
                                      glm::vec3(0, 1, 0));
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      EXPECT_NEAR(v_actual[c][r], v_expected[c][r], 1e-3f);
}

TEST(PrecisionOriginTest, ComputeViewMatrixAliasEqualsGeneric) {
  // computeRTCViewMatrix is a forwarder — must produce identical output.
  PrecisionOrigin po;
  po.setOrigin({1234.5, -678.9, 42.0});

  glm::dvec3 eye{1244.5, -668.9, 50.0};
  glm::dvec3 target{1234.5, -678.9, 42.0};
  glm::vec3  up{0.0f, 0.0f, 1.0f};

  glm::mat4 generic = po.computeViewMatrix(eye, target, up);
  glm::mat4 alias   = po.computeRTCViewMatrix(eye, target, up);
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      EXPECT_FLOAT_EQ(generic[c][r], alias[c][r]);
}

TEST(PrecisionOriginTest, ShouldRecenterGenericThreshold) {
  PrecisionOrigin po;
  po.setOrigin({0.0, 0.0, 0.0});

  EXPECT_FALSE(po.shouldRecenter({100.0, 0.0, 0.0}, 1000.0));
  EXPECT_TRUE (po.shouldRecenter({2000.0, 0.0, 0.0}, 1000.0));
  // Default threshold is 500 km — well outside the small world.
  EXPECT_FALSE(po.shouldRecenter({1000.0, 1000.0, 1000.0}));
}

TEST(PrecisionOriginTest, RTCOriginAliasIsSameType) {
  // The typedef must resolve to PrecisionOrigin so existing globe code that
  // declares `RTCOrigin rtc;` keeps compiling and behaving identically.
  RTCOrigin       legacy;
  PrecisionOrigin modern;
  legacy.setOrigin({7.0, 8.0, 9.0});
  modern.setOrigin({7.0, 8.0, 9.0});

  EXPECT_FLOAT_EQ(legacy.worldToLocal({10.0, 11.0, 12.0}).x,
                  modern.worldToLocal({10.0, 11.0, 12.0}).x);
}
