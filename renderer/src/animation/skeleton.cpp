#include <hongtu/renderer/animation/skeleton.hpp>

#include <glm/gtc/matrix_transform.hpp>

namespace hongtu::renderer::animation {

uint32_t Skeleton::addJoint(Joint j) {
  joints_.push_back(std::move(j));
  return static_cast<uint32_t>(joints_.size() - 1);
}

namespace {

glm::mat4 trs(const glm::vec3& t, const glm::quat& r, const glm::vec3& s) {
  glm::mat4 m = glm::mat4_cast(r);
  m[0] *= s.x; m[1] *= s.y; m[2] *= s.z;
  m[3] = glm::vec4(t, 1.0f);
  return m;
}

}  // namespace

void Skeleton::forwardKinematics(const SkinPose& pose,
                                  std::vector<glm::mat4>& out) const {
  out.assign(joints_.size(), glm::mat4(1.0f));
  for (size_t i = 0; i < joints_.size(); ++i) {
    const auto& j = joints_[i];
    glm::mat4 local = trs(pose.translations[i], pose.rotations[i], pose.scales[i]);
    if (j.parent >= 0 && static_cast<size_t>(j.parent) < i) {
      // Parent is always earlier in the array (joints inserted in topological
      // order by addJoint contract).
      out[i] = out[j.parent] * local;
    } else {
      // Root joint: prefix with the armature/skeleton-root global transform
      // (identity unless the loader set one — see Skeleton::setRootTransform).
      out[i] = root_transform_ * local;
    }
  }
}

void Skeleton::skinMatrices(const SkinPose& pose,
                             std::vector<glm::mat4>& out) const {
  std::vector<glm::mat4> world;
  forwardKinematics(pose, world);
  out.resize(joints_.size());
  for (size_t i = 0; i < joints_.size(); ++i) {
    out[i] = world[i] * joints_[i].inverse_bind;
  }
}

}  // namespace hongtu::renderer::animation
