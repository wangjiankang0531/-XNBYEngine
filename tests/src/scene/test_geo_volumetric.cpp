// GeoVolumetric tests — geodetic 中心 → ECEF → RTC.

#include <gtest/gtest.h>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/scene/volumetric_entity.hpp>

using hongtu::coordinate::Geodetic;
using hongtu::coordinate::PrecisionOrigin;
using hongtu::scene::GeoVolumetric;

namespace { constexpr float kCm = 0.01f; }

TEST(GeoVolumetricTest, SameAsOriginLandsAtZero) {
  Geodetic beijing = Geodetic::fromDegrees(39.9042, 116.4074, 0.0);
  PrecisionOrigin o; o.setOriginGeodetic(beijing);
  GeoVolumetric v;
  v.setCenter(beijing);
  glm::vec3 rtc = v.getRTCCenter(o);
  EXPECT_NEAR(rtc.x, 0.0f, kCm);
  EXPECT_NEAR(rtc.y, 0.0f, kCm);
  EXPECT_NEAR(rtc.z, 0.0f, kCm);
}

TEST(GeoVolumetricTest, FiftyMetersAltitudeDistance) {
  Geodetic ground = Geodetic::fromDegrees(31.2304, 121.4737, 0.0);
  Geodetic above  = Geodetic::fromDegrees(31.2304, 121.4737, 50.0);
  PrecisionOrigin o; o.setOriginGeodetic(ground);
  GeoVolumetric v;
  v.setCenter(above);
  glm::vec3 rtc = v.getRTCCenter(o);
  float len = std::sqrt(rtc.x*rtc.x + rtc.y*rtc.y + rtc.z*rtc.z);
  EXPECT_NEAR(len, 50.0f, kCm);
}
