#pragma once

#include <hongtu/core/common.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace hongtu::renderer {

/// SSAO algorithm type.
enum class SSAOAlgorithm {
  SSAO,     ///< Basic screen-space ambient occlusion
  HBAO,     ///< Horizon-Based Ambient Occlusion
  GTAO,     ///< Ground Truth Ambient Occlusion
};

/// SSAO configuration parameters.
struct SSAOConfig {
  SSAOAlgorithm algorithm = SSAOAlgorithm::GTAO;
  u32 kernel_size = 64;         ///< Number of sample points in hemisphere
  f32 radius = 0.5f;            ///< Sampling radius in world units
  f32 bias = 0.025f;            ///< Depth bias to prevent self-occlusion
  f32 intensity = 1.0f;         ///< AO intensity multiplier
  f32 power = 2.0f;             ///< AO power curve
  u32 noise_size = 4;           ///< Noise texture dimension (noise_size x noise_size)
  bool half_resolution = false; ///< Compute at half resolution for performance
};

/// Pre-computed SSAO kernel and noise data (CPU side).
/// These are uploaded to GPU as uniforms/textures.
class SSAOKernel {
 public:
  explicit SSAOKernel(const SSAOConfig& config = {});

  /// Regenerate kernel with new config.
  void regenerate(const SSAOConfig& config);

  /// Get hemisphere sample points (tangent-space, z >= 0).
  /// Each sample is a vec3 with length in [0, 1], distributed with
  /// accelerating density towards the center.
  const std::vector<glm::vec3>& getSamples() const { return samples_; }

  /// Get noise texture data (vec3 tangent-space random rotation vectors).
  /// Size is noise_size * noise_size * 3 floats, tiled across screen.
  const std::vector<glm::vec3>& getNoiseTexture() const { return noise_; }

  /// Get current config.
  const SSAOConfig& getConfig() const { return config_; }

  /// Validate that the kernel is well-distributed.
  /// Returns true if all quality checks pass.
  bool validateDistribution() const;

 private:
  void generateSamples();
  void generateNoise();

  SSAOConfig config_;
  std::vector<glm::vec3> samples_;
  std::vector<glm::vec3> noise_;
};

} // namespace hongtu::renderer
