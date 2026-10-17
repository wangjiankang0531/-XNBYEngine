#include <hongtu/scene/annotation/polygon_annotation.hpp>

#include "annotation_json.hpp"

namespace hongtu::scene {

std::vector<glm::vec3>
GeoPolygonAnnotation::getRtcVertices(const coordinate::PrecisionOrigin& origin) const {
  std::vector<glm::vec3> out;
  out.reserve(vertices_.size());
  for (const auto& g : vertices_) {
    out.push_back(origin.worldToLocal(coordinate::geodeticToECEF(g)));
  }
  return out;
}

nlohmann::json GeoPolygonAnnotation::toJson() const {
  nlohmann::json j;
  j["kind"] = detail::kindToString(AnnotationKind::Polygon);
  j["mode"] = detail::modeToString(AnnotationMode::Geo);
  detail::writeBaseFields(j, *this);
  auto& v = j["vertices"] = nlohmann::json::array();
  for (const auto& g : vertices_) v.push_back(detail::geodeticToJson(g));
  return j;
}

void GeoPolygonAnnotation::fromJson(const nlohmann::json& j) {
  detail::readBaseFields(j, *this);
  vertices_.clear();
  if (j.contains("vertices")) {
    for (const auto& v : j["vertices"]) vertices_.push_back(detail::geodeticFromJson(v));
  }
}

std::vector<glm::vec3>
LocalPolygonAnnotation::getRtcVertices(const coordinate::PrecisionOrigin& origin) const {
  std::vector<glm::vec3> out;
  out.reserve(vertices_.size());
  for (const auto& p : vertices_) out.push_back(origin.worldToLocal(p));
  return out;
}

nlohmann::json LocalPolygonAnnotation::toJson() const {
  nlohmann::json j;
  j["kind"] = detail::kindToString(AnnotationKind::Polygon);
  j["mode"] = detail::modeToString(AnnotationMode::Local);
  detail::writeBaseFields(j, *this);
  auto& v = j["vertices"] = nlohmann::json::array();
  for (const auto& p : vertices_) v.push_back(detail::dvec3ToJson(p));
  return j;
}

void LocalPolygonAnnotation::fromJson(const nlohmann::json& j) {
  detail::readBaseFields(j, *this);
  vertices_.clear();
  if (j.contains("vertices")) {
    for (const auto& v : j["vertices"]) vertices_.push_back(detail::dvec3FromJson(v));
  }
}

}  // namespace hongtu::scene
