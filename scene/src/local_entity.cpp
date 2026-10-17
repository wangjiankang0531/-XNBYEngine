#include <hongtu/scene/local_entity.hpp>

#include <glm/gtc/matrix_transform.hpp>

namespace hongtu::scene {

glm::dmat4 LocalEntity::getModelMatrix() const {
  // T * R * S composed in double precision. Rotation is cast from f32 quat
  // to f64 mat4 — sufficient precision for any practical local scene.
  glm::dmat4 T = glm::translate(glm::dmat4(1.0), position_);
  glm::dmat4 R = static_cast<glm::dmat4>(glm::mat4_cast(rotation_));
  glm::dmat4 S = glm::scale(glm::dmat4(1.0),
                            glm::dvec3(static_cast<f64>(scale_.x),
                                       static_cast<f64>(scale_.y),
                                       static_cast<f64>(scale_.z)));
  return T * R * S;
}

}  // namespace hongtu::scene
