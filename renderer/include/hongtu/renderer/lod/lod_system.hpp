#pragma once

#include <hongtu/core/common.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <string>

namespace hongtu::renderer {

/// LOD level configuration.
struct LODLevel {
  u32 level;                ///< LOD level index (0 = highest detail)
  f32 screen_area_min;      ///< Minimum screen area ratio to use this level
  f32 triangle_ratio;       ///< Target triangle ratio relative to LOD 0 (1.0 = full, 0.05 = 5%)
};

/// LOD selection result.
struct LODSelection {
  u32 level;                ///< Selected LOD level
  f32 screen_area;          ///< Computed screen area ratio
  f32 blend_factor;         ///< Cross-fade factor (0 = fully current, 1 = fully next)
};

/// Configuration for the LOD system.
struct LODConfig {
  u32 max_levels = 5;                   ///< Maximum number of LOD levels
  f32 bias = 0.0f;                      ///< LOD bias (positive = lower quality, negative = higher)
  f32 hysteresis = 0.1f;                ///< Hysteresis factor to prevent oscillation
  bool enable_crossfade = true;         ///< Enable cross-fade transitions
  f32 crossfade_range = 0.1f;           ///< Cross-fade blending range as fraction of threshold
};

/// Manages LOD level selection based on screen-space metrics.
class LODSelector {
 public:
  explicit LODSelector(const LODConfig& config = {});

  /// Set default LOD level thresholds for N levels.
  /// Generates screen_area thresholds: 0.25, 0.0625, 0.015625, ...
  void setDefaultThresholds(u32 num_levels);

  /// Set custom LOD levels.
  void setLevels(const std::vector<LODLevel>& levels);

  /// Get the configured LOD levels.
  const std::vector<LODLevel>& getLevels() const { return levels_; }

  /// Select the appropriate LOD level based on screen area.
  /// @param bounding_sphere_radius  Radius of object's bounding sphere in world units.
  /// @param distance_to_camera      Distance from object center to camera in world units.
  /// @param fov_y                   Vertical field of view in radians.
  /// @param screen_height           Screen height in pixels.
  LODSelection selectLOD(f32 bounding_sphere_radius, f32 distance_to_camera,
                         f32 fov_y, f32 screen_height) const;

  /// Compute screen-space projected area ratio (0-1) for a sphere.
  static f32 computeScreenArea(f32 radius, f32 distance, f32 fov_y, f32 screen_height);

  /// Compute a fetch priority for a 3D-Tiles-style tile from its declared
  /// `geometric_error` (Cesium semantics: a tile with geometricError ≥ K
  /// can be sub-divided once the projected error exceeds K screen pixels).
  ///
  /// Returns a non-negative score where **higher = more urgent**. The
  /// formula matches the standard 3D Tiles SSE estimate:
  ///   priority = geometric_error * screen_height
  ///            / (distance * 2 * tan(fov_y / 2))
  ///
  /// Suitable for plugging directly into `TileScheduler::Fetcher` priority
  /// sort. With `geometric_error = bounding_radius`, this collapses to the
  /// same screen-area formula as `computeScreenArea`.
  static f32 priorityFromScreenArea(f32 geometric_error, f32 distance,
                                    f32 fov_y, f32 screen_height);

  /// Get/set config.
  const LODConfig& getConfig() const { return config_; }
  void setConfig(const LODConfig& config) { config_ = config; }

 private:
  LODConfig config_;
  std::vector<LODLevel> levels_;
};

/// A group of meshes at different LOD levels.
struct LODGroup {
  std::string name;
  std::vector<u32> mesh_indices;     ///< Index of mesh data for each LOD level
  std::vector<u32> triangle_counts;  ///< Triangle count at each level
  f32 bounding_radius = 0.0f;       ///< Bounding sphere radius
};

/// Simple Quadric Error Metric (QEM) mesh simplification.
/// Reduces triangle count while preserving visual quality.
namespace qem {

struct SimplifiedMesh {
  std::vector<glm::vec3> vertices;
  std::vector<u32> indices;
  u32 triangle_count;
};

/// Simplify a mesh to target triangle count using QEM.
/// @param vertices       Input vertex positions.
/// @param indices        Input triangle indices.
/// @param target_ratio   Target triangle ratio (0.5 = half, 0.25 = quarter).
/// @return Simplified mesh data.
SimplifiedMesh simplify(const std::vector<glm::vec3>& vertices,
                        const std::vector<u32>& indices,
                        f32 target_ratio);

/// Generate a complete LOD chain (multiple levels) from a base mesh.
/// @param vertices    Input vertex positions.
/// @param indices     Input triangle indices.
/// @param num_levels  Number of LOD levels to generate (including base).
/// @param ratios      Triangle ratios for each level (e.g., {1.0, 0.5, 0.25, 0.125, 0.05}).
std::vector<SimplifiedMesh> generateLODChain(const std::vector<glm::vec3>& vertices,
                                              const std::vector<u32>& indices,
                                              const std::vector<f32>& ratios);

} // namespace qem

} // namespace hongtu::renderer
