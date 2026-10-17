#pragma once

#include <hongtu/coordinate/ellipsoid.hpp>
#include <glm/glm.hpp>

namespace hongtu::coordinate {

/// Geodetic coordinate (latitude, longitude, altitude).
/// Latitude and longitude are in radians, altitude in meters above the ellipsoid.
struct Geodetic {
  f64 lat; ///< Latitude in radians [-PI/2, PI/2]
  f64 lon; ///< Longitude in radians [-PI, PI]
  f64 alt; ///< Altitude above ellipsoid in meters

  /// Construct from degrees.
  static Geodetic fromDegrees(f64 lat_deg, f64 lon_deg, f64 alt_m = 0.0);
  /// Convert to degrees.
  void toDegrees(f64& lat_deg, f64& lon_deg) const;
};

// ============================================================================
// Geodetic <-> ECEF (Earth-Centered, Earth-Fixed) Conversions
// ============================================================================

/// Convert geodetic (lat/lon/alt) to ECEF (x, y, z) in meters.
/// Uses the exact closed-form formula.
glm::dvec3 geodeticToECEF(const Geodetic& geo, const Ellipsoid& ellipsoid = WGS84);

/// Convert ECEF (x, y, z) in meters to geodetic (lat/lon/alt).
/// Uses Bowring's iterative method (converges in 2-3 iterations for typical cases).
Geodetic ecefToGeodetic(const glm::dvec3& ecef, const Ellipsoid& ellipsoid = WGS84);

// ============================================================================
// ECEF <-> ENU (East-North-Up) Conversions
// ============================================================================

/// Convert ECEF difference vector to ENU coordinates relative to a reference point.
/// @param ecef        The ECEF position to convert.
/// @param ref_geo     Reference geodetic position (origin of ENU frame).
/// @param ref_ecef    Reference ECEF position (precomputed for efficiency).
glm::dvec3 ecefToENU(const glm::dvec3& ecef, const Geodetic& ref_geo,
                     const glm::dvec3& ref_ecef);

/// Convert ENU coordinates to ECEF.
/// @param enu         East-North-Up coordinates relative to reference.
/// @param ref_geo     Reference geodetic position (origin of ENU frame).
/// @param ref_ecef    Reference ECEF position (precomputed for efficiency).
glm::dvec3 enuToECEF(const glm::dvec3& enu, const Geodetic& ref_geo,
                     const glm::dvec3& ref_ecef);

// ============================================================================
// Convenience: Geodetic <-> ENU
// ============================================================================

/// Convert a geodetic position to ENU relative to a reference point.
glm::dvec3 geodeticToENU(const Geodetic& geo, const Geodetic& ref_geo,
                         const Ellipsoid& ellipsoid = WGS84);

/// Convert ENU position to geodetic given a reference point.
Geodetic enuToGeodetic(const glm::dvec3& enu, const Geodetic& ref_geo,
                       const Ellipsoid& ellipsoid = WGS84);

// ============================================================================
// Batch conversions
// ============================================================================

/// Batch convert geodetic positions to ECEF.
void batchGeodeticToECEF(const Geodetic* input, glm::dvec3* output, size_t count,
                         const Ellipsoid& ellipsoid = WGS84);

/// Batch convert ECEF positions to geodetic.
void batchECEFToGeodetic(const glm::dvec3* input, Geodetic* output, size_t count,
                         const Ellipsoid& ellipsoid = WGS84);

} // namespace hongtu::coordinate
