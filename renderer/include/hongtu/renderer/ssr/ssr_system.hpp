#pragma once

#include <hongtu/core/common.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace hongtu::renderer {

/// SSR quality preset.
enum class SSRQuality {
  Low,      ///< 32 steps, half-res
  Medium,   ///< 64 steps, full-res
  High,     ///< 128 steps, full-res
  Ultra,    ///< 256 steps, full-res with multi-bounce
};

/// SSR configuration.
struct SSRConfig {
  SSRQuality quality = SSRQuality::Medium;
  u32 max_steps = 64;            ///< Maximum ray march steps
  u32 binary_search_steps = 8;   ///< Binary search refinement steps
  f32 max_distance = 100.0f;     ///< Maximum ray travel distance (world units)
  f32 thickness = 0.5f;          ///< Depth buffer thickness tolerance
  f32 stride = 1.0f;             ///< Step stride multiplier (>1 for Hi-Z acceleration)
  f32 jitter = 1.0f;             ///< Temporal jitter intensity [0, 1]
  f32 roughness_threshold = 0.5f;///< Max roughness for SSR (fallback to probe above this)
  bool half_resolution = false;  ///< Compute at half resolution
  u32 hi_z_mip_count = 5;        ///< Number of Hi-Z mip levels
};

/// Hi-Z mip level info for hierarchical ray marching.
struct HiZLevel {
  u32 width;
  u32 height;
  u32 mip;
};

/// Result of a single screen-space ray march.
struct SSRRayResult {
  bool hit;                ///< Whether the ray hit a surface
  glm::vec2 hit_uv;       ///< UV coordinates of hit point on screen
  f32 hit_depth;           ///< Depth at hit point
  f32 confidence;          ///< Reflection confidence [0, 1]
  u32 steps_taken;         ///< Number of steps taken
};

/// Hi-Z pyramid for accelerated screen-space ray marching.
class HiZPyramid {
 public:
  /// Build mip chain info for given screen dimensions.
  void build(u32 width, u32 height, u32 max_mips);

  /// Get mip level info.
  const std::vector<HiZLevel>& getLevels() const { return levels_; }

  /// Get mip count.
  u32 getMipCount() const { return static_cast<u32>(levels_.size()); }

  /// Select appropriate mip level for a given step size.
  /// Larger step -> higher mip (coarser, faster).
  u32 selectMipForStepSize(f32 step_pixels) const;

 private:
  std::vector<HiZLevel> levels_;
};

/// Screen-space ray marching (CPU reference / logic).
class SSRRayMarcher {
 public:
  explicit SSRRayMarcher(const SSRConfig& config = {});

  /// Set configuration.
  void setConfig(const SSRConfig& config) { config_ = config; }
  const SSRConfig& getConfig() const { return config_; }

  /// March a ray in screen space (CPU reference implementation).
  /// @param ray_origin_ss   Ray origin in screen space (x, y in pixels, z = depth)
  /// @param ray_dir_ss      Ray direction in screen space
  /// @param screen_size     Screen dimensions
  /// @param depth_buffer    Depth buffer (row-major, [0,1] depth)
  /// @param buf_width       Depth buffer width
  /// @param buf_height      Depth buffer height
  SSRRayResult marchRay(const glm::vec3& ray_origin_ss,
                        const glm::vec3& ray_dir_ss,
                        const glm::vec2& screen_size,
                        const float* depth_buffer,
                        u32 buf_width, u32 buf_height) const;

  /// Compute reflection direction in view space.
  /// @param view_dir   View direction (from camera to surface, normalized)
  /// @param normal     Surface normal in view space (normalized)
  static glm::vec3 reflectDirection(const glm::vec3& view_dir, const glm::vec3& normal);

  /// Compute roughness-based blur radius for reflected color.
  /// @param roughness  Surface roughness [0, 1]
  /// @param distance   Distance from reflection point to camera
  static f32 roughnessBlurRadius(f32 roughness, f32 distance);

  /// Determine fallback weight: how much to blend SSR vs environment probe.
  /// Returns 0.0 when fully SSR, 1.0 when fully probe fallback.
  /// @param roughness       Surface roughness
  /// @param confidence      SSR ray confidence
  /// @param edge_fade       Fade factor near screen edges
  static f32 computeFallbackWeight(f32 roughness, f32 confidence, f32 edge_fade);

  /// Compute edge fade factor (0 at edges, 1 at center).
  /// Prevents artifacts at screen borders.
  static f32 screenEdgeFade(const glm::vec2& uv, f32 fade_distance = 0.1f);

 private:
  SSRConfig config_;
};

} // namespace hongtu::renderer
