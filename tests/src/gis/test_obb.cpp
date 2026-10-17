#include <gtest/gtest.h>
#include <hongtu/gis/tile/obb.hpp>
#include <hongtu/coordinate/ellipsoid.hpp>
#include <hongtu/coordinate/geodetic.hpp>

using namespace hongtu;
using namespace hongtu::gis;
using namespace hongtu::coordinate;

TEST(OBB, FromUnitBoxAtEquator) {
  // A small box centered on (lat=0, lon=0) ±0.001 rad, alt 0..100.
  GeoBounds b{ -0.001, -0.001, 0.001, 0.001 };  // west, south, east, north
  OBB obb = lonLatAltBoxToOBB(b, 0.0, 100.0, WGS84);

  // Center should be near the WGS84 ellipsoid surface at (0,0): ~6378137 m on +x.
  EXPECT_NEAR(obb.center.x, 6378137.0 + 50.0, 200.0);
  EXPECT_NEAR(obb.center.y, 0.0, 1.0);
  EXPECT_NEAR(obb.center.z, 0.0, 1.0);
  // Extent must be positive in all axes.
  EXPECT_GT(obb.half_extent.x, 0.0);
  EXPECT_GT(obb.half_extent.y, 0.0);
  EXPECT_GT(obb.half_extent.z, 0.0);
}

TEST(OBB, ClosestPointInsideReturnsItself) {
  OBB obb;
  obb.center = glm::dvec3(0, 0, 0);
  obb.axes[0] = glm::dvec3(1, 0, 0);
  obb.axes[1] = glm::dvec3(0, 1, 0);
  obb.axes[2] = glm::dvec3(0, 0, 1);
  obb.half_extent = glm::dvec3(10, 10, 10);

  glm::dvec3 p(2, 3, 4);
  auto out = closestPointOnOBB(p, obb);
  EXPECT_NEAR(out.x, 2.0, 1e-9);
  EXPECT_NEAR(out.y, 3.0, 1e-9);
  EXPECT_NEAR(out.z, 4.0, 1e-9);
}

TEST(OBB, ClosestPointOutsideClampsToFace) {
  OBB obb;
  obb.center = glm::dvec3(0, 0, 0);
  obb.axes[0] = glm::dvec3(1, 0, 0);
  obb.axes[1] = glm::dvec3(0, 1, 0);
  obb.axes[2] = glm::dvec3(0, 0, 1);
  obb.half_extent = glm::dvec3(10, 10, 10);

  glm::dvec3 p(50, 0, 0);
  auto out = closestPointOnOBB(p, obb);
  EXPECT_NEAR(out.x, 10.0, 1e-9);  // clamped to +x face
  EXPECT_NEAR(out.y, 0.0, 1e-9);
  EXPECT_NEAR(out.z, 0.0, 1e-9);
}

TEST(OBB, FrustumIntersectInside) {
  OBB obb;
  obb.center = glm::dvec3(0, 0, -5);
  obb.axes[0] = glm::dvec3(1, 0, 0);
  obb.axes[1] = glm::dvec3(0, 1, 0);
  obb.axes[2] = glm::dvec3(0, 0, 1);
  obb.half_extent = glm::dvec3(0.5, 0.5, 0.5);

  // Identity-ish view-proj that keeps z in [-10, -1] visible
  Frustum f;
  // 6 planes: ax+by+cz+d >= 0 for "inside"
  // We construct a frustum centered on origin looking down -z:
  // near at z = -1, far at z = -10, sides at x=±5, y=±5 (orthographic-ish).
  f.planes[0] = glm::dvec4( 1, 0, 0, 5);   // left:   x >= -5
  f.planes[1] = glm::dvec4(-1, 0, 0, 5);   // right:  x <=  5
  f.planes[2] = glm::dvec4(0,  1, 0, 5);   // bottom: y >= -5
  f.planes[3] = glm::dvec4(0, -1, 0, 5);   // top:    y <=  5
  f.planes[4] = glm::dvec4(0, 0,-1,-1);  // near: z <= -1 (observer looks toward -z)
  f.planes[5] = glm::dvec4(0, 0, 1, 10); // far:    z >= -10 → z + 10 >= 0

  auto inter = frustumIntersectOBB(f, obb);
  EXPECT_EQ(inter, FrustumIntersection::Inside);
}

TEST(OBB, FrustumIntersectOutside) {
  OBB obb;
  obb.center = glm::dvec3(100, 0, -5);  // way to the right
  obb.axes[0] = glm::dvec3(1, 0, 0);
  obb.axes[1] = glm::dvec3(0, 1, 0);
  obb.axes[2] = glm::dvec3(0, 0, 1);
  obb.half_extent = glm::dvec3(1, 1, 1);

  Frustum f;
  f.planes[0] = glm::dvec4( 1, 0, 0, 5);
  f.planes[1] = glm::dvec4(-1, 0, 0, 5);
  f.planes[2] = glm::dvec4(0,  1, 0, 5);
  f.planes[3] = glm::dvec4(0, -1, 0, 5);
  f.planes[4] = glm::dvec4(0, 0,-1,-1);
  f.planes[5] = glm::dvec4(0, 0, 1, 10);

  auto inter = frustumIntersectOBB(f, obb);
  EXPECT_EQ(inter, FrustumIntersection::Outside);
}

TEST(OBB, FrustumIntersectIntersecting) {
  // OBB straddles the right plane of the frustum.
  OBB obb;
  obb.center = glm::dvec3(4.9, 0, -5);  // 0.1m inside the +x plane (x=5)
  obb.axes[0] = glm::dvec3(1, 0, 0);
  obb.axes[1] = glm::dvec3(0, 1, 0);
  obb.axes[2] = glm::dvec3(0, 0, 1);
  obb.half_extent = glm::dvec3(1, 1, 1);  // extends from x=3.9 to x=5.9 → crosses

  Frustum f;
  f.planes[0] = glm::dvec4( 1, 0, 0,  5);
  f.planes[1] = glm::dvec4(-1, 0, 0,  5);
  f.planes[2] = glm::dvec4( 0, 1, 0,  5);
  f.planes[3] = glm::dvec4( 0,-1, 0,  5);
  f.planes[4] = glm::dvec4( 0, 0,-1, -1);
  f.planes[5] = glm::dvec4( 0, 0, 1, 10);

  auto inter = frustumIntersectOBB(f, obb);
  EXPECT_EQ(inter, FrustumIntersection::Intersecting);
}

TEST(OBB, ElevatedMinAltClampsLowerBoundToSeaLevel) {
  // Mountainous tile: DEM reports min_h=5000m, max_h=8000m.
  // OBB must still extend down to at least sea level so it encloses the
  // ground footprint visible to a camera looking at the mountain from above.
  GeoBounds b{ glm::radians(86.9), glm::radians(27.9), glm::radians(87.0), glm::radians(28.0) };
  OBB obb = lonLatAltBoxToOBB(b, 5000.0, 8000.0, WGS84);

  // The OBB up-axis points roughly along the local radial direction.
  // OBB center altitude should be (effective_min_alt + max_alt) / 2 = (0+8000)/2 = 4000m
  // (NOT (5000+8000)/2 = 6500m which it would be without the clamp).
  const f64 center_r = glm::length(obb.center);
  const f64 R = WGS84.a;
  const f64 center_alt_approx = center_r - R;
  EXPECT_LT(center_alt_approx, 5000.0)
      << "OBB center should sit below the DEM min (sea-level clamp), but is at " << center_alt_approx;
  EXPECT_GT(obb.half_extent.z, 3000.0)
      << "OBB up half-extent should span at least max_alt/2 = 4000m";
}

TEST(OBB, FullGlobeBoundsProducesEarthSizedBox) {
  // z=0 tile spans the entire longitude range — the OBB must enclose the
  // whole Earth, not collapse laterally.
  GeoBounds b{ -3.14159265, -1.5707963, 3.14159265, 1.5707963 };  // west, south, east, north
  OBB obb = lonLatAltBoxToOBB(b, 0.0, 0.0, WGS84);

  // Center should be at the Earth's center (0,0,0).
  EXPECT_NEAR(obb.center.x, 0.0, 1.0);
  EXPECT_NEAR(obb.center.y, 0.0, 1.0);
  EXPECT_NEAR(obb.center.z, 0.0, 1.0);
  // Half-extents should all be at least the equatorial radius.
  EXPECT_GT(obb.half_extent.x, 6300000.0);
  EXPECT_GT(obb.half_extent.y, 6300000.0);
  EXPECT_GT(obb.half_extent.z, 6300000.0);
}
