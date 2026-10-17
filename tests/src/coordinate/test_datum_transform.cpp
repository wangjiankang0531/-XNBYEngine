#include <gtest/gtest.h>
#include <hongtu/coordinate/datum_transform.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <cmath>

using namespace hongtu::coordinate;

static constexpr double METER_TOL = 0.01;

// ============================================================================
// Bursa-Wolf 7-Parameter Transform Tests
// ============================================================================

TEST(BursaWolfTest, IdentityTransform) {
  BursaWolfParams identity = {0, 0, 0, 0, 0, 0, 0};
  glm::dvec3 point(1000000.0, 2000000.0, 3000000.0);
  auto result = bursaWolfForward(point, identity);
  EXPECT_NEAR(result.x, point.x, 1e-6);
  EXPECT_NEAR(result.y, point.y, 1e-6);
  EXPECT_NEAR(result.z, point.z, 1e-6);
}

TEST(BursaWolfTest, PureTranslation) {
  BursaWolfParams params = {100.0, 200.0, 300.0, 0, 0, 0, 0};
  glm::dvec3 point(1000000.0, 2000000.0, 3000000.0);
  auto result = bursaWolfForward(point, params);
  EXPECT_NEAR(result.x, 1000100.0, 1e-6);
  EXPECT_NEAR(result.y, 2000200.0, 1e-6);
  EXPECT_NEAR(result.z, 3000300.0, 1e-6);
}

TEST(BursaWolfTest, ForwardInverseRoundtrip) {
  // WGS84 to Beijing-54 approximate parameters
  BursaWolfParams params = {-12.0, 356.0, 245.0, 0.0, 0.0, 0.0, 0.0};

  glm::dvec3 point = geodeticToECEF(Geodetic::fromDegrees(39.9042, 116.4074, 50.0));
  auto forward = bursaWolfForward(point, params);
  auto back = bursaWolfInverse(forward, params);

  EXPECT_NEAR(back.x, point.x, METER_TOL);
  EXPECT_NEAR(back.y, point.y, METER_TOL);
  EXPECT_NEAR(back.z, point.z, METER_TOL);
}

TEST(BursaWolfTest, WithRotationAndScale) {
  BursaWolfParams params = {10.0, 20.0, 30.0, 1.0, 2.0, 3.0, 5.0}; // arcsec, ppm
  glm::dvec3 point(3000000.0, 4000000.0, 5000000.0);

  auto forward = bursaWolfForward(point, params);
  // Result should differ from input
  EXPECT_GT(glm::length(forward - point), 1.0);

  // Roundtrip
  auto back = bursaWolfInverse(forward, params);
  // For small angles, inverse is approximate - tolerance is larger
  EXPECT_NEAR(back.x, point.x, 1.0); // 1m tolerance for small angle approx
  EXPECT_NEAR(back.y, point.y, 1.0);
  EXPECT_NEAR(back.z, point.z, 1.0);
}

TEST(BursaWolfTest, BatchTransform) {
  BursaWolfParams params = {100.0, 200.0, 300.0, 0, 0, 0, 0};

  std::vector<glm::dvec3> input = {
      {1000000.0, 2000000.0, 3000000.0},
      {4000000.0, 5000000.0, 6000000.0},
  };
  std::vector<glm::dvec3> output(2);

  batchBursaWolfForward(input.data(), output.data(), 2, params);

  for (size_t i = 0; i < 2; ++i) {
    auto expected = bursaWolfForward(input[i], params);
    EXPECT_DOUBLE_EQ(output[i].x, expected.x);
    EXPECT_DOUBLE_EQ(output[i].y, expected.y);
    EXPECT_DOUBLE_EQ(output[i].z, expected.z);
  }
}

// ============================================================================
// 2D Helmert Tests
// ============================================================================

TEST(Helmert2DTest, IdentityTransform) {
  Helmert2DParams params = {0, 0, 0, 1.0};
  glm::dvec2 point(100.0, 200.0);
  auto result = helmert2DForward(point, params);
  EXPECT_NEAR(result.x, 100.0, 1e-10);
  EXPECT_NEAR(result.y, 200.0, 1e-10);
}

TEST(Helmert2DTest, PureTranslation) {
  Helmert2DParams params = {50.0, 100.0, 0, 1.0};
  glm::dvec2 point(100.0, 200.0);
  auto result = helmert2DForward(point, params);
  EXPECT_NEAR(result.x, 150.0, 1e-10);
  EXPECT_NEAR(result.y, 300.0, 1e-10);
}

TEST(Helmert2DTest, PureRotation90) {
  constexpr double PI = 3.14159265358979323846;
  Helmert2DParams params = {0, 0, PI / 2.0, 1.0};
  glm::dvec2 point(100.0, 0.0);
  auto result = helmert2DForward(point, params);
  EXPECT_NEAR(result.x, 0.0, 1e-6);
  EXPECT_NEAR(result.y, 100.0, 1e-6);
}

TEST(Helmert2DTest, ForwardInverseRoundtrip) {
  Helmert2DParams params = {50.0, 100.0, 0.1, 1.005};
  glm::dvec2 point(1234.5, 6789.0);
  auto forward = helmert2DForward(point, params);
  auto back = helmert2DInverse(forward, params);
  EXPECT_NEAR(back.x, point.x, 1e-8);
  EXPECT_NEAR(back.y, point.y, 1e-8);
}

// ============================================================================
// Parameter Estimation Tests
// ============================================================================

TEST(BursaWolfEstimationTest, RecoverPureTranslation) {
  BursaWolfParams true_params = {100.0, 200.0, 300.0, 0, 0, 0, 0};

  // Generate control points
  std::vector<ControlPointPair> points;
  for (double lat : {30.0, 35.0, 40.0, 45.0}) {
    auto source = geodeticToECEF(Geodetic::fromDegrees(lat, 116.0, 50.0));
    auto target = bursaWolfForward(source, true_params);
    points.push_back({source, target});
  }

  BursaWolfParams estimated;
  std::vector<double> residuals;
  ASSERT_TRUE(estimateBursaWolfParams(points, estimated, &residuals));

  EXPECT_NEAR(estimated.tx, 100.0, 0.01);
  EXPECT_NEAR(estimated.ty, 200.0, 0.01);
  EXPECT_NEAR(estimated.tz, 300.0, 0.01);
  EXPECT_NEAR(estimated.rx, 0.0, 0.01);
  EXPECT_NEAR(estimated.ry, 0.0, 0.01);
  EXPECT_NEAR(estimated.rz, 0.0, 0.01);
  EXPECT_NEAR(estimated.scale, 0.0, 0.01);

  // Residuals should be very small
  for (auto r : residuals) {
    EXPECT_LT(r, METER_TOL);
  }
}

TEST(BursaWolfEstimationTest, RecoverFullParams) {
  BursaWolfParams true_params = {10.0, -20.0, 30.0, 0.5, -0.3, 0.8, 2.5};

  std::vector<ControlPointPair> points;
  std::vector<glm::dvec3> test_locations = {
      geodeticToECEF(Geodetic::fromDegrees(30.0, 100.0, 0.0)),
      geodeticToECEF(Geodetic::fromDegrees(35.0, 110.0, 100.0)),
      geodeticToECEF(Geodetic::fromDegrees(40.0, 120.0, 500.0)),
      geodeticToECEF(Geodetic::fromDegrees(45.0, 115.0, 200.0)),
      geodeticToECEF(Geodetic::fromDegrees(25.0, 105.0, 50.0)),
  };

  for (const auto& loc : test_locations) {
    points.push_back({loc, bursaWolfForward(loc, true_params)});
  }

  BursaWolfParams estimated;
  std::vector<double> residuals;
  ASSERT_TRUE(estimateBursaWolfParams(points, estimated, &residuals));

  // Check parameter recovery
  EXPECT_NEAR(estimated.tx, true_params.tx, 0.1);
  EXPECT_NEAR(estimated.ty, true_params.ty, 0.1);
  EXPECT_NEAR(estimated.tz, true_params.tz, 0.1);

  // Residuals should be very small (perfect data)
  for (auto r : residuals) {
    EXPECT_LT(r, METER_TOL);
  }
}

TEST(BursaWolfEstimationTest, InsufficientPoints) {
  std::vector<ControlPointPair> points = {
      {{1e6, 2e6, 3e6}, {1e6 + 10, 2e6 + 20, 3e6 + 30}},
      {{4e6, 5e6, 6e6}, {4e6 + 10, 5e6 + 20, 6e6 + 30}},
  };

  BursaWolfParams params;
  EXPECT_FALSE(estimateBursaWolfParams(points, params));
}

TEST(Helmert2DEstimationTest, RecoverParams) {
  Helmert2DParams true_params = {50.0, 100.0, 0.01, 1.002};

  std::vector<ControlPointPair2D> points;
  for (double x : {0.0, 1000.0, 500.0, 2000.0}) {
    for (double y : {0.0, 1000.0}) {
      glm::dvec2 src(x, y);
      auto tgt = helmert2DForward(src, true_params);
      points.push_back({src, tgt});
    }
  }

  Helmert2DParams estimated;
  std::vector<double> residuals;
  ASSERT_TRUE(estimateHelmert2DParams(points, estimated, &residuals));

  EXPECT_NEAR(estimated.tx, true_params.tx, 0.001);
  EXPECT_NEAR(estimated.ty, true_params.ty, 0.001);
  EXPECT_NEAR(estimated.rotation, true_params.rotation, 1e-6);
  EXPECT_NEAR(estimated.scale, true_params.scale, 1e-6);

  for (auto r : residuals) {
    EXPECT_LT(r, 0.001);
  }
}

TEST(Helmert2DEstimationTest, InsufficientPoints) {
  std::vector<ControlPointPair2D> points = {
      {{0, 0}, {10, 20}},
  };

  Helmert2DParams params;
  EXPECT_FALSE(estimateHelmert2DParams(points, params));
}
