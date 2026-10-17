#include <hongtu/scene/polyline_entity.hpp>

namespace hongtu::scene {

std::vector<glm::vec3>
GeoPolyline::getRTCPoints(const RTCOrigin& origin) const {
  return getRTCPoints(origin, WGS84);
}

std::vector<glm::vec3>
GeoPolyline::getRTCPoints(const RTCOrigin& origin,
                          const Ellipsoid& ellipsoid) const {
  std::vector<glm::vec3> out;
  out.reserve(points_.size());
  for (const auto& g : points_) {
    glm::dvec3 ecef = coordinate::geodeticToECEF(g, ellipsoid);
    out.push_back(origin.worldToLocal(ecef));
  }
  return out;
}

std::vector<glm::vec3>
LocalPolyline::getRTCPoints(const RTCOrigin& origin) const {
  std::vector<glm::vec3> out;
  out.reserve(points_.size());
  for (const auto& p : points_) {
    out.push_back(origin.worldToLocal(p));
  }
  return out;
}

}  // namespace hongtu::scene
