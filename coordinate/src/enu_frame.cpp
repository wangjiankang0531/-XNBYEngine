#include <hongtu/coordinate/enu_frame.hpp>

#include <cmath>

namespace hongtu::coordinate {

glm::dvec3 EnuFrame::localToEcef(const glm::dvec3& local_m) const {
  return origin_ecef + east * local_m.x + north * local_m.y + up * local_m.z;
}

glm::dvec3 EnuFrame::ecefToLocal(const glm::dvec3& ecef) const {
  glm::dvec3 d = ecef - origin_ecef;
  return glm::dvec3(glm::dot(d, east), glm::dot(d, north), glm::dot(d, up));
}

EnuFrame enuFrameAt(const Geodetic& anchor) {
  EnuFrame f;
  f.origin_ecef = geodeticToECEF(anchor);
  const double sl = std::sin(anchor.lat), cl = std::cos(anchor.lat);
  const double so = std::sin(anchor.lon), co = std::cos(anchor.lon);
  f.east  = glm::dvec3(-so,        co,       0.0);
  f.north = glm::dvec3(-sl * co,  -sl * so,  cl);
  f.up    = glm::dvec3( cl * co,   cl * so,  sl);
  return f;
}

}  // namespace hongtu::coordinate
