#pragma once

#include <hongtu/coordinate/ellipsoid.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/scene/entity.hpp>
#include <glm/glm.hpp>

namespace hongtu::scene {

using hongtu::coordinate::Ellipsoid;
using hongtu::coordinate::Geodetic;
using hongtu::coordinate::RTCOrigin;
using hongtu::coordinate::WGS84;

/// GeoEntity positions a 3D model on the earth's surface using geodetic
/// coordinates (latitude, longitude, altitude). It computes the model matrix
/// that includes ENU (East-North-Up) rotation so the model stands upright
/// on the surface.
class GeoEntity : public Entity {
 public:
  // Expose the base's RTC overload so callers can use `getRTCModelMatrix(origin)`
  // alongside the earth-flavoured `getRTCModelMatrix(origin, ellipsoid)` below.
  using Entity::getRTCModelMatrix;

  GeoEntity() = default;

  /// Set the geodetic position (lat/lon in radians, altitude in meters).
  void setPosition(const Geodetic& pos);

  /// Get the current geodetic position.
  Geodetic getPosition() const;

  /// Set uniform scale.
  void setScale(f32 uniform);

  /// Set non-uniform scale.
  void setScale(const glm::vec3& s);

  /// Set heading (yaw) rotation in degrees. Rotates around the local up axis.
  void setHeading(f32 degrees);

  /// Compute the full model matrix in ECEF coordinates.
  /// The matrix places the entity at the geodetic position with ENU orientation,
  /// heading rotation, and scale applied.
  glm::dmat4 getModelMatrix(const Ellipsoid& ellipsoid = WGS84) const;

  /// Entity override — forwards to the earth-flavoured overload using WGS84.
  glm::dmat4 getModelMatrix() const override { return getModelMatrix(WGS84); }

  /// Compute the model matrix in RTC (Relative-to-Center) space for GPU rendering.
  /// The rotation/scale part is cast to float32, and the translation is the
  /// RTC-relative offset (float32) to avoid precision issues.
  glm::mat4 getRTCModelMatrix(const RTCOrigin& rtc_origin,
                               const Ellipsoid& ellipsoid = WGS84) const;

 private:
  Geodetic position_{0.0, 0.0, 0.0};
  glm::vec3 scale_{1.0f, 1.0f, 1.0f};
  f32 heading_degrees_{0.0f};
};

}  // namespace hongtu::scene
