#include <hongtu/scene/annotation/text_annotation.hpp>

#include "annotation_json.hpp"

namespace hongtu::scene {

glm::vec3
GeoTextAnnotation::getRtcAnchor(const coordinate::PrecisionOrigin& origin) const {
  return origin.worldToLocal(coordinate::geodeticToECEF(anchor_));
}

nlohmann::json GeoTextAnnotation::toJson() const {
  nlohmann::json j;
  j["kind"]     = detail::kindToString(AnnotationKind::Text);
  j["mode"]     = detail::modeToString(AnnotationMode::Geo);
  detail::writeBaseFields(j, *this);
  j["vertices"] = nlohmann::json::array({ detail::geodeticToJson(anchor_) });
  return j;
}

void GeoTextAnnotation::fromJson(const nlohmann::json& j) {
  detail::readBaseFields(j, *this);
  if (j.contains("vertices") && j["vertices"].is_array() && !j["vertices"].empty()) {
    anchor_ = detail::geodeticFromJson(j["vertices"][0]);
  }
}

glm::vec3
LocalTextAnnotation::getRtcAnchor(const coordinate::PrecisionOrigin& origin) const {
  return origin.worldToLocal(anchor_);
}

nlohmann::json LocalTextAnnotation::toJson() const {
  nlohmann::json j;
  j["kind"]     = detail::kindToString(AnnotationKind::Text);
  j["mode"]     = detail::modeToString(AnnotationMode::Local);
  detail::writeBaseFields(j, *this);
  j["vertices"] = nlohmann::json::array({ detail::dvec3ToJson(anchor_) });
  return j;
}

void LocalTextAnnotation::fromJson(const nlohmann::json& j) {
  detail::readBaseFields(j, *this);
  if (j.contains("vertices") && j["vertices"].is_array() && !j["vertices"].empty()) {
    anchor_ = detail::dvec3FromJson(j["vertices"][0]);
  }
}

}  // namespace hongtu::scene
