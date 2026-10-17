#include <hongtu/scene/annotation/arrow_annotation.hpp>

#include "annotation_json.hpp"

namespace hongtu::scene {

std::vector<glm::vec3>
GeoArrowAnnotation::getRtcEndpoints(const coordinate::PrecisionOrigin& origin) const {
  return {
      origin.worldToLocal(coordinate::geodeticToECEF(start_)),
      origin.worldToLocal(coordinate::geodeticToECEF(end_)),
  };
}

nlohmann::json GeoArrowAnnotation::toJson() const {
  nlohmann::json j;
  j["kind"]     = detail::kindToString(AnnotationKind::Arrow);
  j["mode"]     = detail::modeToString(AnnotationMode::Geo);
  detail::writeBaseFields(j, *this);
  j["vertices"] = nlohmann::json::array({
      detail::geodeticToJson(start_),
      detail::geodeticToJson(end_),
  });
  return j;
}

void GeoArrowAnnotation::fromJson(const nlohmann::json& j) {
  detail::readBaseFields(j, *this);
  if (j.contains("vertices") && j["vertices"].size() >= 2) {
    start_ = detail::geodeticFromJson(j["vertices"][0]);
    end_   = detail::geodeticFromJson(j["vertices"][1]);
  }
}

std::vector<glm::vec3>
LocalArrowAnnotation::getRtcEndpoints(const coordinate::PrecisionOrigin& origin) const {
  return { origin.worldToLocal(start_), origin.worldToLocal(end_) };
}

nlohmann::json LocalArrowAnnotation::toJson() const {
  nlohmann::json j;
  j["kind"]     = detail::kindToString(AnnotationKind::Arrow);
  j["mode"]     = detail::modeToString(AnnotationMode::Local);
  detail::writeBaseFields(j, *this);
  j["vertices"] = nlohmann::json::array({
      detail::dvec3ToJson(start_),
      detail::dvec3ToJson(end_),
  });
  return j;
}

void LocalArrowAnnotation::fromJson(const nlohmann::json& j) {
  detail::readBaseFields(j, *this);
  if (j.contains("vertices") && j["vertices"].size() >= 2) {
    start_ = detail::dvec3FromJson(j["vertices"][0]);
    end_   = detail::dvec3FromJson(j["vertices"][1]);
  }
}

}  // namespace hongtu::scene
