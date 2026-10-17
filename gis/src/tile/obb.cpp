#include <hongtu/gis/tile/obb.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace hongtu::gis {

using namespace hongtu::coordinate;

OBB lonLatAltBoxToOBB(const GeoBounds& bounds, f64 min_alt, f64 max_alt,
                      const Ellipsoid& ellipsoid) {
  // Special case: tiles that wrap the full longitude range (z=0 root, or any
  // future global-wrapping tile) — the ENU-frame OBB construction projects all
  // 8 antimeridian-spanning corners onto an east axis of (0,1,0), giving
  // half_extent.x = 0 and a laterally collapsed box.  Instead, use an
  // Earth-centered axis-aligned box big enough to contain the whole ellipsoid
  // plus the altitude range.
  constexpr f64 kFullLonRange = 6.283185307;  // 2π
  if (bounds.east - bounds.west >= kFullLonRange - 1e-6) {
    OBB obb;
    obb.center = glm::dvec3(0.0, 0.0, 0.0);
    obb.axes = {glm::dvec3(1, 0, 0), glm::dvec3(0, 1, 0), glm::dvec3(0, 0, 1)};
    const f64 a = ellipsoid.a + std::max(0.0, max_alt);
    obb.half_extent = glm::dvec3(a, a, a);
    return obb;
  }

  // Extend the OBB downward to at least sea level when DEM-driven min_alt
  // is positive (mountainous terrain). Otherwise the OBB rises off the
  // ellipsoid surface and the perspective frustum — which is narrower at
  // higher elevations — false-rejects tiles whose ground footprint the
  // camera can actually see. Adding sea level to the box costs negligible
  // OBB volume and ensures the frustum test sees the true visibility.
  const f64 effective_min_alt = std::min(min_alt, 0.0);

  f64 center_lat = 0.0, center_lon = 0.0;
  bounds.center(center_lat, center_lon);
  const f64 center_alt = (effective_min_alt + max_alt) * 0.5;

  // Center in ECEF, plus orthonormal ENU axes at that point.
  const glm::dvec3 center =
      geodeticToECEF(Geodetic{center_lat, center_lon, center_alt}, ellipsoid);

  const f64 sl = std::sin(center_lat), cl = std::cos(center_lat);
  const f64 so = std::sin(center_lon), co = std::cos(center_lon);

  // ENU axes at the center point:
  //   east  = (-sinλ, cosλ, 0)
  //   north = (-sinφ·cosλ, -sinφ·sinλ, cosφ)
  //   up    = (cosφ·cosλ,  cosφ·sinλ,  sinφ)
  const glm::dvec3 east(-so, co, 0.0);
  const glm::dvec3 north(-sl * co, -sl * so, cl);
  const glm::dvec3 up(cl * co, cl * so, sl);

  OBB obb;
  obb.center = center;
  obb.axes = {east, north, up};

  // Enumerate the 8 lon/lat/alt corners, project onto each axis.
  const f64 lats[2] = {bounds.south, bounds.north};
  const f64 lons[2] = {bounds.west, bounds.east};
  const f64 alts[2] = {effective_min_alt, max_alt};

  glm::dvec3 hext(0.0);
  for (int i = 0; i < 8; ++i) {
    const glm::dvec3 corner_ecef = geodeticToECEF(
        Geodetic{lats[i & 1], lons[(i >> 1) & 1], alts[(i >> 2) & 1]}, ellipsoid);
    const glm::dvec3 delta = corner_ecef - center;
    hext.x = std::max(hext.x, std::abs(glm::dot(delta, obb.axes[0])));
    hext.y = std::max(hext.y, std::abs(glm::dot(delta, obb.axes[1])));
    hext.z = std::max(hext.z, std::abs(glm::dot(delta, obb.axes[2])));
  }
  obb.half_extent = hext;
  return obb;
}

glm::dvec3 closestPointOnOBB(const glm::dvec3& p, const OBB& obb) {
  const glm::dvec3 delta = p - obb.center;
  glm::dvec3 out = obb.center;
  for (int i = 0; i < 3; ++i) {
    f64 d = glm::dot(delta, obb.axes[i]);
    d = std::clamp(d, -obb.half_extent[i], obb.half_extent[i]);
    out += d * obb.axes[i];
  }
  return out;
}

Frustum Frustum::fromViewProjection(const glm::dmat4& m) {
  // glm uses column-major: m[col][row]. Combined planes (Gribb-Hartmann).
  Frustum f;
  // Row vectors of m.
  const auto row = [&](int r) {
    return glm::dvec4(m[0][r], m[1][r], m[2][r], m[3][r]);
  };
  const auto r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
  f.planes[0] = r3 + r0;  // left
  f.planes[1] = r3 - r0;  // right
  f.planes[2] = r3 + r1;  // bottom
  f.planes[3] = r3 - r1;  // top
  f.planes[4] = r3 + r2;  // near
  f.planes[5] = r3 - r2;  // far
  // Normalize (not strictly required for sign tests, but keeps d meaningful).
  for (auto& p : f.planes) {
    f64 n = glm::length(glm::dvec3(p));
    if (n > 0.0) p /= n;
  }
  return f;
}

FrustumIntersection frustumIntersectOBB(const Frustum& f, const OBB& obb) {
  bool all_inside = true;
  for (const auto& plane : f.planes) {
    const glm::dvec3 n(plane);
    const f64 d = plane.w;
    // Signed distance from OBB center to the plane.
    const f64 dist = glm::dot(n, obb.center) + d;
    // Projected half-extent of the OBB onto the plane normal.
    const f64 r =
        obb.half_extent.x * std::abs(glm::dot(n, obb.axes[0])) +
        obb.half_extent.y * std::abs(glm::dot(n, obb.axes[1])) +
        obb.half_extent.z * std::abs(glm::dot(n, obb.axes[2]));

    if (dist + r < 0.0) return FrustumIntersection::Outside;
    if (dist - r < 0.0) all_inside = false;
  }
  return all_inside ? FrustumIntersection::Inside : FrustumIntersection::Intersecting;
}

}  // namespace hongtu::gis
