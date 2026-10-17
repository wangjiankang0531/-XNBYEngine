#include <gtest/gtest.h>

#include <hongtu/coordinate/enu_frame.hpp>
#include <hongtu/coordinate/geodetic.hpp>

using namespace hongtu::coordinate;

namespace {
constexpr double kMTol  = 0.01;     // 1 cm
constexpr double kDegTol = 1e-6;    // 经纬度容差(度)

struct Site { double lat_deg, lon_deg, alt_m; };
const Site kSites[] = {
  {  0.0,    0.0,   0.0},
  { 33.5,  104.6,  50.0},
  {-33.9,  151.2, 120.0},
  { 80.0,  -45.0, 500.0},
  { 39.9,  116.4,   0.0},
};
}  // namespace

// FRAME-IDENTITY: local 原点复合回锚点。
TEST(EnuFrame, IdentityAtOrigin) {
  for (const auto& s : kSites) {
    auto a = Geodetic::fromDegrees(s.lat_deg, s.lon_deg, s.alt_m);
    auto f = enuFrameAt(a);
    glm::dvec3 ecef = f.localToEcef(glm::dvec3(0.0));
    auto g = ecefToGeodetic(ecef);
    EXPECT_NEAR(glm::degrees(g.lat), s.lat_deg, kDegTol) << s.lat_deg;
    EXPECT_NEAR(glm::degrees(g.lon), s.lon_deg, kDegTol) << s.lon_deg;
    EXPECT_NEAR(g.alt, s.alt_m, kMTol);
  }
}

// FRAME-UP: local +Z 抬升 → geodetic alt 增加。
TEST(EnuFrame, UpIncreasesAltitude) {
  for (const auto& s : kSites) {
    auto a = Geodetic::fromDegrees(s.lat_deg, s.lon_deg, s.alt_m);
    auto f = enuFrameAt(a);
    auto g = ecefToGeodetic(f.localToEcef(glm::dvec3(0.0, 0.0, 100.0)));
    EXPECT_NEAR(g.alt, s.alt_m + 100.0, 0.05);
  }
}

// FRAME-EAST/NORTH: 本地 +X 朝东、+Y 朝北。
TEST(EnuFrame, AxesPointEastNorth) {
  for (const auto& s : kSites) {
    auto a = Geodetic::fromDegrees(s.lat_deg, s.lon_deg, s.alt_m);
    auto f = enuFrameAt(a);
    glm::dvec3 dx = glm::normalize(f.localToEcef(glm::dvec3(1, 0, 0)) - f.origin_ecef);
    glm::dvec3 dy = glm::normalize(f.localToEcef(glm::dvec3(0, 1, 0)) - f.origin_ecef);
    EXPECT_GT(glm::dot(dx, f.east), 0.999);
    EXPECT_GT(glm::dot(dy, f.north), 0.999);
    // 正交性
    EXPECT_NEAR(glm::dot(f.east, f.north), 0.0, 1e-9);
    EXPECT_NEAR(glm::dot(f.east, f.up),    0.0, 1e-9);
    EXPECT_NEAR(glm::dot(f.north, f.up),   0.0, 1e-9);
  }
}

// FRAME-ROUNDTRIP: ecefToLocal(localToEcef(v)) == v。
TEST(EnuFrame, LocalEcefRoundTrip) {
  auto a = Geodetic::fromDegrees(33.5, 104.6, 50.0);
  auto f = enuFrameAt(a);
  const glm::dvec3 v(12.0, -7.5, 3.2);
  glm::dvec3 back = f.ecefToLocal(f.localToEcef(v));
  EXPECT_NEAR(back.x, v.x, 1e-6);
  EXPECT_NEAR(back.y, v.y, 1e-6);
  EXPECT_NEAR(back.z, v.z, 1e-6);
}
