#pragma once

#include <hongtu/core/common.hpp>
#include <array>
#include <string>

namespace hongtu::gis {

/// Geographic bounding box in radians.
struct GeoBounds {
  f64 west = 0;   ///< Western longitude in radians
  f64 south = 0;  ///< Southern latitude in radians
  f64 east = 0;   ///< Eastern longitude in radians
  f64 north = 0;  ///< Northern latitude in radians

  /// Check if this bounds contains a point (lat/lon in radians).
  bool contains(f64 lat, f64 lon) const;

  /// Check if this bounds intersects another bounds.
  bool intersects(const GeoBounds& other) const;

  /// Get the center of this bounds (lat/lon in radians).
  void center(f64& lat, f64& lon) const;

  /// Width in radians.
  f64 width() const { return east - west; }

  /// Height in radians.
  f64 height() const { return north - south; }
};

/// Web Mercator tile coordinate (z/x/y).
///
/// Uses the standard TMS/Slippy Map tile numbering:
/// - z: zoom level (0 = entire world in one tile)
/// - x: column (0 = west, 2^z-1 = east)
/// - y: row (0 = north, 2^z-1 = south)
struct TileCoord {
  i32 z = 0;  ///< Zoom level
  i32 x = 0;  ///< Column index
  i32 y = 0;  ///< Row index

  /// Check if this tile coordinate is valid.
  bool isValid() const;

  /// Get the maximum number of tiles per axis at a given zoom level.
  static i32 maxTilesAtLevel(i32 zoom);

  /// Get the parent tile (one zoom level up).
  TileCoord parent() const;

  /// Get the four child tiles (one zoom level down).
  std::array<TileCoord, 4> children() const;

  /// Convert this tile coordinate to geographic bounds (in radians).
  /// Uses the Web Mercator (EPSG:3857) tile scheme.
  GeoBounds toBounds() const;

  /// Create a tile coordinate from geographic coordinates.
  /// @param lat_deg  Latitude in degrees [-85.05112878, 85.05112878]
  /// @param lon_deg  Longitude in degrees [-180, 180]
  /// @param zoom     Zoom level (>= 0)
  static TileCoord fromGeodetic(f64 lat_deg, f64 lon_deg, i32 zoom);

  /// Equality comparison.
  bool operator==(const TileCoord& other) const;
  bool operator!=(const TileCoord& other) const;

  /// String representation: "z/x/y".
  std::string toString() const;

  /// Hash functor for use in unordered_map/unordered_set.
  struct Hash {
    std::size_t operator()(const TileCoord& coord) const;
  };
};

/// Maximum latitude (in degrees) for Web Mercator projection.
/// Equal to atan(sinh(pi)) in degrees.
constexpr f64 kWebMercatorMaxLatDeg = 85.05112877980659;

/// Maximum latitude (in radians) for Web Mercator projection.
constexpr f64 kWebMercatorMaxLatRad = 1.4844222297453324;

}  // namespace hongtu::gis
