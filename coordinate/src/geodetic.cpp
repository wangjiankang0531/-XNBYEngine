#include <hongtu/coordinate/geodetic.hpp>
#include <cmath>

namespace hongtu::coordinate {

// ============================================================================
// Geodetic helpers
// ============================================================================

Geodetic Geodetic::fromDegrees(f64 lat_deg, f64 lon_deg, f64 alt_m) {
  constexpr f64 DEG_TO_RAD = 3.14159265358979323846 / 180.0;
  return {lat_deg * DEG_TO_RAD, lon_deg * DEG_TO_RAD, alt_m};
}

void Geodetic::toDegrees(f64& lat_deg, f64& lon_deg) const {
  constexpr f64 RAD_TO_DEG = 180.0 / 3.14159265358979323846;
  lat_deg = lat * RAD_TO_DEG;
  lon_deg = lon * RAD_TO_DEG;
}

// ============================================================================
// Geodetic -> ECEF
// ============================================================================

glm::dvec3 geodeticToECEF(const Geodetic& geo, const Ellipsoid& ellipsoid) {
  const f64 sin_lat = std::sin(geo.lat);
  const f64 cos_lat = std::cos(geo.lat);
  const f64 sin_lon = std::sin(geo.lon);
  const f64 cos_lon = std::cos(geo.lon);

  // Prime vertical radius of curvature
  const f64 N = ellipsoid.primeVerticalRadius(sin_lat);

  const f64 x = (N + geo.alt) * cos_lat * cos_lon;
  const f64 y = (N + geo.alt) * cos_lat * sin_lon;
  const f64 z = (N * (1.0 - ellipsoid.e2) + geo.alt) * sin_lat;

  return {x, y, z};
}

// ============================================================================
// ECEF -> Geodetic (Bowring's iterative method)
// ============================================================================

Geodetic ecefToGeodetic(const glm::dvec3& ecef, const Ellipsoid& ellipsoid) {
  const f64 x = ecef.x;
  const f64 y = ecef.y;
  const f64 z = ecef.z;

  const f64 a = ellipsoid.a;
  const f64 e2 = ellipsoid.e2;
  const f64 b = ellipsoid.b;

  const f64 p = std::sqrt(x * x + y * y);
  const f64 lon = std::atan2(y, x);

  // Handle pole case
  if (p < 1e-10) {
    const f64 sign = (z >= 0.0) ? 1.0 : -1.0;
    const f64 lat = sign * 3.14159265358979323846 / 2.0;
    const f64 alt = std::abs(z) - b;
    return {lat, lon, alt};
  }

  // Initial estimate using Bowring's formula
  f64 theta = std::atan2(z * a, p * b);
  f64 sin_theta = std::sin(theta);
  f64 cos_theta = std::cos(theta);

  f64 lat = std::atan2(z + ellipsoid.ep2 * b * sin_theta * sin_theta * sin_theta,
                       p - e2 * a * cos_theta * cos_theta * cos_theta);

  // Iterate (typically converges in 2-3 iterations)
  for (int i = 0; i < 5; ++i) {
    const f64 sin_lat = std::sin(lat);
    const f64 N = ellipsoid.primeVerticalRadius(sin_lat);

    const f64 lat_new = std::atan2(z + e2 * N * sin_lat, p);

    if (std::abs(lat_new - lat) < 1e-14) {
      lat = lat_new;
      break;
    }
    lat = lat_new;
  }

  const f64 sin_lat = std::sin(lat);
  const f64 cos_lat = std::cos(lat);
  const f64 N = ellipsoid.primeVerticalRadius(sin_lat);

  f64 alt;
  if (std::abs(cos_lat) > 1e-10) {
    alt = p / cos_lat - N;
  } else {
    alt = std::abs(z) / std::abs(sin_lat) - N * (1.0 - e2);
  }

  return {lat, lon, alt};
}

// ============================================================================
// ECEF <-> ENU
// ============================================================================

glm::dvec3 ecefToENU(const glm::dvec3& ecef, const Geodetic& ref_geo,
                     const glm::dvec3& ref_ecef) {
  const f64 sin_lat = std::sin(ref_geo.lat);
  const f64 cos_lat = std::cos(ref_geo.lat);
  const f64 sin_lon = std::sin(ref_geo.lon);
  const f64 cos_lon = std::cos(ref_geo.lon);

  const f64 dx = ecef.x - ref_ecef.x;
  const f64 dy = ecef.y - ref_ecef.y;
  const f64 dz = ecef.z - ref_ecef.z;

  // Rotation from ECEF to ENU
  const f64 e = -sin_lon * dx + cos_lon * dy;
  const f64 n = -sin_lat * cos_lon * dx - sin_lat * sin_lon * dy + cos_lat * dz;
  const f64 u = cos_lat * cos_lon * dx + cos_lat * sin_lon * dy + sin_lat * dz;

  return {e, n, u};
}

glm::dvec3 enuToECEF(const glm::dvec3& enu, const Geodetic& ref_geo,
                     const glm::dvec3& ref_ecef) {
  const f64 sin_lat = std::sin(ref_geo.lat);
  const f64 cos_lat = std::cos(ref_geo.lat);
  const f64 sin_lon = std::sin(ref_geo.lon);
  const f64 cos_lon = std::cos(ref_geo.lon);

  const f64 e = enu.x;
  const f64 n = enu.y;
  const f64 u = enu.z;

  // Rotation from ENU to ECEF (transpose of ECEF->ENU)
  const f64 dx = -sin_lon * e - sin_lat * cos_lon * n + cos_lat * cos_lon * u;
  const f64 dy = cos_lon * e - sin_lat * sin_lon * n + cos_lat * sin_lon * u;
  const f64 dz = cos_lat * n + sin_lat * u;

  return {ref_ecef.x + dx, ref_ecef.y + dy, ref_ecef.z + dz};
}

// ============================================================================
// Convenience: Geodetic <-> ENU
// ============================================================================

glm::dvec3 geodeticToENU(const Geodetic& geo, const Geodetic& ref_geo,
                         const Ellipsoid& ellipsoid) {
  const auto ecef = geodeticToECEF(geo, ellipsoid);
  const auto ref_ecef = geodeticToECEF(ref_geo, ellipsoid);
  return ecefToENU(ecef, ref_geo, ref_ecef);
}

Geodetic enuToGeodetic(const glm::dvec3& enu, const Geodetic& ref_geo,
                       const Ellipsoid& ellipsoid) {
  const auto ref_ecef = geodeticToECEF(ref_geo, ellipsoid);
  const auto ecef = enuToECEF(enu, ref_geo, ref_ecef);
  return ecefToGeodetic(ecef, ellipsoid);
}

// ============================================================================
// Batch conversions
// ============================================================================

void batchGeodeticToECEF(const Geodetic* input, glm::dvec3* output, size_t count,
                         const Ellipsoid& ellipsoid) {
  for (size_t i = 0; i < count; ++i) {
    output[i] = geodeticToECEF(input[i], ellipsoid);
  }
}

void batchECEFToGeodetic(const glm::dvec3* input, Geodetic* output, size_t count,
                         const Ellipsoid& ellipsoid) {
  for (size_t i = 0; i < count; ++i) {
    output[i] = ecefToGeodetic(input[i], ellipsoid);
  }
}

} // namespace hongtu::coordinate
