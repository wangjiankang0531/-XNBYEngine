#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/coordinate/ellipsoid.hpp>
#include <hongtu/gis/tile/tile_coord.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/mat4x4.hpp>
#include <array>

namespace hongtu::gis {

/// Oriented bounding box. `axes` are orthonormal; `half_extent` is the
/// half-length along each axis (always positive).
struct OBB {
  glm::dvec3 center{0.0};
  std::array<glm::dvec3, 3> axes{glm::dvec3(1, 0, 0), glm::dvec3(0, 1, 0), glm::dvec3(0, 0, 1)};
  glm::dvec3 half_extent{0.0};
};

/// Build an OBB enclosing a lon/lat/alt box on the ellipsoid. Uses ENU axes
/// at the box center for orientation; half_extent contains the worst-case
/// projection of the 8 corners onto those axes.
OBB lonLatAltBoxToOBB(const GeoBounds& bounds, f64 min_alt, f64 max_alt,
                      const coordinate::Ellipsoid& ellipsoid = coordinate::WGS84);

/// Closest point on `obb` to `p` (ECEF, double precision). If p is inside,
/// returns p unchanged.
glm::dvec3 closestPointOnOBB(const glm::dvec3& p, const OBB& obb);

enum class FrustumIntersection : i32 {
  Outside,
  Intersecting,
  Inside,
};

/// 6-plane frustum. Each plane is (a, b, c, d) such that a*x+b*y+c*z+d >= 0
/// inside the frustum.
struct Frustum {
  std::array<glm::dvec4, 6> planes;

  /// Extract from a glm column-major view-projection matrix. Assumes the
  /// OpenGL clip-space depth convention z ∈ [-w, w]; if a future caller
  /// supplies a Vulkan [0, w] depth-corrected matrix, change near to `r2`
  /// and far to `r3 - r2` in the implementation.
  static Frustum fromViewProjection(const glm::dmat4& view_projection);
};

/// Three-state OBB-vs-frustum intersection test.
FrustumIntersection frustumIntersectOBB(const Frustum& frustum, const OBB& obb);

}  // namespace hongtu::gis
