#pragma once

#include <hongtu/coordinate/rtc_transform.hpp>
#include <glm/glm.hpp>

namespace hongtu::scene {

/// Abstract scene entity — anything that produces a world-space (f64) model
/// matrix and can therefore be rendered. Both the globe-mode `GeoEntity`
/// (positions a model at a geodetic coordinate on an ellipsoid) and the
/// local-mode `LocalEntity` (plain TRS in flat meters) derive from this.
///
/// Subclasses only need to implement `getModelMatrix()`. The shared
/// `getRTCModelMatrix(origin)` casts to float32 with origin-relative
/// translation so GPU shaders avoid float precision loss at large world coords.
class Entity {
 public:
  virtual ~Entity() = default;

  Entity(const Entity&)            = delete;
  Entity& operator=(const Entity&) = delete;
  Entity(Entity&&) noexcept            = default;
  Entity& operator=(Entity&&) noexcept = default;

  /// Model → world transform in double precision.
  virtual glm::dmat4 getModelMatrix() const = 0;

  /// Model → world transform expressed relative to a precision origin and
  /// cast to float32. Suitable for direct GPU upload. The rotation/scale
  /// part is the same as `getModelMatrix()` cast to float32; the translation
  /// column is `worldToLocal(origin)` of the world translation.
  glm::mat4 getRTCModelMatrix(const coordinate::PrecisionOrigin& origin) const;

 protected:
  Entity() = default;
};

}  // namespace hongtu::scene
