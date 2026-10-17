#include <gtest/gtest.h>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <cmath>
#include <vector>

using namespace hongtu::coordinate;

TEST(RTCOriginTest, SetAndGet) {
  RTCOrigin rtc;
  auto geo = Geodetic::fromDegrees(39.9042, 116.4074, 50.0);
  rtc.setOriginGeodetic(geo);

  EXPECT_NEAR(rtc.getOriginGeodetic().lat, geo.lat, 1e-10);
  EXPECT_GT(glm::length(rtc.getOriginECEF()), 6e6); // Should be ~6.3M meters
}

TEST(RTCOriginTest, OriginToRTCIsZero) {
  RTCOrigin rtc;
  auto geo = Geodetic::fromDegrees(39.9042, 116.4074, 50.0);
  rtc.setOriginGeodetic(geo);

  auto rtc_pos = rtc.ecefToRTC(rtc.getOriginECEF());
  EXPECT_NEAR(rtc_pos.x, 0.0f, 1e-3f);
  EXPECT_NEAR(rtc_pos.y, 0.0f, 1e-3f);
  EXPECT_NEAR(rtc_pos.z, 0.0f, 1e-3f);
}

TEST(RTCOriginTest, RoundtripPrecision) {
  RTCOrigin rtc;
  rtc.setOriginGeodetic(Geodetic::fromDegrees(39.9042, 116.4074, 50.0));

  // A point 1km away
  auto nearby = geodeticToECEF(Geodetic::fromDegrees(39.91, 116.41, 50.0));
  auto rtc_pos = rtc.ecefToRTC(nearby);
  auto back = rtc.rtcToECEF(rtc_pos);

  // Should round-trip within 0.001m at 1km distance
  EXPECT_NEAR(back.x, nearby.x, 0.001);
  EXPECT_NEAR(back.y, nearby.y, 0.001);
  EXPECT_NEAR(back.z, nearby.z, 0.001);
}

TEST(RTCOriginTest, PrecisionAt1000km) {
  RTCOrigin rtc;
  rtc.setOriginGeodetic(Geodetic::fromDegrees(39.9042, 116.4074, 50.0));

  // A point ~1000km away (Shanghai)
  auto far_point = geodeticToECEF(Geodetic::fromDegrees(31.2304, 121.4737, 4.0));
  auto rtc_pos = rtc.ecefToRTC(far_point);
  auto back = rtc.rtcToECEF(rtc_pos);

  // At 1000km, float32 has ~0.1m precision. Roundtrip should be within 1m.
  double error = glm::length(back - far_point);
  EXPECT_LT(error, 1.0);
}

TEST(RTCOriginTest, BatchConversion) {
  RTCOrigin rtc;
  rtc.setOriginGeodetic(Geodetic::fromDegrees(0, 0, 0));

  std::vector<glm::dvec3> ecef_in(100);
  std::vector<glm::vec3> rtc_out(100);

  for (int i = 0; i < 100; ++i) {
    ecef_in[i] = geodeticToECEF(Geodetic::fromDegrees(
        static_cast<double>(i) * 0.1, static_cast<double>(i) * 0.2, 0.0));
  }

  rtc.batchECEFToRTC(ecef_in.data(), rtc_out.data(), 100);

  for (int i = 0; i < 100; ++i) {
    auto single = rtc.ecefToRTC(ecef_in[i]);
    EXPECT_FLOAT_EQ(rtc_out[i].x, single.x);
    EXPECT_FLOAT_EQ(rtc_out[i].y, single.y);
    EXPECT_FLOAT_EQ(rtc_out[i].z, single.z);
  }
}

TEST(RTCOriginTest, PrecisionEstimation) {
  // At the origin, precision should be very high
  float p0 = RTCOrigin::precisionAtDistance(0.0);
  EXPECT_LT(p0, 1e-6f);

  // At 1000km, precision should be ~0.06m
  float p1000 = RTCOrigin::precisionAtDistance(1000000.0);
  EXPECT_GT(p1000, 0.0f);
  EXPECT_LT(p1000, 1.0f);

  // Precision degrades with distance
  float p10 = RTCOrigin::precisionAtDistance(10000.0);
  float p100 = RTCOrigin::precisionAtDistance(100000.0);
  EXPECT_LT(p10, p100);
}

TEST(RTCOriginTest, SafeZone) {
  RTCOrigin rtc;
  rtc.setOriginGeodetic(Geodetic::fromDegrees(39.9042, 116.4074, 50.0));

  // Nearby point should be in safe zone
  auto nearby = geodeticToECEF(Geodetic::fromDegrees(39.91, 116.41, 50.0));
  EXPECT_TRUE(rtc.isInSafeZone(nearby));

  // Origin itself should be in safe zone
  EXPECT_TRUE(rtc.isInSafeZone(rtc.getOriginECEF()));
}

TEST(RTCOriginTest, ShouldRecenter) {
  RTCOrigin rtc;
  rtc.setOriginGeodetic(Geodetic::fromDegrees(0, 0, 0));

  // Camera at origin: no recenter needed
  EXPECT_FALSE(rtc.shouldRecenter(rtc.getOriginECEF()));

  // Camera very far away: should recenter
  auto far_cam = geodeticToECEF(Geodetic::fromDegrees(45, 90, 0));
  EXPECT_TRUE(rtc.shouldRecenter(far_cam, 100000.0));
}

TEST(RTCOriginTest, ViewMatrix) {
  RTCOrigin rtc;
  rtc.setOriginGeodetic(Geodetic::fromDegrees(39.9, 116.4, 0));

  // Camera 1km above and 1km north of origin
  auto camera = geodeticToECEF(Geodetic::fromDegrees(39.91, 116.4, 1000));
  auto target = geodeticToECEF(Geodetic::fromDegrees(39.9, 116.4, 0));

  auto view = rtc.computeRTCViewMatrix(camera, target, {0, 1, 0});
  // View matrix should have non-zero determinant
  float det = glm::determinant(view);
  EXPECT_NE(det, 0.0f);
}
