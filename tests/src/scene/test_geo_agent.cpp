#include <gtest/gtest.h>
#include <hongtu/scene/agent.hpp>
#include <hongtu/coordinate/geodetic.hpp>

using hongtu::scene::GeoAgent;
using hongtu::coordinate::Geodetic;
using hongtu::coordinate::geodeticToECEF;

namespace { constexpr double kEps = 1e-3; }

TEST(GeoAgentTest, DefaultPositionAtOrigin) {
  GeoAgent a;
  // Default Geodetic{0,0,0} → ECEF on equator at lon=0.
  auto w = a.getWorldPosition();
  EXPECT_NEAR(w.x, 6378137.0, 1e-3);   // WGS84 equatorial radius
}

TEST(GeoAgentTest, SetPositionGeoUpdatesWorldPos) {
  GeoAgent a;
  Geodetic g = Geodetic::fromDegrees(39.9042, 116.4074);
  a.setPositionGeo(g);
  auto w = a.getWorldPosition();
  auto expected = geodeticToECEF(g);
  EXPECT_NEAR(w.x, expected.x, kEps);
  EXPECT_NEAR(w.y, expected.y, kEps);
  EXPECT_NEAR(w.z, expected.z, kEps);
}

TEST(GeoAgentTest, ModelMatrixTranslationIsECEFPos) {
  GeoAgent a;
  Geodetic g = Geodetic::fromDegrees(39.9042, 116.4074, 5.0);
  a.setPositionGeo(g);
  auto m = a.getModelMatrix();
  auto ecef = geodeticToECEF(g);
  EXPECT_NEAR(m[3][0], ecef.x, kEps);
  EXPECT_NEAR(m[3][1], ecef.y, kEps);
  EXPECT_NEAR(m[3][2], ecef.z, kEps);
}

TEST(GeoAgentTest, IntegrateMovesGeodeticPosition) {
  GeoAgent a;
  Geodetic g0 = Geodetic::fromDegrees(39.9042, 116.4074, 0.0);
  a.setPositionGeo(g0);

  // Velocity 1 m/s eastward in ECEF — for this latitude, ECEF +X
  // doesn't point east; we just integrate and verify position changed.
  auto ecef0 = a.getWorldPosition();
  a.integrate({1.0, 0.0, 0.0}, 1.0f);
  auto ecef1 = a.getWorldPosition();
  EXPECT_NEAR(ecef1.x, ecef0.x + 1.0, 1e-2);
}

TEST(GeoAgentTest, ModelMatrixZAxisAlignedToUp) {
  // Agent +Z (column 2) should align with the geodetic "up" direction
  // (radial from earth center to position).
  GeoAgent a;
  Geodetic g = Geodetic::fromDegrees(0.0, 0.0);   // equator @ lon=0
  a.setPositionGeo(g);
  auto m = a.getModelMatrix();
  // At lat=0/lon=0, up = (1, 0, 0) in ECEF.
  EXPECT_NEAR(m[2][0], 1.0, kEps);
  EXPECT_NEAR(m[2][1], 0.0, kEps);
  EXPECT_NEAR(m[2][2], 0.0, kEps);
}
