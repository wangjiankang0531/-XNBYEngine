#include <gtest/gtest.h>

#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/renderer/globe/water_surface_renderer.hpp>

#include <glm/glm.hpp>

using hongtu::coordinate::Geodetic;
using hongtu::coordinate::RTCOrigin;
using hongtu::coordinate::geodeticToECEF;
using hongtu::renderer::buildWaterSurfaceRtcVertices;

namespace {

// A small lake quad near Hong Kong at 30 m water level.
std::vector<Geodetic> lakeQuad() {
  return {Geodetic::fromDegrees(22.40, 114.00, 30.0),
          Geodetic::fromDegrees(22.40, 114.02, 30.0),
          Geodetic::fromDegrees(22.42, 114.02, 30.0),
          Geodetic::fromDegrees(22.42, 114.00, 30.0)};
}

RTCOrigin originAt(const Geodetic& geo) {
  RTCOrigin o;
  o.setOriginGeodetic(geo);
  return o;
}

}  // namespace

TEST(WaterSurface, DegenerateBoundaryIsEmpty) {
  RTCOrigin origin = originAt(Geodetic::fromDegrees(22.4, 114.0, 0.0));
  EXPECT_TRUE(buildWaterSurfaceRtcVertices({}, origin).empty());
  EXPECT_TRUE(buildWaterSurfaceRtcVertices(
                  {Geodetic::fromDegrees(22.4, 114.0, 0.0),
                   Geodetic::fromDegrees(22.5, 114.0, 0.0)},
                  origin)
                  .empty());
}

TEST(WaterSurface, QuadFansIntoTwoTriangles) {
  RTCOrigin origin = originAt(Geodetic::fromDegrees(22.41, 114.01, 30.0));
  auto verts = buildWaterSurfaceRtcVertices(lakeQuad(), origin);
  // Fan over 4 boundary vertices → 2 triangles → 6 vertices.
  ASSERT_EQ(verts.size(), 6u);
}

TEST(WaterSurface, RtcVerticesStaySmallNearOrigin) {
  // With the origin at the lake, RTC magnitudes are a few km — well inside
  // float32 precision (the whole point of RTC; see geo-rtc-precision memory).
  RTCOrigin origin = originAt(Geodetic::fromDegrees(22.41, 114.01, 30.0));
  auto verts = buildWaterSurfaceRtcVertices(lakeQuad(), origin);
  ASSERT_FALSE(verts.empty());
  for (const auto& v : verts) {
    EXPECT_LT(glm::length(v), 5000.0f) << "vertex unexpectedly far from origin";
  }
}

TEST(WaterSurface, ReconstructsExactEcefPositions) {
  // vertex_rtc + origin_ecef must reproduce geodeticToECEF(boundary[i]) — the
  // first fan triangle starts at boundary[0], so check that one.
  RTCOrigin origin = originAt(Geodetic::fromDegrees(22.41, 114.01, 30.0));
  auto boundary = lakeQuad();
  auto verts = buildWaterSurfaceRtcVertices(boundary, origin);
  ASSERT_FALSE(verts.empty());

  glm::dvec3 reconstructed = glm::dvec3(verts[0]) + origin.getOriginECEF();
  glm::dvec3 truth = geodeticToECEF(boundary[0]);
  EXPECT_NEAR(reconstructed.x, truth.x, 0.5);
  EXPECT_NEAR(reconstructed.y, truth.y, 0.5);
  EXPECT_NEAR(reconstructed.z, truth.z, 0.5);
}

TEST(WaterSurface, WaterLevelRaisesSurface) {
  // Same footprint at 0 m vs 100 m → vertices differ by ~100 m radially.
  RTCOrigin origin = originAt(Geodetic::fromDegrees(22.41, 114.01, 0.0));
  auto low  = lakeQuad();
  auto high = lakeQuad();
  for (auto& g : low)  g.alt = 0.0;
  for (auto& g : high) g.alt = 100.0;

  auto v_low  = buildWaterSurfaceRtcVertices(low, origin);
  auto v_high = buildWaterSurfaceRtcVertices(high, origin);
  ASSERT_EQ(v_low.size(), v_high.size());
  ASSERT_FALSE(v_low.empty());

  float d = glm::length(v_high[0] - v_low[0]);
  EXPECT_NEAR(d, 100.0f, 1.0f);
}
