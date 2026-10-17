#pragma once

#include <hongtu/core/common.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/euler_angles.hpp>

namespace hongtu::core::math {

// Type aliases for common GLM types
using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using Vec4 = glm::vec4;

using IVec2 = glm::ivec2;
using IVec3 = glm::ivec3;
using IVec4 = glm::ivec4;

using Mat2 = glm::mat2;
using Mat3 = glm::mat3;
using Mat4 = glm::mat4;

using Quat = glm::quat;

// Common constants
constexpr f32 PI = 3.14159265359f;
constexpr f32 TWO_PI = 6.28318530718f;
constexpr f32 HALF_PI = 1.57079632679f;
constexpr f32 EPSILON = 1e-6f;

// Utility functions
constexpr f32 radians(f32 degrees) {
  return degrees * (PI / 180.0f);
}

constexpr f32 degrees(f32 radians) {
  return radians * (180.0f / PI);
}

template<typename T>
constexpr T clamp(T value, T min_val, T max_val) {
  return (value < min_val) ? min_val : (value > max_val) ? max_val : value;
}

template<typename T>
constexpr T lerp(T a, T b, f32 t) {
  return a + (b - a) * t;
}

} // namespace hongtu::core::math
