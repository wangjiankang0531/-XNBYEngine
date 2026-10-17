// Tests for scene::GeoPolyline — verifies geodetic → ECEF → RTC float32
// conversion against PrecisionOrigin. The arithmetic correctness of
// geodeticToECEF itself is covered by coordinate/ tests; here we only check
// that:
//   - the point at the same geodetic coord as the origin lands at RTC = (0,0,0)
//   - a nearby geodetic point lands at a small RTC offset (no precision loss)
//   - empty input gives empty output

#include <gtest/gtest.h>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/scene/polyline_entity.hpp>
#include <glm/glm.hpp>

#include <cmath>

using hongtu::coordinate::Geodetic;
using hongtu::coordinate::PrecisionOrigin;
using hongtu::coordinate::WGS84;
using hongtu::coordinate::geodeticToECEF;
using hongtu::scene::GeoPolyline;

namespace {
constexpr float kEps = 1e-2f;  // 1cm tolerance on RTC float32
}

TEST(GeoPolylineTest, DefaultsAreSensible) {
  GeoPolyline p;
  EXPECT_EQ(p.getColor(), glm::vec4(1, 1, 1, 1));
  EXPECT_FLOAT_EQ(p.getWidthMeters(), 1.0f);
  EXPECT_FALSE(p.isClosed());
  EXPECT_TRUE(p.getPoints().empty());
}

TEST(GeoPolylineTest, PointAtOriginLandsAtRTCZero) {
  // Set origin at Beijing (~39.9°N, 116.4°E, 0m). Single polyline point at
  // the exact same geodetic coord should give RTC ≈ (0, 0, 0).
  Geodetic beijing = Geodetic::fromDegrees(39.9042, 116.4074, 0.0);
  PrecisionOrigin origin;
  origin.setOriginGeodetic(beijing);

  GeoPolyline p;
  p.addPoint(beijing);
  auto rtc = p.getRTCPoints(origin);
  ASSERT_EQ(rtc.size(), 1u);
  EXPECT_NEAR(rtc[0].x, 0.0f, kEps);
  EXPECT_NEAR(rtc[0].y, 0.0f, kEps);
  EXPECT_NEAR(rtc[0].z, 0.0f, kEps);
}

TEST(GeoPolylineTest, NearbyPointLandsAtSmallOffset) {
  // Origin at Beijing. A point 10 meters higher (same lat/lon, alt+10).
  // The RTC offset magnitude should be ~10 m (the direction is along the
  // ellipsoid normal — close to ECEF "up").
  Geodetic beijing       = Geodetic::fromDegrees(39.9042, 116.4074, 0.0);
  Geodetic beijing_above = Geodetic::fromDegrees(39.9042, 116.4074, 10.0);
  PrecisionOrigin origin;
  origin.setOriginGeodetic(beijing);

  GeoPolyline p;
  p.addPoint(beijing_above);
  auto rtc = p.getRTCPoints(origin);
  ASSERT_EQ(rtc.size(), 1u);
  float len = std::sqrt(rtc[0].x * rtc[0].x +
                        rtc[0].y * rtc[0].y +
                        rtc[0].z * rtc[0].z);
  EXPECT_NEAR(len, 10.0f, 0.01f);
}

TEST(GeoPolylineTest, MultiplePointsPreserveSpacing) {
  // Two points 100m apart vertically. RTC distance between their float32
  // offsets should still be ~100m (precision loss < 1cm at this scale).
  Geodetic p1 = Geodetic::fromDegrees(31.2304, 121.4737, 0.0);     // Shanghai
  Geodetic p2 = Geodetic::fromDegrees(31.2304, 121.4737, 100.0);
  PrecisionOrigin origin;
  origin.setOriginGeodetic(p1);

  GeoPolyline poly;
  poly.addPoint(p1);
  poly.addPoint(p2);
  auto rtc = poly.getRTCPoints(origin);
  ASSERT_EQ(rtc.size(), 2u);
  glm::vec3 d = rtc[1] - rtc[0];
  float len = std::sqrt(glm::dot(d, d));
  EXPECT_NEAR(len, 100.0f, 0.01f);
}

TEST(GeoPolylineTest, EmptyPolylineProducesEmptyRTC) {
  PrecisionOrigin origin;
  origin.setOriginGeodetic(Geodetic::fromDegrees(0, 0, 0));
  GeoPolyline poly;
  EXPECT_TRUE(poly.getRTCPoints(origin).empty());
}
