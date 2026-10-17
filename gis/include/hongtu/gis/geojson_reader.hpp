#pragma once

#include <hongtu/gis/geometry.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace hongtu::gis {

/// Reads GeoJSON files (RFC 7946).
/// Supports FeatureCollection, Feature, and bare Geometry objects.
class GeoJSONReader {
 public:
  GeoJSONReader() = default;

  /// Parse a GeoJSON string.
  bool parse(const std::string& json_str);

  /// Parse from a JSON object.
  bool parse(const nlohmann::json& j);

  /// Get the feature collection.
  const FeatureCollection& getFeatures() const { return features_; }

  /// Get error message if parse() failed.
  const std::string& getError() const { return error_; }

 private:
  Geometry parseGeometry(const nlohmann::json& j);
  GeoPoint parseCoordinate(const nlohmann::json& j);
  Ring parseRing(const nlohmann::json& j);
  Feature parseFeature(const nlohmann::json& j);
  void parseProperties(const nlohmann::json& j, Feature& feature);

  FeatureCollection features_;
  std::string error_;
};

/// Write features to GeoJSON string.
std::string writeGeoJSON(const FeatureCollection& fc, int indent = 2);

} // namespace hongtu::gis
