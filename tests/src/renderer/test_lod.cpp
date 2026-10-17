#include <gtest/gtest.h>
#include <hongtu/renderer/lod/lod_system.hpp>
#include <cmath>

using namespace hongtu::renderer;

static constexpr float PI = 3.14159265f;

// ============================================================================
// LOD Selection
// ============================================================================

TEST(LODSelectorTest, DefaultThresholds) {
  LODSelector selector;
  EXPECT_EQ(selector.getLevels().size(), 5u);
  // First level should have highest screen area threshold
  EXPECT_GT(selector.getLevels()[0].screen_area_min, selector.getLevels()[1].screen_area_min);
}

TEST(LODSelectorTest, CloseObjectSelectsLOD0) {
  LODSelector selector;
  auto result = selector.selectLOD(1.0f, 1.0f, PI / 4.0f, 1080.0f);
  EXPECT_EQ(result.level, 0u);
  EXPECT_GT(result.screen_area, 0.0f);
}

TEST(LODSelectorTest, FarObjectSelectsHigherLOD) {
  LODSelector selector;
  auto close = selector.selectLOD(1.0f, 10.0f, PI / 4.0f, 1080.0f);
  auto far = selector.selectLOD(1.0f, 1000.0f, PI / 4.0f, 1080.0f);

  EXPECT_LE(close.level, far.level);
  EXPECT_GT(close.screen_area, far.screen_area);
}

TEST(LODSelectorTest, Deterministic) {
  LODSelector selector;
  auto r1 = selector.selectLOD(5.0f, 100.0f, PI / 3.0f, 1080.0f);
  auto r2 = selector.selectLOD(5.0f, 100.0f, PI / 3.0f, 1080.0f);
  EXPECT_EQ(r1.level, r2.level);
  EXPECT_FLOAT_EQ(r1.screen_area, r2.screen_area);
}

TEST(LODSelectorTest, LargerObjectSelectsLowerLOD) {
  LODSelector selector;
  auto small = selector.selectLOD(0.1f, 100.0f, PI / 4.0f, 1080.0f);
  auto large = selector.selectLOD(10.0f, 100.0f, PI / 4.0f, 1080.0f);
  EXPECT_GE(small.level, large.level);
}

TEST(LODSelectorTest, ScreenAreaComputation) {
  // Object at distance=radius should have a known projected size
  float area = LODSelector::computeScreenArea(1.0f, 10.0f, PI / 4.0f, 1080.0f);
  EXPECT_GT(area, 0.0f);
  EXPECT_LT(area, 1.0f);

  // Zero distance should return max area
  float area_close = LODSelector::computeScreenArea(1.0f, 0.001f, PI / 4.0f, 1080.0f);
  EXPECT_GT(area_close, area);
}

TEST(LODSelectorTest, BiasAffectsSelection) {
  LODConfig config;
  config.bias = 2.0f; // Force lower quality
  LODSelector biased(config);

  LODConfig config_no_bias;
  config_no_bias.bias = 0.0f;
  LODSelector unbiased(config_no_bias);

  auto r_biased = biased.selectLOD(5.0f, 50.0f, PI / 4.0f, 1080.0f);
  auto r_unbiased = unbiased.selectLOD(5.0f, 50.0f, PI / 4.0f, 1080.0f);

  EXPECT_GE(r_biased.level, r_unbiased.level);
}

TEST(LODSelectorTest, CustomLevels) {
  LODSelector selector;
  std::vector<LODLevel> levels = {
      {0, 0.5f, 1.0f},
      {1, 0.1f, 0.5f},
      {2, 0.0f, 0.1f},
  };
  selector.setLevels(levels);
  EXPECT_EQ(selector.getLevels().size(), 3u);
}

// ============================================================================
// QEM Mesh Simplification
// ============================================================================

static void makeBox(std::vector<glm::vec3>& vertices, std::vector<uint32_t>& indices) {
  // Simple cube: 8 vertices, 12 triangles
  vertices = {
      {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
      {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1},
  };
  indices = {
      0,1,2, 0,2,3,  // front
      4,6,5, 4,7,6,  // back
      0,4,5, 0,5,1,  // bottom
      2,6,7, 2,7,3,  // top
      0,3,7, 0,7,4,  // left
      1,5,6, 1,6,2,  // right
  };
}

TEST(QEMSimplifyTest, FullRatioReturnsOriginal) {
  std::vector<glm::vec3> verts;
  std::vector<uint32_t> inds;
  makeBox(verts, inds);

  auto result = qem::simplify(verts, inds, 1.0f);
  EXPECT_EQ(result.triangle_count, 12u);
}

TEST(QEMSimplifyTest, HalfRatioReducesTriangles) {
  std::vector<glm::vec3> verts;
  std::vector<uint32_t> inds;
  makeBox(verts, inds);

  auto result = qem::simplify(verts, inds, 0.5f);
  EXPECT_LT(result.triangle_count, 12u);
  EXPECT_GT(result.triangle_count, 0u);
  EXPECT_EQ(result.indices.size(), result.triangle_count * 3u);
}

TEST(QEMSimplifyTest, ExtremeSimplification) {
  std::vector<glm::vec3> verts;
  std::vector<uint32_t> inds;
  makeBox(verts, inds);

  // 12 tris * 0.01 = target 0 -> may collapse to 0
  auto result = qem::simplify(verts, inds, 0.01f);
  EXPECT_LT(result.triangle_count, 12u);
  // With a cube (only 12 tris), extreme simplification may collapse everything
  // This is expected behavior for such a small input
}

TEST(QEMSimplifyTest, LODChainDecreasingTriangles) {
  std::vector<glm::vec3> verts;
  std::vector<uint32_t> inds;
  makeBox(verts, inds);

  auto chain = qem::generateLODChain(verts, inds, {1.0f, 0.75f, 0.5f, 0.25f});
  EXPECT_EQ(chain.size(), 4u);

  // Each level should have <= triangles of previous
  for (size_t i = 1; i < chain.size(); ++i) {
    EXPECT_LE(chain[i].triangle_count, chain[i - 1].triangle_count);
  }
}

TEST(QEMSimplifyTest, EmptyMesh) {
  auto result = qem::simplify({}, {}, 0.5f);
  EXPECT_EQ(result.triangle_count, 0u);
}
