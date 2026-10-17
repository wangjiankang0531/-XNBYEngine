#include <hongtu/renderer/camera/camera_base.hpp>
#include <hongtu/renderer/picking/ray_caster.hpp>

#include <glm/gtc/matrix_transform.hpp>

namespace hongtu::renderer {

std::optional<PickHit>
CameraBase::pickWorld(double /*sx*/, double /*sy*/,
                      double /*sw*/, double /*sh*/) const {
  return std::nullopt;
}

glm::dmat4 CameraBase::getViewMatrix() const {
  return glm::lookAt(getEyeWorld(), getTargetWorld(), getUpWorld());
}

glm::mat4 CameraBase::getProjectionMatrix(f32 aspect) const {
  return glm::perspective(glm::radians(fovy_deg_), aspect, near_, far_);
}

glm::mat4 CameraBase::getRTCViewMatrix(const coordinate::PrecisionOrigin& origin) const {
  // Cast up to vec3 — the precision-origin view matrix is float32 throughout.
  glm::dvec3 up64 = getUpWorld();
  glm::vec3  up32(static_cast<f32>(up64.x),
                   static_cast<f32>(up64.y),
                   static_cast<f32>(up64.z));
  return origin.computeViewMatrix(getEyeWorld(), getTargetWorld(), up32);
}

}  // namespace hongtu::renderer
