#pragma once

#include <hongtu/gis/geometry.hpp>
#include <string>
#include <vector>
#include <cstdio>

namespace hongtu::gis {

/// Shapefile shape type codes (from ESRI spec).
enum class ShapeType : i32 {
  Null = 0,
  Point = 1,
  PolyLine = 3,
  Polygon = 5,
  MultiPoint = 8,
  PointZ = 11,
  PolyLineZ = 13,
  PolygonZ = 15,
  MultiPointZ = 18,
  PointM = 21,
  PolyLineM = 23,
  PolygonM = 25,
  MultiPointM = 28,
};

/// DBF field descriptor.
struct DBFField {
  std::string name;
  char type;         ///< C=character, N=numeric, F=float, L=logical, D=date
  u8 length;
  u8 decimal_count;
};

/// Reads ESRI Shapefiles (.shp + .dbf).
/// Supports Point, PolyLine, Polygon, MultiPoint (2D and Z variants).
class ShapefileReader {
 public:
  ShapefileReader() = default;

  /// Open and read a shapefile. Pass path without extension (reads .shp + .dbf).
  /// @param base_path  Path without extension, e.g. "/data/roads"
  /// @return true on success.
  bool open(const std::string& base_path);

  /// Read from raw byte buffers (for testing without files).
  bool openFromBuffers(const std::vector<u8>& shp_data,
                       const std::vector<u8>& dbf_data);

  /// Get the feature collection.
  const FeatureCollection& getFeatures() const { return features_; }

  /// Get the shape type.
  ShapeType getShapeType() const { return shape_type_; }

  /// Get the bounding box from the file header.
  void getFileBounds(f64& min_x, f64& min_y, f64& max_x, f64& max_y) const;

  /// Get the DBF field descriptors.
  const std::vector<DBFField>& getFields() const { return fields_; }

  /// Get error message if open() failed.
  const std::string& getError() const { return error_; }

 private:
  bool parseSHP(const u8* data, size_t size);
  bool parseDBF(const u8* data, size_t size);
  Geometry parsePointRecord(const u8* data, size_t size, bool has_z);
  Geometry parsePolyRecord(const u8* data, size_t size, GeometryType type, bool has_z);
  Geometry parseMultiPointRecord(const u8* data, size_t size, bool has_z);

  FeatureCollection features_;
  ShapeType shape_type_ = ShapeType::Null;
  std::vector<DBFField> fields_;
  f64 bounds_[4] = {};  // min_x, min_y, max_x, max_y
  std::string error_;
};

} // namespace hongtu::gis
