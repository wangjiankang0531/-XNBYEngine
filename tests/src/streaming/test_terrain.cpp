#include <gtest/gtest.h>
#include <hongtu/renderer/terrain/terrain_system.hpp>
#include <cmath>

using namespace hongtu::renderer;

TEST(DEMDataTest, GenerateTestDEM) {
  auto dem = generateTestDEM(64, 64, 0.0, 0.0, 1.0, 500.0f);
  EXPECT_EQ(dem.width, 64u);
  EXPECT_EQ(dem.height, 64u);
  EXPECT_EQ(dem.heights.size(), 64u * 64u);
  // Multi-frequency DEM may exceed amplitude due to additive waves
  EXPECT_GT(dem.max_height, 0.0f);
}

TEST(DEMDataTest, SampleHeight) {
  auto dem = generateTestDEM(10, 10, 0.0, 0.0, 1.0, 100.0f);

  // Sample at grid point
  float h = dem.sampleHeight(0.0, 0.0);
  EXPECT_FLOAT_EQ(h, dem.heights[0]);

  // Sample at center should interpolate
  float hc = dem.sampleHeight(0.5, 0.5);
  EXPECT_GE(hc, dem.min_height);
  EXPECT_LE(hc, dem.max_height);
}

TEST(DEMDataTest, SampleOutOfBounds) {
  auto dem = generateTestDEM(10, 10, 0.0, 0.0, 1.0);
  // Out of bounds should clamp
  float h = dem.sampleHeight(-100.0, -100.0);
  EXPECT_GE(h, dem.min_height);
  EXPECT_LE(h, dem.max_height);
}

TEST(DEMDataTest, Bounds) {
  auto dem = generateTestDEM(10, 10, 100.0, 200.0, 5.0);
  double x0, y0, x1, y1;
  dem.getBounds(x0, y0, x1, y1);
  EXPECT_DOUBLE_EQ(x0, 100.0);
  EXPECT_DOUBLE_EQ(y0, 200.0);
  EXPECT_GT(x1, x0);
  EXPECT_GT(y1, y0);
}

TEST(DEMAsciiTest, ParseSimple) {
  std::string ascii =
      "ncols 3\n"
      "nrows 3\n"
      "xllcorner 0\n"
      "yllcorner 0\n"
      "cellsize 1\n"
      "NODATA_value -9999\n"
      "1 2 3\n"
      "4 5 6\n"
      "7 8 9\n";

  DEMData dem;
  ASSERT_TRUE(loadDEMAscii(ascii, dem));
  EXPECT_EQ(dem.width, 3u);
  EXPECT_EQ(dem.height, 3u);
  EXPECT_FLOAT_EQ(dem.min_height, 1.0f);
  EXPECT_FLOAT_EQ(dem.max_height, 9.0f);
}

TEST(TerrainLODTest, CellSizeAtLOD) {
  TerrainConfig cfg;
  cfg.base_cell_size = 1.0f;
  TerrainLODSystem sys(cfg);

  EXPECT_FLOAT_EQ(sys.cellSizeAtLOD(0), 1.0f);
  EXPECT_FLOAT_EQ(sys.cellSizeAtLOD(1), 2.0f);
  EXPECT_FLOAT_EQ(sys.cellSizeAtLOD(2), 4.0f);
}

TEST(TerrainLODTest, GeometricError) {
  TerrainLODSystem sys;
  float e0 = sys.computeGeometricError(0);
  float e1 = sys.computeGeometricError(1);
  float e2 = sys.computeGeometricError(2);

  // Higher LOD = lower detail = higher error
  EXPECT_LT(e0, e1);
  EXPECT_LT(e1, e2);
}

TEST(TerrainLODTest, SelectTilesProducesResults) {
  TerrainConfig cfg;
  cfg.max_lod_levels = 5;
  cfg.base_cell_size = 10.0f;
  TerrainLODSystem sys(cfg);

  auto tiles = sys.selectTiles({500, 500, 100}, glm::radians(60.0f), 1080.0f,
                               0, 0, 1000, 1000);
  EXPECT_GT(tiles.size(), 0u);

  // All tiles should have valid LOD levels
  for (const auto& t : tiles) {
    EXPECT_LT(t.lod_level, cfg.max_lod_levels);
  }
}

TEST(TerrainLODTest, CloserTilesHaveHigherDetail) {
  TerrainConfig cfg;
  cfg.max_lod_levels = 8;
  cfg.base_cell_size = 1.0f;
  TerrainLODSystem sys(cfg);

  // Camera at center, close to ground
  auto tiles = sys.selectTiles({50, 50, 10}, glm::radians(60.0f), 1080.0f,
                               0, 0, 100, 100);

  // Find min LOD (highest detail) tile
  uint32_t min_lod = 999;
  for (const auto& t : tiles) {
    if (t.lod_level < min_lod) min_lod = t.lod_level;
  }
  // Near camera should have low LOD level
  EXPECT_LT(min_lod, 4u);
}

TEST(TerrainLODTest, ComputeLODLevel) {
  TerrainLODSystem sys;
  uint32_t lod_near = sys.computeLODLevel(10.0f, glm::radians(60.0f), 1080.0f);
  uint32_t lod_far = sys.computeLODLevel(10000.0f, glm::radians(60.0f), 1080.0f);
  EXPECT_LE(lod_near, lod_far);
}
