#include <gtest/gtest.h>
#include <hongtu/renderer/globe/tile_gpu_cache.hpp>

using namespace hongtu;
using namespace hongtu::renderer;
using gis::TileCoord;

TEST(TileGpuCache, TouchInsertsEmptyEntry) {
  TileGpuCache cache;
  auto* t = cache.touch(TileCoord{4, 1, 2}, /*frame=*/10);
  ASSERT_NE(t, nullptr);
  EXPECT_FALSE(t->owns_real_texture);  // empty until uploadTile fills it
  EXPECT_EQ(t->frame, 10u);
}

TEST(TileGpuCache, FindMissReturnsNullptr) {
  TileGpuCache cache;
  EXPECT_EQ(cache.find(TileCoord{4, 1, 2}), nullptr);
  cache.touch(TileCoord{4, 1, 2}, 10);
  EXPECT_NE(cache.find(TileCoord{4, 1, 2}), nullptr);
}

TEST(TileGpuCache, NewTileHasInvalidEdgeScales) {
  TileGpuCache cache;
  auto* t = cache.touch(TileCoord{4, 1, 2}, 10);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->last_edge_scales, -1)
      << "New GpuTile must signal 'never initialized' so refreshTileIBO runs on first frame";
}

TEST(TileGpuCache, EvictionRespectsCurrentFrame) {
  TileGpuCache cache;
  cache.setRatio(1.0f);  // cap = peak
  for (i32 i = 0; i < 10; ++i) cache.touch(TileCoord{2, i, 0}, 100);
  cache.endFrameWithEviction(100);
  EXPECT_EQ(cache.size(), 10u);

  for (i32 i = 0; i < 3; ++i) cache.touch(TileCoord{2, i, 0}, 101);
  for (i32 i = 0; i < 5; ++i) cache.touch(TileCoord{3, i, 0}, 101);
  cache.endFrameWithEviction(101);
  EXPECT_EQ(cache.size(), 10u);  // cap = max(8, 10*1) = 10
  // All 8 current-frame tiles must survive
  EXPECT_NE(cache.find(TileCoord{3, 0, 0}), nullptr);
  EXPECT_NE(cache.find(TileCoord{2, 0, 0}), nullptr);
}
