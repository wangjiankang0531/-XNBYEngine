#include <hongtu/coordinate/anchor_manager.hpp>
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>

namespace hongtu::coordinate {

static constexpr f64 PI = 3.14159265358979323846;
static constexpr f64 RAD_TO_DEG = 180.0 / PI;
static constexpr f64 DEG_TO_RAD = PI / 180.0;

// ============================================================================
// Internal: local <-> ECEF with azimuth rotation
// ============================================================================

glm::dvec3 AnchorManager::localToECEFInternal(const glm::dvec3& local_pos,
                                               const CoordinateAnchor& anchor) const {
  // Apply azimuth rotation: rotate local frame around Up axis by azimuth
  // Azimuth is clockwise from north, so we rotate E-N coordinates
  const f64 cos_az = std::cos(anchor.azimuth);
  const f64 sin_az = std::sin(anchor.azimuth);

  // Rotate local to standard ENU
  // Local X (forward) = cos(az)*E + sin(az)*N  => E = cos(az)*X - sin(az)*Y
  // Local Y (right)   = -sin(az)*E + cos(az)*N => N = sin(az)*X + cos(az)*Y
  glm::dvec3 enu;
  enu.x = cos_az * local_pos.x - sin_az * local_pos.y;  // East
  enu.y = sin_az * local_pos.x + cos_az * local_pos.y;  // North
  enu.z = local_pos.z;                                    // Up

  return enuToECEF(enu, anchor.origin, anchor.origin_ecef);
}

glm::dvec3 AnchorManager::ecefToLocalInternal(const glm::dvec3& ecef,
                                               const CoordinateAnchor& anchor) const {
  auto enu = ecefToENU(ecef, anchor.origin, anchor.origin_ecef);

  // Inverse azimuth rotation: standard ENU to rotated local frame
  const f64 cos_az = std::cos(anchor.azimuth);
  const f64 sin_az = std::sin(anchor.azimuth);

  glm::dvec3 local;
  local.x = cos_az * enu.x + sin_az * enu.y;
  local.y = -sin_az * enu.x + cos_az * enu.y;
  local.z = enu.z;

  return local;
}

// ============================================================================
// CRUD
// ============================================================================

bool AnchorManager::addAnchor(const std::string& id, const std::string& name,
                              const Geodetic& origin, f64 azimuth_rad,
                              const Ellipsoid& ellipsoid) {
  if (id.empty()) return false;

  CoordinateAnchor anchor;
  anchor.id = id;
  anchor.name = name;
  anchor.origin = origin;
  anchor.azimuth = azimuth_rad;
  anchor.origin_ecef = geodeticToECEF(origin, ellipsoid);

  anchors_[id] = std::move(anchor);
  return true;
}

bool AnchorManager::removeAnchor(const std::string& id) {
  return anchors_.erase(id) > 0;
}

const CoordinateAnchor* AnchorManager::getAnchor(const std::string& id) const {
  auto it = anchors_.find(id);
  return (it != anchors_.end()) ? &it->second : nullptr;
}

// ============================================================================
// Conversion
// ============================================================================

bool AnchorManager::convertBetweenAnchors(const glm::dvec3& local_pos,
                                           const std::string& from_id,
                                           const std::string& to_id,
                                           glm::dvec3& result) const {
  glm::dvec3 ecef;
  if (!localToECEF(local_pos, from_id, ecef)) return false;
  return ecefToLocal(ecef, to_id, result);
}

bool AnchorManager::localToECEF(const glm::dvec3& local_pos,
                                const std::string& anchor_id,
                                glm::dvec3& ecef) const {
  const auto* anchor = getAnchor(anchor_id);
  if (!anchor) return false;
  ecef = localToECEFInternal(local_pos, *anchor);
  return true;
}

bool AnchorManager::ecefToLocal(const glm::dvec3& ecef,
                                const std::string& anchor_id,
                                glm::dvec3& local_pos) const {
  const auto* anchor = getAnchor(anchor_id);
  if (!anchor) return false;
  local_pos = ecefToLocalInternal(ecef, *anchor);
  return true;
}

std::string AnchorManager::findNearestAnchor(const glm::dvec3& ecef) const {
  std::string nearest_id;
  f64 min_dist = std::numeric_limits<f64>::max();

  for (const auto& [id, anchor] : anchors_) {
    f64 dist = glm::length(ecef - anchor.origin_ecef);
    if (dist < min_dist) {
      min_dist = dist;
      nearest_id = id;
    }
  }

  return nearest_id;
}

// ============================================================================
// Serialization
// ============================================================================

nlohmann::json AnchorManager::toJson() const {
  nlohmann::json j = nlohmann::json::array();

  for (const auto& [id, anchor] : anchors_) {
    f64 lat_deg, lon_deg;
    anchor.origin.toDegrees(lat_deg, lon_deg);

    j.push_back({
        {"id", anchor.id},
        {"name", anchor.name},
        {"latitude_deg", lat_deg},
        {"longitude_deg", lon_deg},
        {"altitude_m", anchor.origin.alt},
        {"azimuth_deg", anchor.azimuth * RAD_TO_DEG},
    });
  }

  return j;
}

size_t AnchorManager::fromJson(const nlohmann::json& j, const Ellipsoid& ellipsoid) {
  size_t count = 0;

  for (const auto& item : j) {
    std::string id = item.at("id").get<std::string>();
    std::string name = item.value("name", "");
    f64 lat_deg = item.at("latitude_deg").get<f64>();
    f64 lon_deg = item.at("longitude_deg").get<f64>();
    f64 alt = item.value("altitude_m", 0.0);
    f64 azimuth_deg = item.value("azimuth_deg", 0.0);

    auto geo = Geodetic::fromDegrees(lat_deg, lon_deg, alt);
    if (addAnchor(id, name, geo, azimuth_deg * DEG_TO_RAD, ellipsoid)) {
      ++count;
    }
  }

  return count;
}

} // namespace hongtu::coordinate
