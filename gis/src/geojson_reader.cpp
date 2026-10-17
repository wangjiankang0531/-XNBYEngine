#include <hongtu/gis/geojson_reader.hpp>
#include <nlohmann/json.hpp>

namespace hongtu::gis {

GeoPoint GeoJSONReader::parseCoordinate(const nlohmann::json& j) {
  GeoPoint pt;
  pt.x = j[0].get<f64>();
  pt.y = j[1].get<f64>();
  if (j.size() >= 3) {
    pt.z = j[2].get<f64>();
    pt.has_z = true;
  }
  return pt;
}

Ring GeoJSONReader::parseRing(const nlohmann::json& j) {
  Ring ring;
  for (const auto& coord : j) {
    ring.push_back(parseCoordinate(coord));
  }
  return ring;
}

Geometry GeoJSONReader::parseGeometry(const nlohmann::json& j) {
  Geometry geom;
  std::string type = j.at("type").get<std::string>();

  if (type == "Point") {
    geom.type = GeometryType::Point;
    geom.point = parseCoordinate(j.at("coordinates"));
  } else if (type == "MultiPoint") {
    geom.type = GeometryType::MultiPoint;
    for (const auto& coord : j.at("coordinates")) {
      geom.points.push_back(parseCoordinate(coord));
    }
  } else if (type == "LineString") {
    geom.type = GeometryType::LineString;
    for (const auto& coord : j.at("coordinates")) {
      geom.points.push_back(parseCoordinate(coord));
    }
  } else if (type == "MultiLineString") {
    geom.type = GeometryType::MultiLineString;
    for (const auto& line : j.at("coordinates")) {
      geom.rings.push_back(parseRing(line));
    }
  } else if (type == "Polygon") {
    geom.type = GeometryType::Polygon;
    for (const auto& ring : j.at("coordinates")) {
      geom.rings.push_back(parseRing(ring));
    }
  } else if (type == "MultiPolygon") {
    geom.type = GeometryType::MultiPolygon;
    for (const auto& polygon : j.at("coordinates")) {
      for (const auto& ring : polygon) {
        geom.rings.push_back(parseRing(ring));
      }
    }
  } else if (type == "GeometryCollection") {
    geom.type = GeometryType::GeometryCollection;
    for (const auto& g : j.at("geometries")) {
      geom.geometries.push_back(parseGeometry(g));
    }
  }

  return geom;
}

void GeoJSONReader::parseProperties(const nlohmann::json& j, Feature& feature) {
  if (!j.is_object()) return;
  for (auto& [key, val] : j.items()) {
    if (val.is_string()) {
      feature.properties[key] = val.get<std::string>();
    } else if (val.is_number_integer()) {
      feature.properties[key] = val.get<i64>();
    } else if (val.is_number_float()) {
      feature.properties[key] = val.get<f64>();
    } else if (val.is_boolean()) {
      feature.properties[key] = val.get<bool>();
    }
  }
}

Feature GeoJSONReader::parseFeature(const nlohmann::json& j) {
  Feature feature;
  if (j.contains("geometry") && !j["geometry"].is_null()) {
    feature.geometry = parseGeometry(j["geometry"]);
  }
  if (j.contains("properties") && !j["properties"].is_null()) {
    parseProperties(j["properties"], feature);
  }
  if (j.contains("id")) {
    if (j["id"].is_number_integer()) {
      feature.fid = j["id"].get<i64>();
    }
  }
  return feature;
}

bool GeoJSONReader::parse(const std::string& json_str) {
  try {
    auto j = nlohmann::json::parse(json_str);
    return parse(j);
  } catch (const std::exception& e) {
    error_ = std::string("JSON parse error: ") + e.what();
    return false;
  }
}

bool GeoJSONReader::parse(const nlohmann::json& j) {
  features_ = {};
  error_.clear();

  try {
    std::string type = j.at("type").get<std::string>();

    if (type == "FeatureCollection") {
      if (j.contains("features")) {
        i64 fid = 0;
        for (const auto& feat : j["features"]) {
          auto f = parseFeature(feat);
          if (f.fid < 0) f.fid = fid;
          features_.features.push_back(std::move(f));
          ++fid;
        }
      }
    } else if (type == "Feature") {
      features_.features.push_back(parseFeature(j));
    } else {
      // Bare geometry
      Feature f;
      f.geometry = parseGeometry(j);
      features_.features.push_back(std::move(f));
    }

    // Collect field names from first feature
    if (!features_.features.empty()) {
      for (const auto& [key, _] : features_.features[0].properties) {
        features_.field_names.push_back(key);
      }
    }

    return true;
  } catch (const std::exception& e) {
    error_ = std::string("GeoJSON parse error: ") + e.what();
    return false;
  }
}

// ============================================================================
// Writer
// ============================================================================

static nlohmann::json coordToJson(const GeoPoint& pt) {
  if (pt.has_z) return {pt.x, pt.y, pt.z};
  return {pt.x, pt.y};
}

static nlohmann::json ringToJson(const Ring& ring) {
  auto arr = nlohmann::json::array();
  for (const auto& pt : ring) arr.push_back(coordToJson(pt));
  return arr;
}

static nlohmann::json geometryToJson(const Geometry& geom) {
  nlohmann::json j;
  j["type"] = geometryTypeName(geom.type);

  switch (geom.type) {
    case GeometryType::Point:
      j["coordinates"] = coordToJson(geom.point);
      break;
    case GeometryType::MultiPoint:
    case GeometryType::LineString: {
      auto arr = nlohmann::json::array();
      for (const auto& pt : geom.points) arr.push_back(coordToJson(pt));
      j["coordinates"] = arr;
      break;
    }
    case GeometryType::Polygon:
    case GeometryType::MultiLineString: {
      auto arr = nlohmann::json::array();
      for (const auto& ring : geom.rings) arr.push_back(ringToJson(ring));
      j["coordinates"] = arr;
      break;
    }
    case GeometryType::MultiPolygon: {
      // For simplicity, wrap each ring in its own polygon
      auto arr = nlohmann::json::array();
      for (const auto& ring : geom.rings) {
        arr.push_back(nlohmann::json::array({ringToJson(ring)}));
      }
      j["coordinates"] = arr;
      break;
    }
    case GeometryType::GeometryCollection: {
      auto arr = nlohmann::json::array();
      for (const auto& g : geom.geometries) arr.push_back(geometryToJson(g));
      j["geometries"] = arr;
      break;
    }
    default: break;
  }
  return j;
}

std::string writeGeoJSON(const FeatureCollection& fc, int indent) {
  nlohmann::json j;
  j["type"] = "FeatureCollection";

  auto features = nlohmann::json::array();
  for (const auto& f : fc.features) {
    nlohmann::json feat;
    feat["type"] = "Feature";
    feat["geometry"] = geometryToJson(f.geometry);

    nlohmann::json props = nlohmann::json::object();
    for (const auto& [key, val] : f.properties) {
      std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
          props[key] = nullptr;
        } else {
          props[key] = v;
        }
      }, val);
    }
    feat["properties"] = props;
    features.push_back(feat);
  }
  j["features"] = features;

  return j.dump(indent);
}

} // namespace hongtu::gis
