#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace hongtu::renderer::animation {

/// One bone in the skeleton. Joints form a tree via `parent` index (-1 = root).
/// `inverse_bind` maps mesh-space vertices into joint-local bind-pose space
/// (so a skinned vertex transforms via `joint_world * inverse_bind * v`).
///
/// `local_*` is the joint's bind-pose TRS relative to its parent. The
/// `AnimationSampler` may override these per frame to produce a `SkinPose`,
/// from which `forwardKinematics` builds world-space matrices.
struct Joint {
  std::string name;
  int         parent = -1;
  glm::mat4   inverse_bind {1.0f};
  glm::vec3   local_translation {0.0f};
  glm::quat   local_rotation    {1.0f, 0.0f, 0.0f, 0.0f};  // identity
  glm::vec3   local_scale       {1.0f};
};

/// Per-joint TRS overrides at a moment in time. AnimationSampler writes this;
/// forwardKinematics reads it. Bind-pose initialization is via
/// `AnimationSampler::bindPose`.
struct SkinPose {
  std::vector<glm::vec3> translations;
  std::vector<glm::quat> rotations;
  std::vector<glm::vec3> scales;
};

class Skeleton {
 public:
  Skeleton() = default;

  /// Insert a joint. Caller must ensure `parent` refers to an already-inserted
  /// joint (or -1 for root). Returns the new joint index.
  uint32_t addJoint(Joint j);

  size_t              jointCount() const { return joints_.size(); }
  const Joint&        joint(size_t i) const { return joints_[i]; }
  Joint&              joint(size_t i)       { return joints_[i]; }
  const std::vector<Joint>& joints() const { return joints_; }

  /// Global transform of the node ABOVE the root joint(s) — the "armature".
  /// glTF joint world transforms include every ancestor up to the scene root,
  /// but this joint hierarchy starts at the root joint, so any non-joint
  /// ancestor transform (commonly a Z-up↔Y-up armature rotation) must be
  /// applied separately or the bind pose won't reconstruct to identity
  /// (mesh renders rotated/lying down). The glTF loader sets this from the
  /// root joint node's parent; procedural skeletons leave it identity.
  void setRootTransform(const glm::mat4& m) { root_transform_ = m; }
  const glm::mat4& rootTransform() const { return root_transform_; }

  /// Build world-space joint matrices from `pose` (size must == jointCount()).
  /// Output is resized to `jointCount()`.
  void forwardKinematics(const SkinPose& pose, std::vector<glm::mat4>& out) const;

  /// Skin matrices for GPU upload: `world * inverse_bind`. Output sized to
  /// `jointCount()`. The vertex shader then does
  ///   `v_world = sum(weight_i * skin[joint_i] * model * v_local)`
  /// (model_matrix applied separately by the renderer).
  void skinMatrices(const SkinPose& pose, std::vector<glm::mat4>& out) const;

 private:
  std::vector<Joint> joints_;
  glm::mat4          root_transform_ {1.0f};
};

}  // namespace hongtu::renderer::animation
