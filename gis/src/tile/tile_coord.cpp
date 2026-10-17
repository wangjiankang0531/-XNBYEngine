#include <hongtu/gis/tile/tile_coord.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace hongtu::gis {

// ============================================================================
// Constants
// ============================================================================

static constexpr f64 kPI = 3.14159265358979323846;
static constexpr f64 kDegToRad = kPI / 180.0;
// kRadToDeg not needed currently

// ============================================================================
// GeoBounds
// ============================================================================

bool GeoBounds::contains(f64 lat, f64 lon) const {
  return lon >= west && lon <= east && lat >= south && lat <= north;
}

bool GeoBounds::intersects(const GeoBounds& other) const {
  return west <= other.east && east >= other.west && south <= other.north && north >= other.south;
}

void GeoBounds::center(f64& lat, f64& lon) const {
  lat = (south + north) * 0.5;
  lon = (west + east) * 0.5;
}

// ============================================================================
// TileCoord
// ============================================================================

bool TileCoord::isValid() const {
  if (z < 0) return false;
  i32 n = maxTilesAtLevel(z);
  return x >= 0 && x < n && y >= 0 && y < n;
}

i32 TileCoord::maxTilesAtLevel(i32 zoom) {
  if (zoom < 0) return 0;
  return 1 << zoom;
}

TileCoord TileCoord::parent() const { return {z - 1, x / 2, y / 2}; }

std::array<TileCoord, 4> TileCoord::children() const {
  i32 cx = x * 2;
  i32 cy = y * 2;
  i32 cz = z + 1;
  return {{{cz, cx, cy}, {cz, cx + 1, cy}, {cz, cx, cy + 1}, {cz, cx + 1, cy + 1}}};
}

GeoBounds TileCoord::toBounds() const {
  f64 n = static_cast<f64>(maxTilesAtLevel(z));

  // Longitude: linear mapping [0, n] -> [-PI, PI]
  f64 west = static_cast<f64>(x) / n * 2.0 * kPI - kPI;
  f64 east = static_cast<f64>(x + 1) / n * 2.0 * kPI - kPI;

  // Latitude: inverse Gudermannian (inverse Mercator)
  // y=0 is north, y=n is south
  f64 north = std::atan(std::sinh(kPI * (1.0 - 2.0 * static_cast<f64>(y) / n)));
  f64 south = std::atan(std::sinh(kPI * (1.0 - 2.0 * static_cast<f64>(y + 1) / n)));

  return {west, south, east, north};
}

TileCoord TileCoord::fromGeodetic(f64 lat_deg, f64 lon_deg, i32 zoom) {
  // Clamp latitude to Web Mercator range
  lat_deg = std::clamp(lat_deg, -kWebMercatorMaxLatDeg, kWebMercatorMaxLatDeg);

  f64 n = static_cast<f64>(maxTilesAtLevel(zoom));
  f64 lat_rad = lat_deg * kDegToRad;

  // Longitude -> x
  i32 tx = static_cast<i32>((lon_deg + 180.0) / 360.0 * n);

  // Latitude -> y (Mercator projection)
  f64 y_frac = (1.0 - std::log(std::tan(lat_rad) + 1.0 / std::cos(lat_rad)) / kPI) / 2.0;
  i32 ty = static_cast<i32>(y_frac * n);

  // Clamp to valid range
  i32 max_tile = static_cast<i32>(n) - 1;
  tx = std::clamp(tx, 0, max_tile);
  ty = std::clamp(ty, 0, max_tile);

  return {zoom, tx, ty};
}

bool TileCoord::operator==(const TileCoord& other) const {
  return z == other.z && x == other.x && y == other.y;
}

bool TileCoord::operator!=(const TileCoord& other) const { return !(*this == other); }

std::string TileCoord::toString() const {
  std::ostringstream oss;
  oss << z << "/" << x << "/" << y;
  return oss.str();
}

std::size_t TileCoord::Hash::operator()(const TileCoord& coord) const {
  // Golden ratio hash mixing
  constexpr std::size_t kGoldenRatio = 0x9e3779b97f4a7c15ULL;
  std::size_t h = 0;
  h ^= std::hash<i32>{}(coord.z) + kGoldenRatio + (h << 6) + (h >> 2);
  h ^= std::hash<i32>{}(coord.x) + kGoldenRatio + (h << 6) + (h >> 2);
  h ^= std::hash<i32>{}(coord.y) + kGoldenRatio + (h << 6) + (h >> 2);
  return h;
}

}  // namespace hongtu::gis
