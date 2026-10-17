// GeoEmitter tests — geodetic → ECEF → RTC.

#include <gtest/gtest.h>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/scene/emitter_entity.hpp>

#include <cmath>

using hongtu::coordinate::Geodetic;
using hongtu::coordinate::PrecisionOrigin;
using hongtu::scene::GeoEmitter;

namespace { constexpr float kCm = 0.01f; }

TEST(GeoEmitterTest, SameAsOriginLandsAtZero) {
  Geodetic beijing = Geodetic::fromDegrees(39.9042, 116.4074, 0.0);
  PrecisionOrigin o; o.setOriginGeodetic(beijing);
  GeoEmitter e;
  e.setPosition(beijing);
  glm::vec3 rtc = e.getRTCPosition(o);
  EXPECT_NEAR(rtc.x, 0.0f, kCm);
  EXPECT_NEAR(rtc.y, 0.0f, kCm);
  EXPECT_NEAR(rtc.z, 0.0f, kCm);
}

TEST(GeoEmitterTest, OneHundredMetersUpDistance) {
  Geodetic ground = Geodetic::fromDegrees(31.2304, 121.4737, 0.0);  // Shanghai
  Geodetic above  = Geodetic::fromDegrees(31.2304, 121.4737, 100.0);
  PrecisionOrigin o; o.setOriginGeodetic(ground);
  GeoEmitter e;
  e.setPosition(above);
  glm::vec3 rtc = e.getRTCPosition(o);
  float len = std::sqrt(rtc.x*rtc.x + rtc.y*rtc.y + rtc.z*rtc.z);
  EXPECT_NEAR(len, 100.0f, kCm);
}
