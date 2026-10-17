#include <hongtu/coordinate/projection.hpp>
#include <cmath>
namespace hongtu::coordinate {

static constexpr f64 PI = 3.14159265358979323846;
static constexpr f64 DEG_TO_RAD = PI / 180.0;
static constexpr f64 RAD_TO_DEG = 180.0 / PI;

// UTM scale factor at central meridian
static constexpr f64 UTM_K0 = 0.9996;
// Gauss-Kruger scale factor (no scale reduction)
static constexpr f64 GK_K0 = 1.0;

// UTM false easting
static constexpr f64 UTM_FALSE_EASTING = 500000.0;
// UTM false northing for southern hemisphere
static constexpr f64 UTM_FALSE_NORTHING_SOUTH = 10000000.0;

// ============================================================================
// Meridian arc length from equator to latitude phi
// Uses Helmert series (accurate to ~0.01mm)
// ============================================================================

static f64 meridianArcLength(f64 lat_rad, const Ellipsoid& ellipsoid) {
  const f64 a = ellipsoid.a;

  const f64 n = ellipsoid.f / (2.0 - ellipsoid.f);
  const f64 n2 = n * n;
  const f64 n3 = n2 * n;
  const f64 n4 = n3 * n;

  // Helmert series coefficients
  const f64 A0 = 1.0 + n2 / 4.0 + n4 / 64.0;
  const f64 A2 = 3.0 / 2.0 * (n - n3 / 8.0);
  const f64 A4 = 15.0 / 16.0 * (n2 - n4 / 4.0);
  const f64 A6 = 35.0 / 48.0 * n3;
  const f64 A8 = 315.0 / 512.0 * n4;

  const f64 ap = a / (1.0 + n) * A0;

  return ap * (lat_rad
               - A2 * std::sin(2.0 * lat_rad)
               + A4 * std::sin(4.0 * lat_rad)
               - A6 * std::sin(6.0 * lat_rad)
               + A8 * std::sin(8.0 * lat_rad));
}

// ============================================================================
// Footpoint latitude from meridian arc length (inverse of meridianArcLength)
// ============================================================================

static f64 footpointLatitude(f64 M, const Ellipsoid& ellipsoid) {
  const f64 a = ellipsoid.a;
  const f64 n = ellipsoid.f / (2.0 - ellipsoid.f);
  const f64 n2 = n * n;
  const f64 n3 = n2 * n;
  const f64 n4 = n3 * n;

  const f64 A0 = 1.0 + n2 / 4.0 + n4 / 64.0;
  const f64 ap = a / (1.0 + n) * A0;

  f64 mu = M / ap;

  // Inverse series
  const f64 B2 = 3.0 / 2.0 * n - 27.0 / 32.0 * n3;
  const f64 B4 = 21.0 / 16.0 * n2 - 55.0 / 32.0 * n4;
  const f64 B6 = 151.0 / 96.0 * n3;
  const f64 B8 = 1097.0 / 512.0 * n4;

  return mu
         + B2 * std::sin(2.0 * mu)
         + B4 * std::sin(4.0 * mu)
         + B6 * std::sin(6.0 * mu)
         + B8 * std::sin(8.0 * mu);
}

// ============================================================================
// Transverse Mercator Forward
// ============================================================================

void transverseMercatorForward(f64 lat_rad, f64 lon_rad, f64 central_meridian,
                               const Ellipsoid& ellipsoid,
                               f64& easting, f64& northing) {
  const f64 ep2 = ellipsoid.ep2;

  const f64 phi = lat_rad;
  const f64 dl = lon_rad - central_meridian;

  const f64 sin_phi = std::sin(phi);
  const f64 cos_phi = std::cos(phi);
  const f64 tan_phi = std::tan(phi);

  const f64 N = ellipsoid.primeVerticalRadius(sin_phi);
  const f64 T = tan_phi * tan_phi;
  const f64 C = ep2 * cos_phi * cos_phi;
  const f64 A = cos_phi * dl;

  const f64 M = meridianArcLength(phi, ellipsoid);

  const f64 A2 = A * A;
  const f64 A3 = A2 * A;
  const f64 A4 = A3 * A;
  const f64 A5 = A4 * A;
  const f64 A6 = A5 * A;

  easting = N * (A
                 + (1.0 - T + C) * A3 / 6.0
                 + (5.0 - 18.0 * T + T * T + 72.0 * C - 58.0 * ep2) * A5 / 120.0);

  northing = M + N * tan_phi * (A2 / 2.0
                                + (5.0 - T + 9.0 * C + 4.0 * C * C) * A4 / 24.0
                                + (61.0 - 58.0 * T + T * T + 600.0 * C - 330.0 * ep2) * A6 / 720.0);
}

// ============================================================================
// Transverse Mercator Inverse
// ============================================================================

void transverseMercatorInverse(f64 easting, f64 northing, f64 central_meridian,
                               const Ellipsoid& ellipsoid,
                               f64& lat_rad, f64& lon_rad) {
  const f64 ep2 = ellipsoid.ep2;

  const f64 phi1 = footpointLatitude(northing, ellipsoid);

  const f64 sin_phi1 = std::sin(phi1);
  const f64 cos_phi1 = std::cos(phi1);
  const f64 tan_phi1 = std::tan(phi1);

  const f64 N1 = ellipsoid.primeVerticalRadius(sin_phi1);
  const f64 R1 = ellipsoid.meridianRadius(sin_phi1);
  const f64 T1 = tan_phi1 * tan_phi1;
  const f64 C1 = ep2 * cos_phi1 * cos_phi1;
  const f64 D = easting / N1;

  const f64 D2 = D * D;
  const f64 D3 = D2 * D;
  const f64 D4 = D3 * D;
  const f64 D5 = D4 * D;
  const f64 D6 = D5 * D;

  lat_rad = phi1
            - (N1 * tan_phi1 / R1) * (D2 / 2.0
              - (5.0 + 3.0 * T1 + 10.0 * C1 - 4.0 * C1 * C1 - 9.0 * ep2) * D4 / 24.0
              + (61.0 + 90.0 * T1 + 298.0 * C1 + 45.0 * T1 * T1 - 252.0 * ep2 - 3.0 * C1 * C1) * D6 / 720.0);

  lon_rad = central_meridian
            + (D
               - (1.0 + 2.0 * T1 + C1) * D3 / 6.0
               + (5.0 - 2.0 * C1 + 28.0 * T1 - 3.0 * C1 * C1 + 8.0 * ep2 + 24.0 * T1 * T1) * D5 / 120.0)
              / cos_phi1;
}

// ============================================================================
// UTM Zone Utilities
// ============================================================================

int utmZoneFromLongitude(f64 lon_deg) {
  // Normalize longitude to [-180, 180)
  while (lon_deg >= 180.0) lon_deg -= 360.0;
  while (lon_deg < -180.0) lon_deg += 360.0;
  return static_cast<int>(std::floor((lon_deg + 180.0) / 6.0)) + 1;
}

f64 utmCentralMeridian(int zone_number) {
  return (zone_number - 1) * 6.0 - 180.0 + 3.0;
}

// ============================================================================
// UTM Forward
// ============================================================================

UTMCoord geodeticToUTM(f64 lat_rad, f64 lon_rad, const Ellipsoid& ellipsoid,
                       int force_zone) {
  f64 lat_deg = lat_rad * RAD_TO_DEG;
  f64 lon_deg = lon_rad * RAD_TO_DEG;

  int zone = (force_zone > 0) ? force_zone : utmZoneFromLongitude(lon_deg);
  bool is_north = (lat_deg >= 0.0);

  f64 cm_rad = utmCentralMeridian(zone) * DEG_TO_RAD;

  f64 x, y;
  transverseMercatorForward(lat_rad, lon_rad, cm_rad, ellipsoid, x, y);

  f64 easting = x * UTM_K0 + UTM_FALSE_EASTING;
  f64 northing = y * UTM_K0;
  if (!is_north) {
    northing += UTM_FALSE_NORTHING_SOUTH;
  }

  return {easting, northing, {zone, is_north}};
}

// ============================================================================
// UTM Inverse
// ============================================================================

void utmToGeodetic(const UTMCoord& utm, f64& lat_rad, f64& lon_rad,
                   const Ellipsoid& ellipsoid) {
  f64 x = (utm.easting - UTM_FALSE_EASTING) / UTM_K0;
  f64 y = utm.northing;
  if (!utm.zone.is_north) {
    y -= UTM_FALSE_NORTHING_SOUTH;
  }
  y /= UTM_K0;

  f64 cm_rad = utmCentralMeridian(utm.zone.zone_number) * DEG_TO_RAD;

  transverseMercatorInverse(x, y, cm_rad, ellipsoid, lat_rad, lon_rad);
}

// ============================================================================
// Gauss-Kruger Zone Utilities
// ============================================================================

int gaussKrugerZoneFromLongitude(f64 lon_deg, int zone_width) {
  if (zone_width == 6) {
    return static_cast<int>(std::floor(lon_deg / 6.0)) + 1;
  } else if (zone_width == 3) {
    return static_cast<int>(std::round(lon_deg / 3.0));
  }
  return 0;
}

f64 gaussKrugerCentralMeridian(int zone_number, int zone_width) {
  if (zone_width == 6) {
    return zone_number * 6.0 - 3.0;
  } else if (zone_width == 3) {
    return zone_number * 3.0;
  }
  return 0.0;
}

// ============================================================================
// Gauss-Kruger Forward
// ============================================================================

GaussKrugerCoord geodeticToGaussKruger(f64 lat_rad, f64 lon_rad,
                                       int zone_width,
                                       const Ellipsoid& ellipsoid,
                                       int force_zone) {
  f64 lon_deg = lon_rad * RAD_TO_DEG;

  int zone = (force_zone > 0) ? force_zone : gaussKrugerZoneFromLongitude(lon_deg, zone_width);
  f64 cm_deg = gaussKrugerCentralMeridian(zone, zone_width);
  f64 cm_rad = cm_deg * DEG_TO_RAD;

  f64 x, y;
  transverseMercatorForward(lat_rad, lon_rad, cm_rad, ellipsoid, x, y);

  // Gauss-Kruger uses k0=1.0, easting includes zone prefix
  f64 easting = x * GK_K0 + 500000.0 + zone * 1000000.0;
  f64 northing = y * GK_K0;

  return {easting, northing, zone, zone_width};
}

// ============================================================================
// Gauss-Kruger Inverse
// ============================================================================

void gaussKrugerToGeodetic(const GaussKrugerCoord& gk, f64& lat_rad, f64& lon_rad,
                           const Ellipsoid& ellipsoid) {
  // Remove zone prefix from easting
  f64 x = gk.easting - gk.zone_number * 1000000.0 - 500000.0;
  f64 y = gk.northing;

  // k0 = 1.0 for Gauss-Kruger
  f64 cm_deg = gaussKrugerCentralMeridian(gk.zone_number, gk.zone_width);
  f64 cm_rad = cm_deg * DEG_TO_RAD;

  transverseMercatorInverse(x, y, cm_rad, ellipsoid, lat_rad, lon_rad);
}

} // namespace hongtu::coordinate
