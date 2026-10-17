#include <hongtu/scene/shockwave_entity.hpp>

namespace hongtu::scene {

glm::vec3 GeoShockwave::getRTCCenter(const RTCOrigin& origin) const {
  return getRTCCenter(origin, WGS84);
}

glm::vec3 GeoShockwave::getRTCCenter(const RTCOrigin& origin,
                                     const Ellipsoid& ellipsoid) const {
  glm::dvec3 ecef = coordinate::geodeticToECEF(center_, ellipsoid);
  return origin.worldToLocal(ecef);
}

glm::vec3 LocalShockwave::getRTCCenter(const RTCOrigin& origin) const {
  return origin.worldToLocal(center_);
}

}  // namespace hongtu::scene
