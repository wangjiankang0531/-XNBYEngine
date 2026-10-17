#include <hongtu/scene/billboard_entity.hpp>

namespace hongtu::scene {

glm::vec3 GeoBillboard::getRTCPosition(const RTCOrigin& origin) const {
  return getRTCPosition(origin, WGS84);
}

glm::vec3 GeoBillboard::getRTCPosition(const RTCOrigin& origin,
                                       const Ellipsoid& ellipsoid) const {
  glm::dvec3 ecef = coordinate::geodeticToECEF(position_, ellipsoid);
  return origin.worldToLocal(ecef);
}

glm::vec3 LocalBillboard::getRTCPosition(const RTCOrigin& origin) const {
  return origin.worldToLocal(position_);
}

}  // namespace hongtu::scene
