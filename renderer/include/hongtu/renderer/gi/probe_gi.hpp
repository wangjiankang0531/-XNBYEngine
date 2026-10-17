#pragma once

#include <hongtu/core/common.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <array>

namespace hongtu::renderer {

/// Spherical harmonics coefficients (L2, 9 coefficients per color channel).
struct SH9 {
  std::array<glm::vec3, 9> coeffs{};

  /// Evaluate SH at a given direction.
  glm::vec3 evaluate(const glm::vec3& dir) const;

  /// Add a directional sample to the SH.
  void addSample(const glm::vec3& dir, const glm::vec3& color, f32 weight = 1.0f);

  /// Scale all coefficients.
  SH9 operator*(f32 s) const;
  SH9 operator+(const SH9& other) const;

  /// Reset to zero.
  void clear();
};

/// Compute SH basis functions for a direction (L2, 9 values).
std::array<f32, 9> shBasis(const glm::vec3& dir);

/// A single light probe in the grid.
struct LightProbe {
  glm::vec3 position;      ///< World-space position
  SH9 irradiance;          ///< Irradiance stored as L2 SH
  f32 depth_mean = 0.0f;   ///< Mean depth for occlusion
  f32 depth_variance = 0.0f;///< Depth variance for soft shadows
  bool valid = false;       ///< Whether this probe has been baked
  bool needs_update = false;///< Whether this probe needs re-baking
};

/// Configuration for the probe grid.
struct ProbeGridConfig {
  glm::ivec3 grid_size = {8, 4, 8};    ///< Number of probes in x, y, z
  glm::vec3 grid_origin = {0, 0, 0};   ///< World-space origin of grid
  glm::vec3 grid_spacing = {4, 3, 4};  ///< Distance between probes (meters)
  u32 rays_per_probe = 64;             ///< Rays traced per probe update
  f32 hysteresis = 0.97f;              ///< Temporal blend factor for stability
  f32 normal_bias = 0.2f;              ///< Bias along normal to prevent self-illumination
  f32 view_bias = 0.3f;                ///< Bias along view direction
};

/// Light probe grid for global illumination.
class ProbeGrid {
 public:
  explicit ProbeGrid(const ProbeGridConfig& config = {});

  /// Initialize the grid with probe positions.
  void initialize();

  /// Get grid configuration.
  const ProbeGridConfig& getConfig() const { return config_; }

  /// Resize and reinitialize the grid.
  void resize(const ProbeGridConfig& config);

  /// Get total number of probes.
  u32 getProbeCount() const;

  /// Get a probe by 3D grid index.
  LightProbe& getProbe(int x, int y, int z);
  const LightProbe& getProbe(int x, int y, int z) const;

  /// Get a probe by linear index.
  LightProbe& getProbe(u32 index);
  const LightProbe& getProbe(u32 index) const;

  /// Convert 3D grid index to linear index.
  u32 gridToLinear(int x, int y, int z) const;

  /// Convert linear index to 3D grid index.
  glm::ivec3 linearToGrid(u32 index) const;

  /// Get the world-space position of a probe.
  glm::vec3 probePosition(int x, int y, int z) const;

  /// Find the 8 surrounding probes and interpolation weights for a world position.
  /// Returns false if position is outside the grid.
  bool findSurroundingProbes(const glm::vec3& world_pos,
                             std::array<u32, 8>& probe_indices,
                             glm::vec3& weights) const;

  /// Sample irradiance at a world position by trilinear interpolation of probes.
  /// @param world_pos   World-space sample position
  /// @param normal      Surface normal (for SH evaluation)
  glm::vec3 sampleIrradiance(const glm::vec3& world_pos,
                             const glm::vec3& normal) const;

  /// Get list of probes that need updating (round-robin schedule).
  /// @param max_count   Maximum number of probes to update per frame.
  /// @param frame       Current frame number (for scheduling).
  std::vector<u32> getProbesForUpdate(u32 max_count, u32 frame) const;

  /// Mark all probes as needing update.
  void invalidateAll();

  /// Get all probes (read-only).
  const std::vector<LightProbe>& getProbes() const { return probes_; }

  /// Get the axis-aligned bounding box of the grid.
  void getBounds(glm::vec3& min_bound, glm::vec3& max_bound) const;

 private:
  ProbeGridConfig config_;
  std::vector<LightProbe> probes_;
};

} // namespace hongtu::renderer
