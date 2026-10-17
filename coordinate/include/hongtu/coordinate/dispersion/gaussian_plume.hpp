#pragma once

#include <hongtu/core/common.hpp>
#include <glm/glm.hpp>

#include <vector>

namespace hongtu::coordinate::dispersion {

/// Pasquill–Gifford atmospheric stability class. Controls how fast the
/// plume spreads laterally (σy) and vertically (σz). A = sunny/light wind
/// (strong turbulence,quick spread); F = night/light wind (stable layer,
/// long thin plume).
enum class PasquillStability : u32 {
  A,   ///< very unstable (sunny daytime, light wind)
  B,   ///< unstable
  C,   ///< slightly unstable
  D,   ///< neutral (overcast / windy)
  E,   ///< slightly stable (night / moderate wind)
  F,   ///< very stable (clear night, light wind)
};

/// Configuration for a single Gaussian plume source.
struct GaussianPlumeConfig {
  glm::dvec3 source{0.0, 0.0, 1.0};   ///< source release point (meters)
  glm::dvec3 wind{1.0, 0.0, 0.0};     ///< wind velocity vector (m/s) — direction + magnitude
  double     emission_rate = 1.0;      ///< Q,质量释放速率(g/s)
  PasquillStability stability = PasquillStability::D;
};

/// 3D density grid output. `values[k * h * w + j * w + i]` is the
/// concentration at cell (i, j, k) where the grid spans `aabb_min..aabb_max`
/// inclusive of `aabb_min` exclusive of `aabb_max`. Values are normalized
/// to peak = 1.0 (callers multiply by their visual density scale at sample
/// time — keeps the texture format options open: R8_UNORM / R16F / etc.).
struct DensityGrid {
  uint32_t   width  = 0;
  uint32_t   height = 0;
  uint32_t   depth  = 0;
  glm::dvec3 aabb_min{0.0};
  glm::dvec3 aabb_max{0.0};
  std::vector<float> values;          ///< size = w*h*d, peak normalized to 1

  size_t indexOf(uint32_t i, uint32_t j, uint32_t k) const {
    return (static_cast<size_t>(k) * height + j) * width + i;
  }
  glm::dvec3 cellCenter(uint32_t i, uint32_t j, uint32_t k) const {
    glm::dvec3 ext = aabb_max - aabb_min;
    return aabb_min + glm::dvec3(
        (static_cast<double>(i) + 0.5) * ext.x / static_cast<double>(width),
        (static_cast<double>(j) + 0.5) * ext.y / static_cast<double>(height),
        (static_cast<double>(k) + 0.5) * ext.z / static_cast<double>(depth));
  }
};

/// Compute Pasquill σy and σz at downwind distance x meters.
/// Returns (sigma_y, sigma_z) in meters. Uses the rural Briggs / Pasquill
/// formulas — accurate enough for visualization of leaks at 10-1000 m scale.
glm::dvec2 pasquillSigmas(PasquillStability stab, double x_meters);

/// Sample the Gaussian plume concentration at world point `p`, given config.
/// Returns g/m³ (un-normalized). Coordinates are in the same space as
/// `config.source` (typically RTC). Returns 0 for points upwind of source.
double sampleGaussianPlume(const glm::dvec3& p, const GaussianPlumeConfig& cfg);

/// Build a 3D density grid by sampling the plume at the center of each cell.
/// Auto-normalizes peak to 1.0 (stores `value / peak`). Convenient for direct
/// upload to a R8_UNORM 3D texture (after `static_cast<uint8_t>(v * 255)`).
DensityGrid buildGaussianPlumeGrid(const GaussianPlumeConfig& cfg,
                                   const glm::dvec3& aabb_min,
                                   const glm::dvec3& aabb_max,
                                   uint32_t w, uint32_t h, uint32_t d);

/// Build a room-style smoke density grid by simple iterated diffusion.
/// Starts with `injection_rate` placed at `source` cell, then runs N
/// neighbor-average steps with `dt` per step. Useful for static indoor
/// smoke (not a fluid sim — just a smooth blob).
DensityGrid buildRoomSmokeGrid(const glm::dvec3& source,
                               const glm::dvec3& aabb_min,
                               const glm::dvec3& aabb_max,
                               uint32_t w, uint32_t h, uint32_t d,
                               uint32_t iterations,
                               float injection_rate = 1.0f);

}  // namespace hongtu::coordinate::dispersion
