#include <hongtu/scene/emitter_entity.hpp>

namespace hongtu::scene {

glm::vec3 GeoEmitter::getRTCPosition(const RTCOrigin& origin) const {
  return getRTCPosition(origin, WGS84);
}

glm::vec3 GeoEmitter::getRTCPosition(const RTCOrigin& origin,
                                     const Ellipsoid& ellipsoid) const {
  glm::dvec3 ecef = coordinate::geodeticToECEF(position_, ellipsoid);
  return origin.worldToLocal(ecef);
}

glm::vec3 LocalEmitter::getRTCPosition(const RTCOrigin& origin) const {
  return origin.worldToLocal(position_);
}

}  // namespace hongtu::scene
