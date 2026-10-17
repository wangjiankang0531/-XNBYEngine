#include <hongtu/scene/agent.hpp>

#include <glm/gtc/matrix_transform.hpp>

namespace hongtu::scene {

namespace {

/// Yaw-only rotation matrix from a heading vector. Agent stays upright;
/// heading rotates around +Z (local) or local up axis (geo via base rotation
/// composed at render time). For P0 + Local we just yaw around +Z.
glm::dmat4 yawMatrix(const glm::dvec3& heading_xy) {
  // Project heading to XY plane; if degenerate, identity.
  glm::dvec2 h(heading_xy.x, heading_xy.y);
  double len2 = glm::dot(h, h);
  if (len2 < 1e-9) return glm::dmat4(1.0);
  h /= std::sqrt(len2);
  glm::dmat4 m(1.0);
  m[0][0] =  h.x;  m[0][1] =  h.y;
  m[1][0] = -h.y;  m[1][1] =  h.x;
  return m;
}

}  // namespace

// =============================================================================
// LocalAgent
// =============================================================================

glm::dmat4 LocalAgent::getModelMatrix() const {
  glm::dmat4 m = glm::translate(glm::dmat4(1.0), pos_);
  m = m * yawMatrix(getHeading());
  return m;
}

// =============================================================================
// GeoAgent
// =============================================================================

glm::dvec3 GeoAgent::getWorldPosition() const {
  return coordinate::geodeticToECEF(pos_geo_);
}

glm::dmat4 GeoAgent::getModelMatrix() const {
  // Build an ENU-aligned frame at the agent's position:
  //   - +Z = local up (ECEF normal)
  //   - heading mapped from ECEF velocity into ENU
  // Compose translate(ECEF) * R(enu_frame * yaw).
  glm::dvec3 ecef = coordinate::geodeticToECEF(pos_geo_);
  glm::dmat4 m(1.0);
  m[3] = glm::dvec4(ecef, 1.0);

  // ENU basis at this geodetic point.
  const double lat = pos_geo_.lat, lon = pos_geo_.lon;
  const double sl = std::sin(lat), cl = std::cos(lat);
  const double slo = std::sin(lon), clo = std::cos(lon);
  glm::dvec3 east(-slo,             clo,              0.0);
  glm::dvec3 north(-sl * clo, -sl * slo, cl);
  glm::dvec3 up   ( cl * clo,  cl * slo, sl);

  // Heading in ENU local frame (projected from world velocity).
  glm::dvec3 v = getVelocity();
  double vx = glm::dot(v, east);
  double vy = glm::dot(v, north);
  glm::dvec2 h(vx, vy);
  double len2 = glm::dot(h, h);
  if (len2 > 1e-9) h /= std::sqrt(len2);
  else             h = {1.0, 0.0};

  // Compose: each model column is the agent-local axis in world ECEF.
  // agent +X = h.x * east + h.y * north  (forward heading)
  // agent +Y = -h.y * east + h.x * north (left)
  // agent +Z = up
  m[0] = glm::dvec4( h.x * east + h.y * north, 0.0);
  m[1] = glm::dvec4(-h.y * east + h.x * north, 0.0);
  m[2] = glm::dvec4(up, 0.0);
  return m;
}

void GeoAgent::integrate(const glm::dvec3& world_velocity, float dt) {
  // Use ENU-relative offset:add velocity × dt to ECEF position, then round
  // trip back to Geodetic. enuToGeodetic gives us the new geodetic from an
  // ENU offset to the reference point; we feed it a zero ENU offset of
  // current pos + the velocity component.
  glm::dvec3 ecef = coordinate::geodeticToECEF(pos_geo_);
  ecef += world_velocity * static_cast<double>(dt);
  pos_geo_ = coordinate::ecefToGeodetic(ecef);
}

}  // namespace hongtu::scene
