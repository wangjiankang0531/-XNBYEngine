#include <hongtu/renderer/ssao/ssao_kernel.hpp>
#include <cmath>
#include <random>

namespace hongtu::renderer {

SSAOKernel::SSAOKernel(const SSAOConfig& config) : config_(config) {
  generateSamples();
  generateNoise();
}

void SSAOKernel::regenerate(const SSAOConfig& config) {
  config_ = config;
  generateSamples();
  generateNoise();
}

void SSAOKernel::generateSamples() {
  samples_.clear();
  samples_.reserve(config_.kernel_size);

  std::mt19937 rng(42); // Fixed seed for reproducibility
  std::uniform_real_distribution<f32> dist(0.0f, 1.0f);

  for (u32 i = 0; i < config_.kernel_size; ++i) {
    // Generate random point in hemisphere (tangent space, z >= 0)
    f32 xi1 = dist(rng);
    f32 xi2 = dist(rng);
    f32 xi3 = dist(rng);

    // Uniform hemisphere distribution using spherical coordinates
    f32 cos_theta = xi1;              // cos(theta) for hemisphere
    f32 sin_theta = std::sqrt(1.0f - cos_theta * cos_theta);
    f32 phi = 2.0f * 3.14159265f * xi2;

    glm::vec3 sample(
        sin_theta * std::cos(phi),
        sin_theta * std::sin(phi),
        cos_theta  // Always positive (hemisphere)
    );

    // Scale with accelerating interpolation (more samples closer to origin)
    f32 scale = static_cast<f32>(i) / static_cast<f32>(config_.kernel_size);
    scale = 0.1f + scale * scale * 0.9f; // lerp(0.1, 1.0, scale^2)

    // Random length within [0, 1], weighted by xi3
    sample *= xi3 * scale;

    samples_.push_back(sample);
  }
}

void SSAOKernel::generateNoise() {
  noise_.clear();
  u32 noise_count = config_.noise_size * config_.noise_size;
  noise_.reserve(noise_count);

  std::mt19937 rng(137); // Different seed from samples
  std::uniform_real_distribution<f32> dist(-1.0f, 1.0f);

  for (u32 i = 0; i < noise_count; ++i) {
    // Random rotation vectors in tangent space (around z-axis)
    glm::vec3 noise(dist(rng), dist(rng), 0.0f);
    noise = glm::normalize(noise);
    noise_.push_back(noise);
  }
}

bool SSAOKernel::validateDistribution() const {
  if (samples_.size() != config_.kernel_size) return false;
  if (noise_.size() != config_.noise_size * config_.noise_size) return false;

  // Check all samples are in hemisphere (z >= 0)
  for (const auto& s : samples_) {
    if (s.z < 0.0f) return false;
  }

  // Check all samples have length <= 1
  for (const auto& s : samples_) {
    if (glm::length(s) > 1.01f) return false; // Small tolerance
  }

  // Check noise vectors are normalized (length ~1) and z == 0
  for (const auto& n : noise_) {
    f32 len = glm::length(n);
    if (std::abs(len - 1.0f) > 0.01f) return false;
    if (std::abs(n.z) > 0.001f) return false;
  }

  // Check distribution: samples should cover both positive and negative x/y
  bool has_pos_x = false, has_neg_x = false;
  bool has_pos_y = false, has_neg_y = false;
  for (const auto& s : samples_) {
    if (s.x > 0.01f) has_pos_x = true;
    if (s.x < -0.01f) has_neg_x = true;
    if (s.y > 0.01f) has_pos_y = true;
    if (s.y < -0.01f) has_neg_y = true;
  }
  if (!has_pos_x || !has_neg_x || !has_pos_y || !has_neg_y) return false;

  return true;
}

} // namespace hongtu::renderer
