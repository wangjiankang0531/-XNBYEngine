// Pure unit tests for Gaussian plume + room smoke density generators.
// Coordinate math only — no Vulkan / GPU dependency.

#include <gtest/gtest.h>
#include <hongtu/coordinate/dispersion/gaussian_plume.hpp>

#include <cmath>

using hongtu::coordinate::dispersion::DensityGrid;
using hongtu::coordinate::dispersion::GaussianPlumeConfig;
using hongtu::coordinate::dispersion::PasquillStability;
using hongtu::coordinate::dispersion::buildGaussianPlumeGrid;
using hongtu::coordinate::dispersion::buildRoomSmokeGrid;
using hongtu::coordinate::dispersion::pasquillSigmas;
using hongtu::coordinate::dispersion::sampleGaussianPlume;

namespace { constexpr double kEps = 1e-9; }

// =============================================================================
// Pasquill σ
// =============================================================================

TEST(PasquillSigmasTest, ZeroAtOrigin) {
  auto s = pasquillSigmas(PasquillStability::D, 0.0);
  EXPECT_NEAR(s.x, 0.0, kEps);
  EXPECT_NEAR(s.y, 0.0, kEps);
}

TEST(PasquillSigmasTest, MonotonicWithDistance) {
  // σy / σz are both monotonically increasing in downwind distance.
  double prev_y = 0.0, prev_z = 0.0;
  for (double x = 10.0; x <= 5000.0; x *= 2.0) {
    auto s = pasquillSigmas(PasquillStability::D, x);
    EXPECT_GE(s.x, prev_y);
    EXPECT_GE(s.y, prev_z);
    prev_y = s.x;
    prev_z = s.y;
  }
}

TEST(PasquillSigmasTest, UnstableSpreadsMoreThanStable) {
  // 同距离下,A 类(very unstable)σ 比 F 类(very stable)大很多。
  auto a = pasquillSigmas(PasquillStability::A, 1000.0);
  auto f = pasquillSigmas(PasquillStability::F, 1000.0);
  EXPECT_GT(a.x, f.x);
  EXPECT_GT(a.y, f.y);
}

// =============================================================================
// Gaussian plume sampling
// =============================================================================

TEST(GaussianPlumeTest, UpwindReturnsZero) {
  GaussianPlumeConfig cfg;
  cfg.source = {0, 0, 0};
  cfg.wind   = {1, 0, 0};   // 风向 +X
  // 上风点(x<0)
  double v = sampleGaussianPlume({-100.0, 0, 0}, cfg);
  EXPECT_NEAR(v, 0.0, kEps);
}

TEST(GaussianPlumeTest, OnAxisDownwindPositive) {
  GaussianPlumeConfig cfg;
  cfg.source = {0, 0, 0};
  cfg.wind   = {1, 0, 0};
  cfg.emission_rate = 100.0;
  double v = sampleGaussianPlume({200.0, 0, 0}, cfg);
  EXPECT_GT(v, 0.0);
}

TEST(GaussianPlumeTest, PeakOnAxisLaterallySymmetric) {
  GaussianPlumeConfig cfg;
  cfg.source = {0, 0, 0};
  cfg.wind   = {1, 0, 0};
  cfg.emission_rate = 100.0;
  double on_axis = sampleGaussianPlume({500.0,  0.0, 0}, cfg);
  double off_y_p = sampleGaussianPlume({500.0,  +50.0, 0}, cfg);
  double off_y_n = sampleGaussianPlume({500.0,  -50.0, 0}, cfg);
  EXPECT_GT(on_axis, off_y_p);
  EXPECT_NEAR(off_y_p, off_y_n, on_axis * 1e-3);  // 对称
}

TEST(GaussianPlumeTest, ConcentrationDecaysWithDistance) {
  GaussianPlumeConfig cfg;
  cfg.source = {0, 0, 0};
  cfg.wind   = {1, 0, 0};
  cfg.emission_rate = 1.0;
  // σ 随距离增大而稀释 → 同 emission_rate 下,远处中心浓度更低
  double near = sampleGaussianPlume({200.0, 0, 0}, cfg);
  double far  = sampleGaussianPlume({2000.0, 0, 0}, cfg);
  EXPECT_GT(near, far);
}

TEST(GaussianPlumeTest, ZeroWindReturnsZero) {
  GaussianPlumeConfig cfg;
  cfg.wind = {0, 0, 0};
  EXPECT_NEAR(sampleGaussianPlume({100, 0, 0}, cfg), 0.0, kEps);
}

// =============================================================================
// DensityGrid 构建
// =============================================================================

TEST(GaussianPlumeGridTest, NormalizesPeakToOne) {
  GaussianPlumeConfig cfg;
  cfg.source = {0, 0, 1.0};
  cfg.wind   = {2.0, 0, 0};
  cfg.stability = PasquillStability::D;
  cfg.emission_rate = 10.0;

  auto grid = buildGaussianPlumeGrid(cfg,
      /*aabb_min=*/{-5, -10, 0},
      /*aabb_max=*/{60,  10, 5},
      /*w=*/32, /*h=*/16, /*d=*/8);

  EXPECT_EQ(grid.width,  32u);
  EXPECT_EQ(grid.height, 16u);
  EXPECT_EQ(grid.depth,  8u);
  EXPECT_EQ(grid.values.size(), 32u * 16u * 8u);

  // peak == 1
  float peak = 0.0f;
  for (auto v : grid.values) peak = std::max(peak, v);
  EXPECT_NEAR(peak, 1.0f, 1e-4f);
}

TEST(GaussianPlumeGridTest, AllNonNegative) {
  GaussianPlumeConfig cfg;
  cfg.source = {0, 0, 1.0};
  cfg.wind   = {2.0, 0, 0};
  auto grid = buildGaussianPlumeGrid(cfg, {-5, -5, 0}, {50, 5, 3}, 16, 8, 4);
  for (auto v : grid.values) EXPECT_GE(v, 0.0f);
}

TEST(GaussianPlumeGridTest, CellCenterMatchesPosition) {
  GaussianPlumeConfig cfg;
  auto grid = buildGaussianPlumeGrid(cfg, {0, 0, 0}, {10, 20, 30}, 5, 4, 3);
  glm::dvec3 c = grid.cellCenter(0, 0, 0);
  EXPECT_NEAR(c.x, 1.0,   1e-6);  // 10 / 5 / 2 = 1
  EXPECT_NEAR(c.y, 2.5,   1e-6);
  EXPECT_NEAR(c.z, 5.0,   1e-6);
}

// =============================================================================
// 室内 smoke 网格
// =============================================================================

TEST(RoomSmokeGridTest, PeakAtSourceCellAfterIterations) {
  auto g = buildRoomSmokeGrid(/*source=*/{2.5, 2.5, 2.5},
                              /*aabb_min=*/{0, 0, 0},
                              /*aabb_max=*/{5, 5, 5},
                              /*w=*/8, /*h=*/8, /*d=*/8,
                              /*iterations=*/20);
  // peak normalized to 1
  float peak = 0.0f;
  for (auto v : g.values) peak = std::max(peak, v);
  EXPECT_NEAR(peak, 1.0f, 1e-4f);

  // The source cell or its immediate neighbors should carry the peak.
  // Center cell index ≈ (4, 4, 4).
  float center_v = g.values[g.indexOf(4, 4, 4)];
  EXPECT_GT(center_v, 0.5f);
}

TEST(RoomSmokeGridTest, ZeroIterationsLeavesOnlySource) {
  auto g = buildRoomSmokeGrid({2.5, 2.5, 2.5}, {0, 0, 0}, {5, 5, 5},
                              8, 8, 8, /*iterations=*/0);
  // 0 iter → only source cell has value 1.0,after normalize.
  int nz = 0;
  for (auto v : g.values) if (v > 0.01f) ++nz;
  EXPECT_EQ(nz, 1);
}
