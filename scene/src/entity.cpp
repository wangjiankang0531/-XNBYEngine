#include <hongtu/scene/entity.hpp>

namespace hongtu::scene {

glm::mat4 Entity::getRTCModelMatrix(const coordinate::PrecisionOrigin& origin) const {
  glm::dmat4 model = getModelMatrix();

  // Extract the world translation (column 3) and re-express it as a float32
  // offset relative to the origin. The remaining columns (rotation + scale)
  // are cast to float32 directly — they are independent of world translation.
  glm::dvec3 world_translation(model[3][0], model[3][1], model[3][2]);
  glm::vec3  local_translation = origin.worldToLocal(world_translation);

  glm::mat4 rtc(static_cast<glm::mat4>(model));
  rtc[3][0] = local_translation.x;
  rtc[3][1] = local_translation.y;
  rtc[3][2] = local_translation.z;
  rtc[3][3] = 1.0f;
  return rtc;
}

}  // namespace hongtu::scene
