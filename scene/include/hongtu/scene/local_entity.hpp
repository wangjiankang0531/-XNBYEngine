#pragma once

#include <hongtu/scene/entity.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace hongtu::scene {

/// Local-mode scene entity: a plain TRS transform in flat-world meters.
///
/// Unlike `GeoEntity`, there is no ellipsoid / geodetic / ENU machinery —
/// the model matrix is just translation × rotation × scale composed in
/// double precision. Suitable for CAD, indoor / building-scale, and other
/// non-earth scenes.
class LocalEntity : public Entity {
 public:
  LocalEntity() = default;

  void setPosition(const glm::dvec3& p) { position_ = p; }
  void setRotation(const glm::quat& q)  { rotation_ = q; }
  void setScale(const glm::vec3& s)     { scale_ = s; }
  void setScale(f32 uniform)            { scale_ = glm::vec3(uniform); }

  glm::dvec3 getPosition() const { return position_; }
  glm::quat  getRotation() const { return rotation_; }
  glm::vec3  getScale()    const { return scale_; }

  /// World-space model matrix = T * R * S in double precision.
  glm::dmat4 getModelMatrix() const override;

 private:
  glm::dvec3 position_{0.0, 0.0, 0.0};
  glm::quat  rotation_{1.0f, 0.0f, 0.0f, 0.0f};   // identity (w,x,y,z)
  glm::vec3  scale_{1.0f, 1.0f, 1.0f};
};

}  // namespace hongtu::scene
