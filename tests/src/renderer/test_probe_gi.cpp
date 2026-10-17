#include <gtest/gtest.h>
#include <hongtu/renderer/gi/probe_gi.hpp>
#include <cmath>

using namespace hongtu::renderer;

// ============================================================================
// SH Basis
// ============================================================================

TEST(SHBasisTest, Orthonormality) {
  // SH basis functions: Y10 ~ z, Y1p1 ~ x, Y1n1 ~ y
  auto basis_z = shBasis({0, 0, 1}); // +Z
  auto basis_x = shBasis({1, 0, 0}); // +X
  auto basis_y = shBasis({0, 1, 0}); // +Y

  // L0 should be constant for all directions
  EXPECT_FLOAT_EQ(basis_z[0], basis_x[0]);
  EXPECT_FLOAT_EQ(basis_z[0], basis_y[0]);

  // L1 components: Y10 ~ z, Y1p1 ~ x, Y1n1 ~ y
  EXPECT_GT(std::abs(basis_z[2]), 0.1f);   // Y10 responds to z
  EXPECT_GT(std::abs(basis_x[3]), 0.1f);   // Y1p1 responds to x
  EXPECT_GT(std::abs(basis_y[1]), 0.1f);   // Y1n1 responds to y
}

TEST(SHBasisTest, Normalized) {
  // For any direction, L0 coefficient should be 1/(2*sqrt(pi)) ≈ 0.282
  auto basis = shBasis(glm::normalize(glm::vec3(1, 2, 3)));
  EXPECT_NEAR(basis[0], 0.282095f, 0.001f);
}

// ============================================================================
// SH9 Operations
// ============================================================================

TEST(SH9Test, ClearIsZero) {
  SH9 sh;
  sh.clear();
  auto result = sh.evaluate({0, 1, 0});
  EXPECT_NEAR(result.r, 0.0f, 1e-6f);
  EXPECT_NEAR(result.g, 0.0f, 1e-6f);
  EXPECT_NEAR(result.b, 0.0f, 1e-6f);
}

TEST(SH9Test, UniformLight) {
  // Add uniform white light from all directions
  SH9 sh;
  sh.clear();
  const int N = 1000;
  for (int i = 0; i < N; ++i) {
    float phi = 2.0f * 3.14159f * static_cast<float>(i) / N;
    for (int j = 0; j < N / 10; ++j) {
      float theta = 3.14159f * static_cast<float>(j) / (N / 10);
      glm::vec3 dir(std::sin(theta) * std::cos(phi),
                    std::cos(theta),
                    std::sin(theta) * std::sin(phi));
      sh.addSample(dir, glm::vec3(1.0f), 4.0f * 3.14159f / (N * N / 10));
    }
  }

  // Uniform light should produce roughly equal response in all directions
  auto up = sh.evaluate({0, 1, 0});
  auto down = sh.evaluate({0, -1, 0});
  auto side = sh.evaluate({1, 0, 0});

  // All should be positive and roughly similar
  EXPECT_GT(up.r, 0.0f);
  EXPECT_GT(down.r, 0.0f);
  EXPECT_GT(side.r, 0.0f);
}

TEST(SH9Test, DirectionalLight) {
  SH9 sh;
  sh.clear();
  // Strong light from above
  glm::vec3 light_dir(0, 1, 0);
  sh.addSample(light_dir, glm::vec3(10.0f), 1.0f);

  auto up = sh.evaluate({0, 1, 0});
  auto down = sh.evaluate({0, -1, 0});

  // Looking up should be brighter than looking down
  EXPECT_GT(up.r, down.r);
}

TEST(SH9Test, AddAndScale) {
  SH9 a, b;
  a.clear();
  b.clear();
  a.addSample({1, 0, 0}, glm::vec3(1.0f));
  b.addSample({0, 1, 0}, glm::vec3(1.0f));

  auto sum = a + b;
  auto scaled = a * 2.0f;

  // sum should respond to both directions
  auto eval_x = sum.evaluate({1, 0, 0});
  auto eval_y = sum.evaluate({0, 1, 0});
  EXPECT_GT(eval_x.r, 0.0f);
  EXPECT_GT(eval_y.r, 0.0f);

  // scaled should be 2x original
  auto eval_a = a.evaluate({1, 0, 0});
  auto eval_2a = scaled.evaluate({1, 0, 0});
  EXPECT_NEAR(eval_2a.r, eval_a.r * 2.0f, 1e-4f);
}

// ============================================================================
// ProbeGrid
// ============================================================================

TEST(ProbeGridTest, DefaultGrid) {
  ProbeGrid grid;
  EXPECT_EQ(grid.getProbeCount(), 8u * 4u * 8u); // 256 probes
  EXPECT_EQ(grid.getProbes().size(), 256u);
}

TEST(ProbeGridTest, CustomGrid) {
  ProbeGridConfig config;
  config.grid_size = {4, 2, 3};
  ProbeGrid grid(config);
  EXPECT_EQ(grid.getProbeCount(), 24u);
}

TEST(ProbeGridTest, ProbePositions) {
  ProbeGridConfig config;
  config.grid_size = {3, 2, 2};
  config.grid_origin = {0, 0, 0};
  config.grid_spacing = {10, 5, 10};
  ProbeGrid grid(config);

  auto pos_000 = grid.getProbe(0, 0, 0).position;
  EXPECT_NEAR(pos_000.x, 0.0f, 1e-5f);
  EXPECT_NEAR(pos_000.y, 0.0f, 1e-5f);

  auto pos_210 = grid.getProbe(2, 1, 0).position;
  EXPECT_NEAR(pos_210.x, 20.0f, 1e-5f);
  EXPECT_NEAR(pos_210.y, 5.0f, 1e-5f);
}

TEST(ProbeGridTest, LinearGridConversion) {
  ProbeGridConfig config;
  config.grid_size = {4, 3, 2};
  ProbeGrid grid(config);

  for (uint32_t i = 0; i < grid.getProbeCount(); ++i) {
    auto g = grid.linearToGrid(i);
    uint32_t back = grid.gridToLinear(g.x, g.y, g.z);
    EXPECT_EQ(back, i);
  }
}

TEST(ProbeGridTest, FindSurroundingProbes) {
  ProbeGridConfig config;
  config.grid_size = {4, 4, 4};
  config.grid_origin = {0, 0, 0};
  config.grid_spacing = {1, 1, 1};
  ProbeGrid grid(config);

  std::array<uint32_t, 8> indices{};
  glm::vec3 weights;

  // Point at (0.5, 0.5, 0.5) -> center of first cell
  EXPECT_TRUE(grid.findSurroundingProbes({0.5f, 0.5f, 0.5f}, indices, weights));
  EXPECT_NEAR(weights.x, 0.5f, 1e-5f);
  EXPECT_NEAR(weights.y, 0.5f, 1e-5f);
  EXPECT_NEAR(weights.z, 0.5f, 1e-5f);

  // Point outside grid
  EXPECT_FALSE(grid.findSurroundingProbes({-1.0f, 0.0f, 0.0f}, indices, weights));
  EXPECT_FALSE(grid.findSurroundingProbes({10.0f, 0.0f, 0.0f}, indices, weights));
}

TEST(ProbeGridTest, SampleIrradianceAllValid) {
  ProbeGridConfig config;
  config.grid_size = {3, 3, 3};
  config.grid_origin = {0, 0, 0};
  config.grid_spacing = {2, 2, 2};
  ProbeGrid grid(config);

  // Set all probes to uniform white
  for (uint32_t i = 0; i < grid.getProbeCount(); ++i) {
    auto& probe = grid.getProbe(i);
    probe.valid = true;
    probe.irradiance.clear();
    probe.irradiance.addSample({0, 1, 0}, glm::vec3(1.0f));
  }

  // Sample at center of grid
  auto irr = grid.sampleIrradiance({2.0f, 2.0f, 2.0f}, {0, 1, 0});
  EXPECT_GT(irr.r, 0.0f);
}

TEST(ProbeGridTest, UpdateScheduling) {
  ProbeGridConfig config;
  config.grid_size = {4, 4, 4};
  ProbeGrid grid(config);

  auto list = grid.getProbesForUpdate(8, 0);
  EXPECT_EQ(list.size(), 8u);

  auto list2 = grid.getProbesForUpdate(8, 1);
  EXPECT_EQ(list2.size(), 8u);
  // Should start at different offset
  EXPECT_NE(list[0], list2[0]);
}

TEST(ProbeGridTest, Bounds) {
  ProbeGridConfig config;
  config.grid_size = {4, 2, 3};
  config.grid_origin = {10, 20, 30};
  config.grid_spacing = {5, 10, 5};
  ProbeGrid grid(config);

  glm::vec3 min_b, max_b;
  grid.getBounds(min_b, max_b);
  EXPECT_NEAR(min_b.x, 10.0f, 1e-5f);
  EXPECT_NEAR(max_b.x, 25.0f, 1e-5f); // 10 + 3*5
  EXPECT_NEAR(max_b.y, 30.0f, 1e-5f); // 20 + 1*10
}

TEST(ProbeGridTest, InvalidateAll) {
  ProbeGrid grid;
  for (uint32_t i = 0; i < grid.getProbeCount(); ++i) {
    grid.getProbe(i).valid = true;
  }
  grid.invalidateAll();
  for (uint32_t i = 0; i < grid.getProbeCount(); ++i) {
    EXPECT_FALSE(grid.getProbe(i).valid);
    EXPECT_TRUE(grid.getProbe(i).needs_update);
  }
}
