#include <algorithm>
#include <cmath>
#include <fstream>
#include <hongtu/gis/dlg_layer.hpp>
#include <hongtu/gis/geojson_reader.hpp>
#include <limits>
#include <sstream>
#include <string_view>
#include <type_traits>

namespace hongtu::gis {
namespace {

std::string lowerAscii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    if (c >= 'A' && c <= 'Z') {
      return static_cast<char>(c - 'A' + 'a');
    }
    return static_cast<char>(c);
  });
  return value;
}

std::string attributeToString(const AttributeValue& value) {
  return std::visit(
      [](const auto& item) -> std::string {
        using T = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<T, std::string>) {
          return lowerAscii(item);
        } else if constexpr (std::is_same_v<T, bool>) {
          return item ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::monostate>) {
          return {};
        } else {
          return std::to_string(item);
        }
      },
      value);
}

const AttributeValue* findProperty(const Feature& feature, std::string_view requested_key) {
  for (const auto& [key, value] : feature.properties) {
    if (lowerAscii(key) == requested_key) {
      return &value;
    }
  }
  return nullptr;
}

bool hasProperty(const Feature& feature, std::string_view key) {
  const AttributeValue* value = findProperty(feature, key);
  return value != nullptr && !attributeToString(*value).empty() && attributeToString(*value) != "no";
}

bool propertyEquals(const Feature& feature, std::string_view key, std::string_view expected) {
  const AttributeValue* value = findProperty(feature, key);
  return value != nullptr && attributeToString(*value) == expected;
}

DlgFeatureClass classify(const Feature& feature) {
  if (hasProperty(feature, "highway")) return DlgFeatureClass::Road;
  if (hasProperty(feature, "railway")) return DlgFeatureClass::Railway;
  if (hasProperty(feature, "waterway") || propertyEquals(feature, "natural", "water") || hasProperty(feature, "water"))
    return DlgFeatureClass::Water;
  if (hasProperty(feature, "boundary")) return DlgFeatureClass::Boundary;
  if (hasProperty(feature, "building")) return DlgFeatureClass::Building;
  if (propertyEquals(feature, "man_made", "pipeline") || hasProperty(feature, "pipeline"))
    return DlgFeatureClass::Pipeline;
  if (propertyEquals(feature, "power", "line") || propertyEquals(feature, "power", "minor_line") ||
      propertyEquals(feature, "power", "cable"))
    return DlgFeatureClass::Power;
  if (hasProperty(feature, "barrier")) return DlgFeatureClass::Barrier;
  if (propertyEquals(feature, "landuse", "industrial")) return DlgFeatureClass::Industrial;
  return DlgFeatureClass::Other;
}

bool isFiniteWgs84(const GeoPoint& point) {
  return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) && point.x >= -180.0 &&
         point.x <= 180.0 && point.y >= -90.0 && point.y <= 90.0;
}

void includeBounds(DlgWgs84Bounds& bounds, const GeoPoint& point) {
  if (!bounds.valid) {
    bounds.min_lon = bounds.max_lon = point.x;
    bounds.min_lat = bounds.max_lat = point.y;
    bounds.valid = true;
    return;
  }
  bounds.min_lon = std::min(bounds.min_lon, point.x);
  bounds.min_lat = std::min(bounds.min_lat, point.y);
  bounds.max_lon = std::max(bounds.max_lon, point.x);
  bounds.max_lat = std::max(bounds.max_lat, point.y);
}

bool pointsClosed(const std::vector<GeoPoint>& points) {
  return points.size() >= 3 && points.front().x == points.back().x && points.front().y == points.back().y;
}

bool appendPolyline(DlgDataset& output, const Feature& feature, DlgFeatureClass feature_class,
                    const std::vector<GeoPoint>& points, bool polygon_ring) {
  if (points.size() < 2) return true;
  for (const GeoPoint& point : points) {
    if (!isFiniteWgs84(point)) {
      output.error = "DLG contains a non-finite or out-of-range WGS84 coordinate";
      return false;
    }
  }

  DlgPolyline polyline;
  polyline.feature_id = feature.fid;
  polyline.feature_class = feature_class;
  polyline.closed = polygon_ring || pointsClosed(points);
  polyline.points = points;
  output.vertex_count += points.size();
  ++output.class_counts[static_cast<std::size_t>(feature_class)];
  for (const GeoPoint& point : points) includeBounds(output.bounds, point);
  output.polylines.push_back(std::move(polyline));
  return true;
}

bool appendGeometry(DlgDataset& output, const Feature& feature, DlgFeatureClass feature_class,
                    const Geometry& geometry) {
  switch (geometry.type) {
    case GeometryType::LineString:
      return appendPolyline(output, feature, feature_class, geometry.points, false);
    case GeometryType::MultiLineString:
      for (const Ring& line : geometry.rings) {
        if (!appendPolyline(output, feature, feature_class, line, false)) return false;
      }
      return true;
    case GeometryType::Polygon:
    case GeometryType::MultiPolygon:
      for (const Ring& ring : geometry.rings) {
        if (!appendPolyline(output, feature, feature_class, ring, true)) return false;
      }
      return true;
    case GeometryType::GeometryCollection:
      for (const Geometry& child : geometry.geometries) {
        if (!appendGeometry(output, feature, feature_class, child)) return false;
      }
      return true;
    default:
      return true;
  }
}

}  // namespace

const char* dlgFeatureClassName(DlgFeatureClass feature_class) {
  switch (feature_class) {
    case DlgFeatureClass::Road:
      return "road";
    case DlgFeatureClass::Railway:
      return "railway";
    case DlgFeatureClass::Water:
      return "water";
    case DlgFeatureClass::Boundary:
      return "boundary";
    case DlgFeatureClass::Building:
      return "building";
    case DlgFeatureClass::Pipeline:
      return "pipeline";
    case DlgFeatureClass::Power:
      return "power";
    case DlgFeatureClass::Barrier:
      return "barrier";
    case DlgFeatureClass::Industrial:
      return "industrial";
    case DlgFeatureClass::Other:
      return "other";
    case DlgFeatureClass::Count:
      break;
  }
  return "unknown";
}

DlgDataset buildDlgDataset(const FeatureCollection& features, std::string source) {
  DlgDataset output;
  output.source = std::move(source);
  output.source_feature_count = features.features.size();

  for (const Feature& feature : features.features) {
    if (!appendGeometry(output, feature, classify(feature), feature.geometry)) return output;
  }

  if (output.polylines.empty() || output.vertex_count == 0 || !output.bounds.valid) {
    output.error = "DLG contains no supported line or polygon geometry";
    return output;
  }
  output.valid = true;
  return output;
}

DlgDataset loadDlgGeoJson(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    DlgDataset output;
    output.source = path.generic_string();
    output.error = "Cannot read DLG GeoJSON: " + path.generic_string();
    return output;
  }
  std::ostringstream contents;
  contents << stream.rdbuf();

  GeoJSONReader reader;
  if (!reader.parse(contents.str())) {
    DlgDataset output;
    output.source = path.generic_string();
    output.error = reader.getError();
    return output;
  }
  return buildDlgDataset(reader.getFeatures(), path.generic_string());
}

DlgLocalDataset projectDlgToEnu(const DlgDataset& dataset, const hongtu::coordinate::Geodetic& anchor) {
  DlgLocalDataset output;
  output.anchor = anchor;
  output.class_counts = dataset.class_counts;
  if (!dataset.valid) {
    output.error = dataset.error.empty() ? "Cannot project an invalid DLG dataset" : dataset.error;
    return output;
  }
  if (!std::isfinite(anchor.lat) || !std::isfinite(anchor.lon) || !std::isfinite(anchor.alt)) {
    output.error = "DLG ENU anchor is not finite";
    return output;
  }

  output.minimum_enu = glm::dvec3(std::numeric_limits<double>::max());
  output.maximum_enu = glm::dvec3(std::numeric_limits<double>::lowest());
  output.polylines.reserve(dataset.polylines.size());
  for (const DlgPolyline& source_line : dataset.polylines) {
    DlgLocalPolyline local_line;
    local_line.feature_id = source_line.feature_id;
    local_line.feature_class = source_line.feature_class;
    local_line.closed = source_line.closed;
    local_line.enu_points.reserve(source_line.points.size());
    for (const GeoPoint& point : source_line.points) {
      const auto geodetic = hongtu::coordinate::Geodetic::fromDegrees(point.y, point.x, point.has_z ? point.z : 0.0);
      const glm::dvec3 enu = hongtu::coordinate::geodeticToENU(geodetic, anchor);
      if (!std::isfinite(enu.x) || !std::isfinite(enu.y) || !std::isfinite(enu.z)) {
        output.error = "DLG WGS84 to ENU conversion produced a non-finite coordinate";
        output.polylines.clear();
        return output;
      }
      output.minimum_enu = glm::min(output.minimum_enu, enu);
      output.maximum_enu = glm::max(output.maximum_enu, enu);
      local_line.enu_points.push_back(enu);

      const auto roundtrip = hongtu::coordinate::enuToGeodetic(enu, anchor);
      const glm::dvec3 source_ecef = hongtu::coordinate::geodeticToECEF(geodetic);
      const glm::dvec3 roundtrip_ecef = hongtu::coordinate::geodeticToECEF(roundtrip);
      output.maximum_roundtrip_error_m =
          std::max(output.maximum_roundtrip_error_m, glm::length(source_ecef - roundtrip_ecef));
      ++output.vertex_count;
    }
    output.polylines.push_back(std::move(local_line));
  }

  output.valid = output.vertex_count == dataset.vertex_count && !output.polylines.empty();
  if (!output.valid) output.error = "DLG ENU projection did not preserve the source vertex count";
  return output;
}

}  // namespace hongtu::gis
