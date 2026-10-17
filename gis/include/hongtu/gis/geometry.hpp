#pragma once

#include <hongtu/core/common.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <variant>
#include <unordered_map>

namespace hongtu::gis {

/// GIS geometry type (aligned with OGC Simple Features).
enum class GeometryType {
  Point,
  MultiPoint,
  LineString,
  MultiLineString,
  Polygon,
  MultiPolygon,
  GeometryCollection,
  Unknown,
};

/// Get string name for geometry type.
const char* geometryTypeName(GeometryType type);

/// A 2D/3D point with optional Z and M values.
struct GeoPoint {
  f64 x = 0;  ///< Longitude or Easting
  f64 y = 0;  ///< Latitude or Northing
  f64 z = 0;  ///< Altitude (optional)
  bool has_z = false;
};

/// A ring (closed sequence of points) for polygon boundaries.
using Ring = std::vector<GeoPoint>;

/// Geometry data - variant-based union of all geometry types.
struct Geometry {
  GeometryType type = GeometryType::Unknown;

  // Point
  GeoPoint point;
  // MultiPoint / LineString
  std::vector<GeoPoint> points;
  // Polygon (outer ring + inner rings/holes)
  std::vector<Ring> rings;
  // Multi-geometries
  std::vector<Geometry> geometries;

  /// Get the bounding box of this geometry.
  void getBounds(f64& min_x, f64& min_y, f64& max_x, f64& max_y) const;

  /// Get total number of vertices.
  u32 getVertexCount() const;
};

/// Attribute value (property).
using AttributeValue = std::variant<std::monostate, i64, f64, std::string, bool>;

/// A single geographic feature: geometry + attributes.
struct Feature {
  Geometry geometry;
  std::unordered_map<std::string, AttributeValue> properties;
  i64 fid = -1;  ///< Feature ID
};

/// A collection of features with shared schema.
struct FeatureCollection {
  std::string name;
  std::vector<Feature> features;
  std::vector<std::string> field_names;  ///< Ordered field names

  /// Bounding box of all features.
  void getBounds(f64& min_x, f64& min_y, f64& max_x, f64& max_y) const;
};

/// Coordinate reference system info (simplified).
struct CRSInfo {
  std::string name;      ///< e.g. "EPSG:4326"
  i32 epsg_code = 0;     ///< EPSG code (0 = unknown)
  bool is_geographic = true; ///< true = lat/lon, false = projected
};

} // namespace hongtu::gis
