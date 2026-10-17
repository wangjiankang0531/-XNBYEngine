#include <hongtu/renderer/ssr/ssr_system.hpp>
#include <algorithm>
#include <cmath>

namespace hongtu::renderer {

// ============================================================================
// HiZPyramid
// ============================================================================

void HiZPyramid::build(u32 width, u32 height, u32 max_mips) {
  levels_.clear();
  u32 w = width, h = height;
  for (u32 mip = 0; mip < max_mips && w > 0 && h > 0; ++mip) {
    levels_.push_back({w, h, mip});
    if (w == 1 && h == 1) break; // Can't subdivide further
    w = std::max(1u, w / 2);
    h = std::max(1u, h / 2);
  }
}

u32 HiZPyramid::selectMipForStepSize(f32 step_pixels) const {
  if (levels_.empty()) return 0;
  // Select mip where pixel footprint matches step size
  for (u32 i = 0; i < static_cast<u32>(levels_.size()); ++i) {
    f32 mip_pixel_size = static_cast<f32>(1u << i);
    if (mip_pixel_size >= step_pixels) return i;
  }
  return static_cast<u32>(levels_.size() - 1);
}

// ============================================================================
// SSRRayMarcher
// ============================================================================

SSRRayMarcher::SSRRayMarcher(const SSRConfig& config) : config_(config) {}

glm::vec3 SSRRayMarcher::reflectDirection(const glm::vec3& view_dir, const glm::vec3& normal) {
  return view_dir - 2.0f * glm::dot(view_dir, normal) * normal;
}

f32 SSRRayMarcher::roughnessBlurRadius(f32 roughness, f32 distance) {
  // Rougher surfaces produce more blurred reflections
  // Blur increases with both roughness and distance from reflector
  return roughness * roughness * distance * 0.5f;
}

f32 SSRRayMarcher::computeFallbackWeight(f32 roughness, f32 confidence, f32 edge_fade) {
  // Blend to environment probe when:
  // - roughness is high (SSR can't represent blurry reflections well)
  // - confidence is low (ray didn't hit anything or hit was uncertain)
  // - near screen edges (not enough data for reflection)
  f32 roughness_fade = glm::clamp((roughness - 0.3f) / 0.2f, 0.0f, 1.0f);
  f32 confidence_fade = 1.0f - confidence;
  f32 total = std::max({roughness_fade, confidence_fade, 1.0f - edge_fade});
  return glm::clamp(total, 0.0f, 1.0f);
}

f32 SSRRayMarcher::screenEdgeFade(const glm::vec2& uv, f32 fade_distance) {
  // Smooth fade to 0 at screen edges
  f32 fade_x = glm::clamp(std::min(uv.x, 1.0f - uv.x) / fade_distance, 0.0f, 1.0f);
  f32 fade_y = glm::clamp(std::min(uv.y, 1.0f - uv.y) / fade_distance, 0.0f, 1.0f);
  return fade_x * fade_y;
}

SSRRayResult SSRRayMarcher::marchRay(const glm::vec3& ray_origin_ss,
                                     const glm::vec3& ray_dir_ss,
                                     const glm::vec2& screen_size,
                                     const float* depth_buffer,
                                     u32 buf_width, u32 buf_height) const {
  SSRRayResult result{};
  result.hit = false;
  result.confidence = 0.0f;
  result.steps_taken = 0;

  if (!depth_buffer || buf_width == 0 || buf_height == 0) return result;

  glm::vec3 pos = ray_origin_ss;
  glm::vec3 dir = glm::normalize(ray_dir_ss);

  // Scale direction so we step roughly 1 pixel at a time
  f32 step_scale = config_.stride;

  for (u32 step = 0; step < config_.max_steps; ++step) {
    pos += dir * step_scale;
    result.steps_taken = step + 1;

    // Check bounds
    if (pos.x < 0 || pos.x >= screen_size.x ||
        pos.y < 0 || pos.y >= screen_size.y ||
        pos.z < 0 || pos.z > 1.0f) {
      break;
    }

    // Sample depth buffer
    u32 px = static_cast<u32>(pos.x);
    u32 py = static_cast<u32>(pos.y);
    px = std::min(px, buf_width - 1);
    py = std::min(py, buf_height - 1);
    f32 scene_depth = depth_buffer[py * buf_width + px];

    // Check intersection
    f32 depth_diff = pos.z - scene_depth;
    if (depth_diff > 0.0f && depth_diff < config_.thickness) {
      // Hit! Refine with binary search
      glm::vec3 lo = pos - dir * step_scale;
      glm::vec3 hi = pos;

      for (u32 bs = 0; bs < config_.binary_search_steps; ++bs) {
        glm::vec3 mid = (lo + hi) * 0.5f;
        u32 mx = std::min(static_cast<u32>(mid.x), buf_width - 1);
        u32 my = std::min(static_cast<u32>(mid.y), buf_height - 1);
        f32 mid_depth = depth_buffer[my * buf_width + mx];

        if (mid.z > mid_depth) {
          hi = mid;
        } else {
          lo = mid;
        }
      }

      glm::vec3 final_pos = (lo + hi) * 0.5f;
      result.hit = true;
      result.hit_uv = {final_pos.x / screen_size.x, final_pos.y / screen_size.y};
      result.hit_depth = final_pos.z;

      // Confidence based on depth difference precision
      f32 final_diff = std::abs(final_pos.z - scene_depth);
      result.confidence = 1.0f - glm::clamp(final_diff / config_.thickness, 0.0f, 1.0f);

      // Apply edge fade
      result.confidence *= screenEdgeFade(result.hit_uv);
      break;
    }

    // Accelerate step size for Hi-Z
    step_scale *= 1.05f;
  }

  return result;
}

} // namespace hongtu::renderer
