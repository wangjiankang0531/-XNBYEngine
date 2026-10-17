// ShockwaveEntity tests — radius growth math + dual-mode RTC.

#include <gtest/gtest.h>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/scene/shockwave_entity.hpp>

using hongtu::coordinate::Geodetic;
using hongtu::coordinate::PrecisionOrigin;
using hongtu::scene::GeoShockwave;
using hongtu::scene::LocalShockwave;

namespace { constexpr float kEps = 1e-5f; }

TEST(ShockwaveEntityTest, RadiusZeroBeforeStartTime) {
  LocalShockwave s;
  s.setStartTimeSec(5.0f);
  s.setSpeedMps(10.0f);
  EXPECT_FLOAT_EQ(s.getCurrentRadius(4.0f), 0.0f);
  EXPECT_FLOAT_EQ(s.getCurrentRadius(5.0f), 0.0f);
}

TEST(ShockwaveEntityTest, RadiusLinearWithTime) {
  LocalShockwave s;
  s.setStartTimeSec(0.0f);
  s.setSpeedMps(20.0f);
  s.setMaxRadiusMeters(1000.0f);
  EXPECT_FLOAT_EQ(s.getCurrentRadius(1.0f), 20.0f);
  EXPECT_FLOAT_EQ(s.getCurrentRadius(2.5f), 50.0f);
}

TEST(ShockwaveEntityTest, RadiusClampsAtMax) {
  LocalShockwave s;
  s.setStartTimeSec(0.0f);
  s.setSpeedMps(10.0f);
  s.setMaxRadiusMeters(30.0f);
  EXPECT_FLOAT_EQ(s.getCurrentRadius(2.0f), 20.0f);
  EXPECT_FLOAT_EQ(s.getCurrentRadius(10.0f), 30.0f);   // clamped
  EXPECT_TRUE(s.isComplete(10.0f));
  EXPECT_FALSE(s.isComplete(2.5f));
}

TEST(LocalShockwaveTest, RTCConversion) {
  PrecisionOrigin o; o.setOrigin({0, 0, 0});
  LocalShockwave s;
  s.setCenter({3.0, -2.0, 1.0});
  glm::vec3 c = s.getRTCCenter(o);
  EXPECT_NEAR(c.x, 3.0f, kEps);
  EXPECT_NEAR(c.y, -2.0f, kEps);
  EXPECT_NEAR(c.z, 1.0f, kEps);
}

TEST(GeoShockwaveTest, RTCSameAsOriginIsZero) {
  Geodetic beijing = Geodetic::fromDegrees(39.9042, 116.4074, 0.0);
  PrecisionOrigin o; o.setOriginGeodetic(beijing);
  GeoShockwave s;
  s.setCenter(beijing);
  glm::vec3 c = s.getRTCCenter(o);
  EXPECT_NEAR(c.x, 0.0f, 0.01f);
  EXPECT_NEAR(c.y, 0.0f, 0.01f);
  EXPECT_NEAR(c.z, 0.0f, 0.01f);
}
