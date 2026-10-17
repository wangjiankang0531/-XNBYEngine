#pragma once

#include <hongtu/coordinate/geodetic.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace hongtu::coordinate {

/// A coordinate anchor defines a local coordinate system origin
/// in geodetic space, with an orientation (azimuth).
struct CoordinateAnchor {
  std::string id;          ///< Unique identifier
  std::string name;        ///< Human-readable name
  Geodetic origin;         ///< Geodetic position of anchor origin
  f64 azimuth;             ///< Azimuth angle (radians) from north, clockwise
  glm::dvec3 origin_ecef;  ///< Precomputed ECEF position of origin
};

/// Manages multiple local coordinate system anchors within a scene.
/// Supports CRUD operations, inter-anchor conversions, serialization, and nearest-anchor lookup.
class AnchorManager {
 public:
  AnchorManager() = default;

  /// Add or update an anchor. Returns false if parameters are invalid.
  bool addAnchor(const std::string& id, const std::string& name,
                 const Geodetic& origin, f64 azimuth_rad = 0.0,
                 const Ellipsoid& ellipsoid = WGS84);

  /// Remove an anchor by ID. Returns false if not found.
  bool removeAnchor(const std::string& id);

  /// Get an anchor by ID. Returns nullptr if not found.
  const CoordinateAnchor* getAnchor(const std::string& id) const;

  /// Get all anchors.
  const std::unordered_map<std::string, CoordinateAnchor>& getAnchors() const {
    return anchors_;
  }

  /// Number of anchors.
  size_t size() const { return anchors_.size(); }

  /// Convert a local position from one anchor's frame to another anchor's frame.
  /// Goes through ECEF as intermediate: local_A -> ECEF -> local_B.
  /// @param local_pos   Position in source anchor's local frame (East-North-Up).
  /// @param from_id     Source anchor ID.
  /// @param to_id       Target anchor ID.
  /// @param result      Output position in target anchor's local frame.
  /// @return true on success, false if anchor IDs not found.
  bool convertBetweenAnchors(const glm::dvec3& local_pos,
                             const std::string& from_id,
                             const std::string& to_id,
                             glm::dvec3& result) const;

  /// Convert a local position to ECEF.
  bool localToECEF(const glm::dvec3& local_pos, const std::string& anchor_id,
                   glm::dvec3& ecef) const;

  /// Convert an ECEF position to a local anchor frame.
  bool ecefToLocal(const glm::dvec3& ecef, const std::string& anchor_id,
                   glm::dvec3& local_pos) const;

  /// Find the nearest anchor to a given ECEF position.
  /// Returns empty string if no anchors exist.
  std::string findNearestAnchor(const glm::dvec3& ecef) const;

  /// Serialize all anchors to JSON.
  nlohmann::json toJson() const;

  /// Deserialize anchors from JSON. Returns number of anchors loaded.
  size_t fromJson(const nlohmann::json& j, const Ellipsoid& ellipsoid = WGS84);

  /// Clear all anchors.
  void clear() { anchors_.clear(); }

 private:
  /// Convert local ENU (with azimuth rotation) to ECEF.
  glm::dvec3 localToECEFInternal(const glm::dvec3& local_pos,
                                 const CoordinateAnchor& anchor) const;

  /// Convert ECEF to local ENU (with azimuth rotation).
  glm::dvec3 ecefToLocalInternal(const glm::dvec3& ecef,
                                 const CoordinateAnchor& anchor) const;

  std::unordered_map<std::string, CoordinateAnchor> anchors_;
};

} // namespace hongtu::coordinate
