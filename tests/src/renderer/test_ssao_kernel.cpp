#include <gtest/gtest.h>
#include <hongtu/renderer/ssao/ssao_kernel.hpp>
#include <cmath>
#include <numeric>

using namespace hongtu::renderer;

TEST(SSAOKernelTest, DefaultConstruction) {
  SSAOKernel kernel;
  EXPECT_EQ(kernel.getSamples().size(), 64u);
  EXPECT_EQ(kernel.getNoiseTexture().size(), 16u); // 4x4
}

TEST(SSAOKernelTest, CustomKernelSize) {
  SSAOConfig config;
  config.kernel_size = 32;
  config.noise_size = 8;
  SSAOKernel kernel(config);
  EXPECT_EQ(kernel.getSamples().size(), 32u);
  EXPECT_EQ(kernel.getNoiseTexture().size(), 64u); // 8x8
}

TEST(SSAOKernelTest, SamplesInHemisphere) {
  SSAOKernel kernel;
  for (const auto& s : kernel.getSamples()) {
    EXPECT_GE(s.z, 0.0f) << "Sample below hemisphere: " << s.x << "," << s.y << "," << s.z;
  }
}

TEST(SSAOKernelTest, SampleLengthBounded) {
  SSAOKernel kernel;
  for (const auto& s : kernel.getSamples()) {
    float len = glm::length(s);
    EXPECT_LE(len, 1.01f) << "Sample too long: " << len;
  }
}

TEST(SSAOKernelTest, NoiseNormalized) {
  SSAOKernel kernel;
  for (const auto& n : kernel.getNoiseTexture()) {
    float len = glm::length(n);
    EXPECT_NEAR(len, 1.0f, 0.01f);
    EXPECT_NEAR(n.z, 0.0f, 0.001f); // Noise is in tangent plane
  }
}

TEST(SSAOKernelTest, ValidateDistribution) {
  SSAOKernel kernel;
  EXPECT_TRUE(kernel.validateDistribution());
}

TEST(SSAOKernelTest, Regenerate) {
  SSAOKernel kernel;
  auto original = kernel.getSamples();

  SSAOConfig new_config;
  new_config.kernel_size = 16;
  kernel.regenerate(new_config);

  EXPECT_EQ(kernel.getSamples().size(), 16u);
  EXPECT_TRUE(kernel.validateDistribution());
}

TEST(SSAOKernelTest, Reproducible) {
  SSAOKernel k1, k2;
  EXPECT_EQ(k1.getSamples().size(), k2.getSamples().size());
  for (size_t i = 0; i < k1.getSamples().size(); ++i) {
    EXPECT_FLOAT_EQ(k1.getSamples()[i].x, k2.getSamples()[i].x);
    EXPECT_FLOAT_EQ(k1.getSamples()[i].y, k2.getSamples()[i].y);
    EXPECT_FLOAT_EQ(k1.getSamples()[i].z, k2.getSamples()[i].z);
  }
}

TEST(SSAOKernelTest, DistributionCoversQuadrants) {
  SSAOKernel kernel;
  bool q[4] = {}; // +x+y, -x+y, -x-y, +x-y
  for (const auto& s : kernel.getSamples()) {
    if (s.x > 0 && s.y > 0) q[0] = true;
    if (s.x < 0 && s.y > 0) q[1] = true;
    if (s.x < 0 && s.y < 0) q[2] = true;
    if (s.x > 0 && s.y < 0) q[3] = true;
  }
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(q[i]) << "Quadrant " << i << " not covered";
  }
}
