// Tests for scene::LocalPolyline — verifies dvec3 → RTC float32 conversion
// against PrecisionOrigin and the basic API (color/width/closed).

#include <gtest/gtest.h>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/scene/polyline_entity.hpp>
#include <glm/glm.hpp>

#include <cmath>

using hongtu::coordinate::PrecisionOrigin;
using hongtu::scene::LocalPolyline;

namespace {
constexpr float kEps = 1e-5f;
}

TEST(LocalPolylineTest, DefaultsAreSensible) {
  LocalPolyline p;
  EXPECT_EQ(p.getColor(), glm::vec4(1, 1, 1, 1));
  EXPECT_FLOAT_EQ(p.getWidthMeters(), 1.0f);
  EXPECT_FALSE(p.isClosed());
  EXPECT_TRUE(p.getPoints().empty());
}

TEST(LocalPolylineTest, RTCConversionAtZeroOrigin) {
  PrecisionOrigin origin;
  origin.setOrigin({0.0, 0.0, 0.0});

  LocalPolyline poly;
  poly.addPoint({1.0, 2.0, 3.0});
  poly.addPoint({4.0, 5.0, 6.0});

  auto rtc = poly.getRTCPoints(origin);
  ASSERT_EQ(rtc.size(), 2u);
  EXPECT_NEAR(rtc[0].x, 1.0f, kEps);
  EXPECT_NEAR(rtc[0].y, 2.0f, kEps);
  EXPECT_NEAR(rtc[0].z, 3.0f, kEps);
  EXPECT_NEAR(rtc[1].x, 4.0f, kEps);
  EXPECT_NEAR(rtc[1].y, 5.0f, kEps);
  EXPECT_NEAR(rtc[1].z, 6.0f, kEps);
}

TEST(LocalPolylineTest, RTCConversionShiftsByOrigin) {
  // Origin at (1000, 2000, 3000). A polyline at (1001, 2001, 3001) should
  // produce RTC (1, 1, 1) — the float32 offset survives because the magnitude
  // is small even though the world-space numbers are large.
  PrecisionOrigin origin;
  origin.setOrigin({1000.0, 2000.0, 3000.0});

  LocalPolyline poly;
  poly.addPoint({1001.0, 2001.0, 3001.0});
  poly.addPoint({1002.0, 2002.0, 3002.0});

  auto rtc = poly.getRTCPoints(origin);
  ASSERT_EQ(rtc.size(), 2u);
  EXPECT_NEAR(rtc[0].x, 1.0f, kEps);
  EXPECT_NEAR(rtc[0].y, 1.0f, kEps);
  EXPECT_NEAR(rtc[0].z, 1.0f, kEps);
  EXPECT_NEAR(rtc[1].x, 2.0f, kEps);
  EXPECT_NEAR(rtc[1].y, 2.0f, kEps);
  EXPECT_NEAR(rtc[1].z, 2.0f, kEps);
}

TEST(LocalPolylineTest, EmptyPolylineProducesEmptyRTC) {
  PrecisionOrigin origin;
  origin.setOrigin({0.0, 0.0, 0.0});
  LocalPolyline poly;
  EXPECT_TRUE(poly.getRTCPoints(origin).empty());
}

TEST(LocalPolylineTest, SettersPropagate) {
  LocalPolyline p;
  p.setColor({0.1f, 0.2f, 0.3f, 0.4f});
  p.setWidthMeters(2.5f);
  p.setClosed(true);
  p.setPoints({{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {1.0, 1.0, 0.0}});
  EXPECT_FLOAT_EQ(p.getColor().r, 0.1f);
  EXPECT_FLOAT_EQ(p.getWidthMeters(), 2.5f);
  EXPECT_TRUE(p.isClosed());
  EXPECT_EQ(p.getPoints().size(), 3u);
}
