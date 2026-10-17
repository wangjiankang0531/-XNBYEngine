#pragma once

#include <hongtu/renderer/animation/skeleton.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace hongtu::renderer::animation {

enum class InterpolationKind {
  Step,
  Linear,
  CubicSpline,
};

/// One animation channel = stream of keyframes for one joint, one TRS field
/// (translation / rotation / scale).
///
/// For Step / Linear: `values.size() == times.size()`, one value per key.
/// For CubicSpline: `values.size() == 3 * times.size()`, layout per key is
/// (in_tangent, value, out_tangent) — matching glTF 2.0 convention.
template <typename T>
struct AnimationChannel {
  int                  joint_index = -1;
  std::vector<float>   times;
  std::vector<T>       values;
  InterpolationKind    interp = InterpolationKind::Linear;

  /// Returns the keyframe value at `time` (clamped to [first, last] outside
  /// the key range). Caller must size the returned T correctly for their
  /// type (we use plain T with operator+/-/* for vec3/scaler types and
  /// custom slerp for quat).
  T sample(float time) const;
};

/// A named clip is a set of channels — translations / rotations / scales —
/// for any subset of joints. Joints not mentioned in any channel keep their
/// bind-pose values.
struct AnimationClip {
  std::string                          name;
  float                                duration_seconds = 0.0f;
  std::vector<AnimationChannel<glm::vec3>> translation_channels;
  std::vector<AnimationChannel<glm::quat>> rotation_channels;
  std::vector<AnimationChannel<glm::vec3>> scale_channels;
};

class AnimationSampler {
 public:
  /// Fill `pose` with the skeleton's bind-pose local TRS. Resizes pose if
  /// needed.
  static void bindPose(const Skeleton& skel, SkinPose& pose);

  /// Sample `clip` at `time` into `pose`. `pose` must already be sized to
  /// `skel.jointCount()` (typically via a prior `bindPose` call). Only joints
  /// with channels are overridden; others keep their bind-pose value.
  static void sample(const Skeleton& skel, const AnimationClip& clip,
                     float time, SkinPose& pose);
};

}  // namespace hongtu::renderer::animation
