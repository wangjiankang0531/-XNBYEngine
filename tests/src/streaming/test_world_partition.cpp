#include <gtest/gtest.h>
#include <hongtu/renderer/streaming/world_partition.hpp>

using namespace hongtu::renderer;

TEST(WorldPartitionTest, WorldToCellConversion) {
  WorldPartitionConfig cfg;
  cfg.cell_size = 100.0f;
  WorldPartition wp(cfg);

  auto c = wp.worldToCell(glm::vec3{150, 250, 0});
  EXPECT_EQ(c.x, 1);
  EXPECT_EQ(c.y, 2);

  auto center = wp.cellToWorld(c);
  EXPECT_NEAR(center.x, 150.0f, 50.0f);
  EXPECT_NEAR(center.y, 250.0f, 50.0f);
}

TEST(WorldPartitionTest, RegisterAndQuery) {
  WorldPartition wp;
  CellCoord c{0, 0, 0};
  wp.registerCell(c, "cell_0_0_0.dat", 1024);
  EXPECT_EQ(wp.getTotalCells(), 1u);
  EXPECT_EQ(wp.getCellState(c), CellState::Unloaded);
}

TEST(WorldPartitionTest, LoadUnloadCycle) {
  WorldPartitionConfig cfg;
  cfg.cell_size = 10.0f;
  cfg.load_radius = 25.0f;
  cfg.unload_radius = 35.0f;
  WorldPartition wp(cfg);

  // Register cells in a grid
  for (int x = -3; x <= 3; ++x)
    for (int y = -3; y <= 3; ++y)
      wp.registerCell({x, y, 0}, "cell.dat", 1000);

  // Camera at origin: nearby cells should be requested for loading
  auto result = wp.update({0, 0, 0});
  EXPECT_GT(result.cells_to_load.size(), 0u);

  // Simulate loading completion
  for (auto& coord : result.cells_to_load) {
    wp.onCellLoaded(coord, 1000);
  }

  // Update again: loaded cells should stay, no new loads
  auto result2 = wp.update({0, 0, 0});
  EXPECT_GT(result2.loaded_count, 0u);

  // Move camera far away: cells should unload
  auto result3 = wp.update({500, 500, 0});
  EXPECT_GT(result3.cells_to_unload.size(), 0u);
}

TEST(WorldPartitionTest, MemoryBudget) {
  WorldPartitionConfig cfg;
  cfg.cell_size = 10.0f;
  cfg.load_radius = 100.0f;
  cfg.memory_budget_bytes = 5000; // Very small budget
  WorldPartition wp(cfg);

  for (int i = 0; i < 100; ++i)
    wp.registerCell({i, 0, 0}, "cell.dat", 1000);

  auto result = wp.update({0, 0, 0});
  // Should not request more loads than budget allows
  uint64_t total_requested = 0;
  for (auto& c : result.cells_to_load) total_requested += 1000;
  EXPECT_LE(total_requested + wp.getUsedMemory(), cfg.memory_budget_bytes + 5000);
}

TEST(WorldPartitionTest, GetLoadedCells) {
  WorldPartition wp;
  wp.registerCell({0, 0, 0}, "a.dat");
  wp.registerCell({1, 0, 0}, "b.dat");

  wp.update({0, 0, 0});
  wp.onCellLoaded({0, 0, 0}, 100);

  auto loaded = wp.getLoadedCells();
  EXPECT_EQ(loaded.size(), 1u);
}
