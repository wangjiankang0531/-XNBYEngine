#include <hongtu/renderer/animation/animation_clip.hpp>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>

namespace hongtu::renderer::animation {

namespace {

float clampFrac(float t) { return std::clamp(t, 0.0f, 1.0f); }

template <typename T>
T lerpValue(const T& a, const T& b, float t) {
  return a + (b - a) * t;
}
inline glm::quat lerpValue(const glm::quat& a, const glm::quat& b, float t) {
  return glm::slerp(a, b, t);
}

/// CubicSpline keyframe layout per glTF 2.0: (in_tangent, value, out_tangent).
/// Hermite spline:
///   p(t) = (2t³-3t²+1)p₀ + (t³-2t²+t)·Δ·m₀ + (-2t³+3t²)p₁ + (t³-t²)·Δ·m₁
/// where m₀=out_tangent_at_key0, m₁=in_tangent_at_key1, Δ=t₁-t₀.
template <typename T>
T hermite(const T& p0, const T& m0, const T& p1, const T& m1,
          float dt, float frac) {
  float f2 = frac * frac;
  float f3 = f2 * frac;
  return  (2.0f*f3 - 3.0f*f2 + 1.0f) * p0
        + (f3      - 2.0f*f2 + frac) * (dt * m0)
        + (-2.0f*f3 + 3.0f*f2)        * p1
        + (f3      - f2)             * (dt * m1);
}

}  // namespace

template <typename T>
T AnimationChannel<T>::sample(float time) const {
  if (times.empty()) return T{};
  if (times.size() == 1 || time <= times.front()) {
    return interp == InterpolationKind::CubicSpline ? values[1] : values.front();
  }
  if (time >= times.back()) {
    return interp == InterpolationKind::CubicSpline
               ? values[(times.size() - 1) * 3 + 1]
               : values.back();
  }
  // Find key i such that times[i] <= time < times[i+1].
  auto it = std::upper_bound(times.begin(), times.end(), time);
  size_t i1 = static_cast<size_t>(it - times.begin());
  size_t i0 = i1 - 1;
  float  t0 = times[i0];
  float  t1 = times[i1];
  float  frac = clampFrac((time - t0) / (t1 - t0));

  if (interp == InterpolationKind::Step) {
    return values[i0];
  }
  if (interp == InterpolationKind::Linear) {
    return lerpValue(values[i0], values[i1], frac);
  }
  // CubicSpline — values laid out as (in0, v0, out0, in1, v1, out1, ...)
  const T& p0 = values[i0 * 3 + 1];
  const T& m0 = values[i0 * 3 + 2];   // out_tangent of key i0
  const T& m1 = values[i1 * 3 + 0];   // in_tangent of key i1
  const T& p1 = values[i1 * 3 + 1];
  return hermite(p0, m0, p1, m1, t1 - t0, frac);
}

// Explicit instantiations — keep type list small.
template glm::vec3 AnimationChannel<glm::vec3>::sample(float) const;
template glm::quat AnimationChannel<glm::quat>::sample(float) const;

// =============================================================================
// AnimationSampler
// =============================================================================

void AnimationSampler::bindPose(const Skeleton& skel, SkinPose& pose) {
  pose.translations.resize(skel.jointCount());
  pose.rotations.resize(skel.jointCount());
  pose.scales.resize(skel.jointCount());
  for (size_t i = 0; i < skel.jointCount(); ++i) {
    pose.translations[i] = skel.joint(i).local_translation;
    pose.rotations[i]    = skel.joint(i).local_rotation;
    pose.scales[i]       = skel.joint(i).local_scale;
  }
}

void AnimationSampler::sample(const Skeleton& skel,
                               const AnimationClip& clip,
                               float time,
                               SkinPose& pose) {
  // Make sure pose is sized; if not, fall back to bind pose.
  if (pose.translations.size() != skel.jointCount()) bindPose(skel, pose);

  for (const auto& ch : clip.translation_channels) {
    if (ch.joint_index < 0 ||
        static_cast<size_t>(ch.joint_index) >= pose.translations.size()) continue;
    pose.translations[ch.joint_index] = ch.sample(time);
  }
  for (const auto& ch : clip.rotation_channels) {
    if (ch.joint_index < 0 ||
        static_cast<size_t>(ch.joint_index) >= pose.rotations.size()) continue;
    pose.rotations[ch.joint_index] = glm::normalize(ch.sample(time));
  }
  for (const auto& ch : clip.scale_channels) {
    if (ch.joint_index < 0 ||
        static_cast<size_t>(ch.joint_index) >= pose.scales.size()) continue;
    pose.scales[ch.joint_index] = ch.sample(time);
  }
}

}  // namespace hongtu::renderer::animation
