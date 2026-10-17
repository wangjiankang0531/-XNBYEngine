#include <gtest/gtest.h>
#include <hongtu/gis/tile/tile_geometry_cache.hpp>

using namespace hongtu;
using namespace hongtu::gis;

TEST(TileGeometryCache, TouchInsertsAndStamps) {
  TileGeometryCache cache;
  TileCoord c{5, 1, 2};
  auto* t = cache.touch(c, /*frame=*/100);
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->coord.z, 5);
  EXPECT_EQ(t->frame, 100u);
}

TEST(TileGeometryCache, TouchHitUpdatesFrame) {
  TileGeometryCache cache;
  TileCoord c{5, 1, 2};
  cache.touch(c, 100);
  auto* t = cache.touch(c, 200);
  EXPECT_EQ(t->frame, 200u);
  EXPECT_EQ(cache.size(), 1u);
}

TEST(TileGeometryCache, EvictionKeepsCurrentFrameTiles) {
  TileGeometryCache cache;
  // Frame 100: touch 5 tiles
  for (i32 i = 0; i < 5; ++i) cache.touch(TileCoord{1, i, 0}, 100);
  cache.endFrameWithEviction(100);
  EXPECT_EQ(cache.size(), 5u);  // peak=5, cap=5*2=10, nothing evicted

  // Frame 101: touch only 2 of them; peak stays 5 → cap=10
  cache.touch(TileCoord{1, 0, 0}, 101);
  cache.touch(TileCoord{1, 1, 0}, 101);
  cache.endFrameWithEviction(101);
  EXPECT_EQ(cache.size(), 5u);  // under cap, no eviction
}

TEST(TileGeometryCache, EvictsLeastRecentWhenOverCap) {
  TileGeometryCache cache;
  cache.setRatio(1.0f);  // cap = peak (no over-allocation buffer)

  // Frame 100: touch 10 tiles. peak=10, used=10, cap=10. No eviction.
  for (i32 i = 0; i < 10; ++i) cache.touch(TileCoord{2, i, 0}, 100);
  cache.endFrameWithEviction(100);
  EXPECT_EQ(cache.size(), 10u);

  // Frame 101: touch 3 OLD + 5 NEW. peak stays 10, used=8, cap = max(8, 10*1) = 10.
  // We now have 10 old + 5 new = 15 entries before eviction. Cap = 10.
  // Eviction must keep the 10 with the newest frame stamps.
  for (i32 i = 0; i < 3; ++i) cache.touch(TileCoord{2, i, 0}, 101);  // 3 touched (frame 101)
  for (i32 i = 0; i < 5; ++i) cache.touch(TileCoord{3, i, 0}, 101);  // 5 new (frame 101)
  cache.endFrameWithEviction(101);

  // 15 entries → cap 10. The 7 untouched-this-frame tiles (frame=100) compete for
  // 10 - 8 = 2 remaining slots. The 5 of them with the highest frame (all 100) are
  // tied; nth_element picks any 2. Size must be exactly 10.
  EXPECT_EQ(cache.size(), 10u);

  // All 3 touched-this-frame tiles must survive.
  EXPECT_NE(cache.find(TileCoord{2, 0, 0}), nullptr);
  EXPECT_NE(cache.find(TileCoord{2, 1, 0}), nullptr);
  EXPECT_NE(cache.find(TileCoord{2, 2, 0}), nullptr);

  // All 5 fresh-this-frame tiles must survive.
  for (i32 i = 0; i < 5; ++i) {
    EXPECT_NE(cache.find(TileCoord{3, i, 0}), nullptr);
  }
}
