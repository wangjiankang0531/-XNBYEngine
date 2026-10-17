#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <vector>

namespace hongtu::scene {

/// Interpolation between adjacent keyframes.
enum class Interp {
  Step,         ///< Hold previous value until next key (great for bool / enable flags)
  Linear,       ///< Straight lerp
  EaseInOut,    ///< Smoothstep — cosine-ish curve for camera moves
};

/// One key in a Track<T>. `interp` controls how to ramp FROM this key to the
/// next; the last key's interp is irrelevant.
template <typename T>
struct Keyframe {
  float  time;
  T      value;
  Interp interp = Interp::Linear;
};

namespace detail {

inline float easeInOut(float t) {
  // Smoothstep: 3t² - 2t³
  return t * t * (3.0f - 2.0f * t);
}

template <typename T>
T lerpValue(const T& a, const T& b, float t) {
  return a + (b - a) * t;
}

// bool specialization — never interpolates, just steps.
inline bool lerpValue(bool a, bool b, float t) { return t < 1.0f ? a : b; }

}  // namespace detail

/// Time-ordered array of keyframes with `sample(t)` returning the value at any
/// time. Outside the key range, clamps to the first / last key's value. Bool
/// tracks always step (Linear / EaseInOut ignored).
///
/// Supported T:
///   - float / glm::vec2 / glm::vec3 / glm::vec4
///   - bool (always Step)
///   - any T with `operator+(T,T)` / `operator-(T,T)` / `operator*(T,scalar)`
template <typename T>
class Track {
 public:
  void addKey(float t, T v, Interp i = Interp::Linear) {
    Keyframe<T> k{t, std::move(v), i};
    auto it = std::lower_bound(keys_.begin(), keys_.end(), k,
                               [](const Keyframe<T>& a, const Keyframe<T>& b) {
                                 return a.time < b.time;
                               });
    keys_.insert(it, std::move(k));
  }

  bool   empty() const  { return keys_.empty(); }
  size_t size() const   { return keys_.size(); }
  float  duration() const { return keys_.empty() ? 0.0f : keys_.back().time; }
  const std::vector<Keyframe<T>>& keys() const { return keys_; }
  void clear() { keys_.clear(); }

  /// Sample at time `t`. Clamps to first / last key value outside range.
  /// Empty tracks return a default-constructed T.
  T sample(float t) const {
    if (keys_.empty()) return T{};
    if (t <= keys_.front().time) return keys_.front().value;
    if (t >= keys_.back().time)  return keys_.back().value;

    // Find first key with time > t — adjacent pair is [it-1, it].
    auto it = std::upper_bound(keys_.begin(), keys_.end(), t,
                               [](float v, const Keyframe<T>& k) {
                                 return v < k.time;
                               });
    const auto& a = *(it - 1);
    const auto& b = *it;
    if (a.interp == Interp::Step) return a.value;
    float u = (b.time - a.time) > 0.0f
                  ? (t - a.time) / (b.time - a.time)
                  : 0.0f;
    if (a.interp == Interp::EaseInOut) u = detail::easeInOut(u);
    return detail::lerpValue(a.value, b.value, u);
  }

 private:
  std::vector<Keyframe<T>> keys_;
};

}  // namespace hongtu::scene
