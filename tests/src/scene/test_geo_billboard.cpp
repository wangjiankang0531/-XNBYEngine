// GeoBillboard tests — geodetic → ECEF → RTC float32, accuracy at large ECEF
// magnitudes (~6.4e6 m).

#include <gtest/gtest.h>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/scene/billboard_entity.hpp>

#include <cmath>

using hongtu::coordinate::Geodetic;
using hongtu::coordinate::PrecisionOrigin;
using hongtu::scene::GeoBillboard;

namespace { constexpr float kCm = 0.01f; }

TEST(GeoBillboardTest, Defaults) {
  GeoBillboard b;
  EXPECT_EQ(b.getColor(), glm::vec4(1, 1, 1, 1));
}

TEST(GeoBillboardTest, SameAsOriginLandsAtZero) {
  Geodetic beijing = Geodetic::fromDegrees(39.9042, 116.4074, 0.0);
  PrecisionOrigin origin; origin.setOriginGeodetic(beijing);

  GeoBillboard b;
  b.setPosition(beijing);
  glm::vec3 rtc = b.getRTCPosition(origin);
  EXPECT_NEAR(rtc.x, 0.0f, kCm);
  EXPECT_NEAR(rtc.y, 0.0f, kCm);
  EXPECT_NEAR(rtc.z, 0.0f, kCm);
}

TEST(GeoBillboardTest, AboveOriginByTenMeters) {
  Geodetic ground = Geodetic::fromDegrees(31.2304, 121.4737, 0.0);  // Shanghai
  Geodetic above  = Geodetic::fromDegrees(31.2304, 121.4737, 10.0);
  PrecisionOrigin origin; origin.setOriginGeodetic(ground);

  GeoBillboard b;
  b.setPosition(above);
  glm::vec3 rtc = b.getRTCPosition(origin);
  float len = std::sqrt(rtc.x*rtc.x + rtc.y*rtc.y + rtc.z*rtc.z);
  EXPECT_NEAR(len, 10.0f, kCm);
}

TEST(GeoBillboardTest, OneKmNorthHasExpectedDistance) {
  // ~1km north shift in latitude. 1° lat ≈ 111km, so 1/111° ≈ 1km.
  Geodetic origin_geo = Geodetic::fromDegrees(0.0, 0.0, 0.0);
  Geodetic north_1km  = Geodetic::fromDegrees(1.0 / 111.0, 0.0, 0.0);
  PrecisionOrigin origin; origin.setOriginGeodetic(origin_geo);

  GeoBillboard b;
  b.setPosition(north_1km);
  glm::vec3 rtc = b.getRTCPosition(origin);
  float len = std::sqrt(rtc.x*rtc.x + rtc.y*rtc.y + rtc.z*rtc.z);
  EXPECT_NEAR(len, 1000.0f, 100.0f);  // 10% slop — 111km is an approximation
}
