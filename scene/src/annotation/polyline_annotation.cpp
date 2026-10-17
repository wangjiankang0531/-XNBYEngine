#include <hongtu/scene/annotation/polyline_annotation.hpp>

#include "annotation_json.hpp"

namespace hongtu::scene {

// =============================================================================
// GeoPolylineAnnotation
// =============================================================================

std::vector<glm::vec3>
GeoPolylineAnnotation::getRtcVertices(const coordinate::PrecisionOrigin& origin) const {
  std::vector<glm::vec3> out;
  out.reserve(vertices_.size());
  for (const auto& g : vertices_) {
    const glm::dvec3 ecef = coordinate::geodeticToECEF(g);
    out.push_back(origin.worldToLocal(ecef));
  }
  return out;
}

nlohmann::json GeoPolylineAnnotation::toJson() const {
  nlohmann::json j;
  j["kind"] = detail::kindToString(AnnotationKind::Polyline);
  j["mode"] = detail::modeToString(AnnotationMode::Geo);
  detail::writeBaseFields(j, *this);
  auto& v = j["vertices"] = nlohmann::json::array();
  for (const auto& g : vertices_) v.push_back(detail::geodeticToJson(g));
  return j;
}

void GeoPolylineAnnotation::fromJson(const nlohmann::json& j) {
  detail::readBaseFields(j, *this);
  vertices_.clear();
  if (j.contains("vertices")) {
    for (const auto& v : j["vertices"]) vertices_.push_back(detail::geodeticFromJson(v));
  }
}

// =============================================================================
// LocalPolylineAnnotation
// =============================================================================

std::vector<glm::vec3>
LocalPolylineAnnotation::getRtcVertices(const coordinate::PrecisionOrigin& origin) const {
  std::vector<glm::vec3> out;
  out.reserve(vertices_.size());
  for (const auto& p : vertices_) {
    out.push_back(origin.worldToLocal(p));
  }
  return out;
}

nlohmann::json LocalPolylineAnnotation::toJson() const {
  nlohmann::json j;
  j["kind"] = detail::kindToString(AnnotationKind::Polyline);
  j["mode"] = detail::modeToString(AnnotationMode::Local);
  detail::writeBaseFields(j, *this);
  auto& v = j["vertices"] = nlohmann::json::array();
  for (const auto& p : vertices_) v.push_back(detail::dvec3ToJson(p));
  return j;
}

void LocalPolylineAnnotation::fromJson(const nlohmann::json& j) {
  detail::readBaseFields(j, *this);
  vertices_.clear();
  if (j.contains("vertices")) {
    for (const auto& v : j["vertices"]) vertices_.push_back(detail::dvec3FromJson(v));
  }
}

}  // namespace hongtu::scene
