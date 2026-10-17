#pragma once

#include <hongtu/coordinate/ellipsoid.hpp>

namespace hongtu::coordinate {

/// UTM zone information.
struct UTMZone {
  int zone_number;  ///< 1-60
  bool is_north;    ///< true = Northern hemisphere, false = Southern
};

/// UTM projected coordinate (easting, northing in meters).
struct UTMCoord {
  f64 easting;   ///< Easting in meters (500000 = central meridian)
  f64 northing;  ///< Northing in meters (10000000 offset in southern hemisphere)
  UTMZone zone;
};

/// Gauss-Kruger projected coordinate.
struct GaussKrugerCoord {
  f64 easting;       ///< Easting in meters (includes zone prefix)
  f64 northing;      ///< Northing in meters
  int zone_number;   ///< Zone number
  int zone_width;    ///< Zone width in degrees (3 or 6)
};

// ============================================================================
// UTM Projection
// ============================================================================

/// Determine UTM zone number from longitude in degrees.
int utmZoneFromLongitude(f64 lon_deg);

/// Get the central meridian (degrees) for a given UTM zone number.
f64 utmCentralMeridian(int zone_number);

/// Convert geodetic (lat/lon in radians) to UTM.
/// @param lat_rad  Latitude in radians
/// @param lon_rad  Longitude in radians
/// @param zone     If zone_number > 0, force that zone; otherwise auto-detect
UTMCoord geodeticToUTM(f64 lat_rad, f64 lon_rad,
                       const Ellipsoid& ellipsoid = WGS84,
                       int force_zone = 0);

/// Convert UTM to geodetic (lat/lon in radians).
/// @param utm      UTM coordinate with zone info
/// @param lat_rad  Output latitude in radians
/// @param lon_rad  Output longitude in radians
void utmToGeodetic(const UTMCoord& utm, f64& lat_rad, f64& lon_rad,
                   const Ellipsoid& ellipsoid = WGS84);

// ============================================================================
// Gauss-Kruger Projection
// ============================================================================

/// Get the central meridian (degrees) for a Gauss-Kruger zone.
/// @param zone_number  Zone number
/// @param zone_width   Zone width: 3 or 6 degrees
f64 gaussKrugerCentralMeridian(int zone_number, int zone_width);

/// Determine Gauss-Kruger zone number from longitude (degrees).
/// @param lon_deg     Longitude in degrees
/// @param zone_width  Zone width: 3 or 6 degrees
int gaussKrugerZoneFromLongitude(f64 lon_deg, int zone_width);

/// Convert geodetic (lat/lon in radians) to Gauss-Kruger.
/// @param lat_rad     Latitude in radians
/// @param lon_rad     Longitude in radians
/// @param zone_width  Zone width: 3 or 6 degrees
/// @param force_zone  If > 0, force that zone; otherwise auto-detect
GaussKrugerCoord geodeticToGaussKruger(f64 lat_rad, f64 lon_rad,
                                       int zone_width = 6,
                                       const Ellipsoid& ellipsoid = CGCS2000,
                                       int force_zone = 0);

/// Convert Gauss-Kruger to geodetic (lat/lon in radians).
void gaussKrugerToGeodetic(const GaussKrugerCoord& gk, f64& lat_rad, f64& lon_rad,
                           const Ellipsoid& ellipsoid = CGCS2000);

// ============================================================================
// Transverse Mercator core (used by both UTM and Gauss-Kruger)
// ============================================================================

/// Forward Transverse Mercator projection.
/// @param lat_rad          Latitude in radians
/// @param lon_rad          Longitude in radians
/// @param central_meridian Central meridian in radians
/// @param easting          Output easting relative to central meridian
/// @param northing         Output northing from equator
void transverseMercatorForward(f64 lat_rad, f64 lon_rad, f64 central_meridian,
                               const Ellipsoid& ellipsoid,
                               f64& easting, f64& northing);

/// Inverse Transverse Mercator projection.
/// @param easting          Easting relative to central meridian
/// @param northing         Northing from equator
/// @param central_meridian Central meridian in radians
/// @param lat_rad          Output latitude in radians
/// @param lon_rad          Output longitude in radians
void transverseMercatorInverse(f64 easting, f64 northing, f64 central_meridian,
                               const Ellipsoid& ellipsoid,
                               f64& lat_rad, f64& lon_rad);

} // namespace hongtu::coordinate
