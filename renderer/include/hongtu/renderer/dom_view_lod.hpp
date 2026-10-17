#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace hongtu::renderer {
  struct DomViewFootprint {
    glm::vec3 focus{0};
    float meters_per_pixel = 0;
    bool valid = false;
  };

  // Intersect the displayed terrain, not an assumed sea-level plane. Upward rays
  // have no usable local footprint. Bounded marching also keeps horizon views finite.
  template <class Height>
  std::optional<glm::vec3> intersectDomTerrain(glm::vec3 eye, glm::vec3 direction, Height height) {
    if (!std::isfinite(direction.z) || direction.z >= -0.005f)
      return std::nullopt;
    direction = glm::normalize(direction);
    float previous = 0;
    if (eye.z <= height(eye.x, eye.y))
      return std::nullopt;
    for (float distance = 32; distance <= 12000; distance += 32) {
      auto point = eye + direction * distance;
      if (point.z <= height(point.x, point.y)) {
        float low = previous, high = distance;
        for (int i = 0; i < 18; ++i) {
          float middle = (low + high) * 0.5f;
          auto sample = eye + direction * middle;
          if (sample.z > height(sample.x, sample.y))
            low = middle;
          else
            high = middle;
        }
        return eye + direction * ((low + high) * 0.5f);
      }
      previous = distance;
    }
    return std::nullopt;
  }

  template <class Height>
  DomViewFootprint domViewFootprint(
      glm::vec3 eye, glm::vec3 target, float fov_degrees, float aspect, int viewport_height, Height height) {
    DomViewFootprint result;
    if (viewport_height <= 0 || !std::isfinite(aspect) || !std::isfinite(fov_degrees) || aspect <= 0 ||
        fov_degrees <= 1 || fov_degrees >= 175 || glm::length(target - eye) < 0.001f)
      return result;
    auto forward = glm::normalize(target - eye);
    auto side = glm::cross(forward, glm::vec3(0, 0, 1));
    if (glm::length(side) < 0.001f)
      side = {1, 0, 0};
    else
      side = glm::normalize(side);
    auto up = glm::normalize(glm::cross(side, forward));
    const float tangent = std::tan(glm::radians(fov_degrees) * 0.5f);
    auto ray = [&](float x, float y) {
      return glm::normalize(forward + side * x * tangent * aspect + up * y * tangent);
    };
    // Prefer the central foreground if the central ray misses terrain. All samples
    // are in the actual viewport; the smallest footprint protects nearby detail.
    for (float y : {0.0f, -0.35f, -0.65f}) {
      auto hit = intersectDomTerrain(eye, ray(0, y), height);
      auto next = intersectDomTerrain(eye, ray(2.0f / (viewport_height * aspect), y), height);
      if (!hit || !next)
        continue;
      float mpp = glm::length(*next - *hit);
      if (!std::isfinite(mpp) || mpp <= 0)
        continue;
      if (!result.valid) {
        result.focus = *hit;
        result.meters_per_pixel = mpp;
        result.valid = true;
      } else {
        result.meters_per_pixel = std::min(result.meters_per_pixel, mpp);
      }
    }
    return result;
  }

  inline int selectDomViewLod(float meters_per_pixel, int previous = -1) {
    constexpr std::array<float, 3> resolution{0.5f, 1.0f, 2.0f};
    if (!std::isfinite(meters_per_pixel) || meters_per_pixel <= 0)
      return previous >= 0 && previous < 3 ? previous : 2;
    if (previous < 0 || previous >= 3) {
      int lod = 0;
      while (lod < 2 && resolution[lod + 1] <= meters_per_pixel)
        ++lod;
      return lod;
    }
    // Refine at >1.15 pixels/texel; coarsen only below 0.8 to avoid oscillation.
    int lod = previous;
    while (lod > 0 && resolution[lod] > meters_per_pixel * 1.15f)
      --lod;
    while (lod < 2 && resolution[lod + 1] < meters_per_pixel * 0.8f)
      ++lod;
    return lod;
  }
} // namespace hongtu::renderer
