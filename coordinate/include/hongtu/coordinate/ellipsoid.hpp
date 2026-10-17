#pragma once

#include <hongtu/core/common.hpp>
#include <cmath>

namespace hongtu::coordinate {

/// Reference ellipsoid parameters for geodetic computations.
/// All lengths in meters.
struct Ellipsoid {
  f64 a;   ///< Semi-major axis (equatorial radius)
  f64 b;   ///< Semi-minor axis (polar radius)
  f64 f;   ///< Flattening: f = (a - b) / a
  f64 e2;  ///< First eccentricity squared: e2 = 2f - f^2
  f64 ep2; ///< Second eccentricity squared: ep2 = (a^2 - b^2) / b^2

  /// Construct from semi-major axis and inverse flattening.
  static constexpr Ellipsoid fromInverseFlattening(f64 semi_major, f64 inv_f) {
    f64 flattening = 1.0 / inv_f;
    f64 semi_minor = semi_major * (1.0 - flattening);
    f64 e_sq = 2.0 * flattening - flattening * flattening;
    f64 ep_sq = (semi_major * semi_major - semi_minor * semi_minor) / (semi_minor * semi_minor);
    return {semi_major, semi_minor, flattening, e_sq, ep_sq};
  }

  /// Radius of curvature in the prime vertical at latitude phi (radians).
  f64 primeVerticalRadius(f64 sin_phi) const {
    return a / std::sqrt(1.0 - e2 * sin_phi * sin_phi);
  }

  /// Radius of curvature in the meridian at latitude phi (radians).
  f64 meridianRadius(f64 sin_phi) const {
    f64 w = 1.0 - e2 * sin_phi * sin_phi;
    return a * (1.0 - e2) / (w * std::sqrt(w));
  }
};

/// WGS84 reference ellipsoid (EPSG:4326).
inline constexpr Ellipsoid WGS84 = Ellipsoid::fromInverseFlattening(6378137.0, 298.257223563);

/// CGCS2000 (China Geodetic Coordinate System 2000), same ellipsoid as GRS80.
inline constexpr Ellipsoid CGCS2000 = Ellipsoid::fromInverseFlattening(6378137.0, 298.257222101);

/// Krasovsky 1940 ellipsoid (used by Beijing-54 and Xi'an 80).
inline constexpr Ellipsoid KRASOVSKY = Ellipsoid::fromInverseFlattening(6378245.0, 298.3);

/// GRS80 ellipsoid (used by ITRF, ETRS89, etc.)
inline constexpr Ellipsoid GRS80 = Ellipsoid::fromInverseFlattening(6378137.0, 298.257222101);

} // namespace hongtu::coordinate
