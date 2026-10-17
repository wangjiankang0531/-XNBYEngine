#include <gtest/gtest.h>
#include <hongtu/renderer/globe/ancestor_fallback.hpp>
#include <unordered_map>

using namespace hongtu;
using namespace hongtu::renderer;
using gis::TileCoord;

namespace {

struct StubTile {
  bool owns_real_texture = false;
};

using StubCache = std::unordered_map<TileCoord, StubTile, TileCoord::Hash>;

const StubTile* finder(const StubCache& c, const TileCoord& coord) {
  auto it = c.find(coord);
  return it == c.end() ? nullptr : &it->second;
}

}  // namespace

TEST(AncestorFallback, SelfHitWhenLeafHasTexture) {
  StubCache cache;
  cache[{5, 10, 12}] = {/*owns=*/true};

  auto result = resolveAncestorFallback<StubTile>(
      TileCoord{5, 10, 12}, [&](const TileCoord& c) { return finder(cache, c); });

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->source_coord.z, 5);
  EXPECT_EQ(result->source_coord.x, 10);
  EXPECT_EQ(result->source_coord.y, 12);
  EXPECT_FLOAT_EQ(result->offset.x, 0.0f);
  EXPECT_FLOAT_EQ(result->offset.y, 0.0f);
  EXPECT_FLOAT_EQ(result->scale.x, 1.0f);
  EXPECT_FLOAT_EQ(result->scale.y, 1.0f);
}

TEST(AncestorFallback, WalksToParentWhenLeafMissing) {
  // Leaf (6, 21, 25) — its parent is (5, 10, 12).
  // Leaf occupies the lower-right quadrant of the parent:
  //   x=21 within parent.x*2..parent.x*2+1 → 21-20=1 (right half)
  //   y=25 within parent.y*2..parent.y*2+1 → 25-24=1 (lower half)
  StubCache cache;
  cache[{5, 10, 12}] = {/*owns=*/true};

  auto result = resolveAncestorFallback<StubTile>(
      TileCoord{6, 21, 25}, [&](const TileCoord& c) { return finder(cache, c); });

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->source_coord.z, 5);
  EXPECT_FLOAT_EQ(result->scale.x, 0.5f);
  EXPECT_FLOAT_EQ(result->scale.y, 0.5f);
  EXPECT_FLOAT_EQ(result->offset.x, 0.5f);
  EXPECT_FLOAT_EQ(result->offset.y, 0.5f);
}

TEST(AncestorFallback, WalksMultipleLevelsUp) {
  // Leaf (8, 42, 50) → ancestor (5, 5, 6) (dz=3, div=8).
  //   x: 42 - (5 << 3) = 42 - 40 = 2  → offset.x = 2/8 = 0.25
  //   y: 50 - (6 << 3) = 50 - 48 = 2  → offset.y = 2/8 = 0.25
  StubCache cache;
  cache[{5, 5, 6}] = {/*owns=*/true};

  auto result = resolveAncestorFallback<StubTile>(
      TileCoord{8, 42, 50}, [&](const TileCoord& c) { return finder(cache, c); });

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->source_coord.z, 5);
  EXPECT_FLOAT_EQ(result->scale.x, 0.125f);
  EXPECT_FLOAT_EQ(result->scale.y, 0.125f);
  EXPECT_FLOAT_EQ(result->offset.x, 0.25f);
  EXPECT_FLOAT_EQ(result->offset.y, 0.25f);
}

TEST(AncestorFallback, ReturnsNulloptWhenChainEmpty) {
  StubCache cache;  // empty
  auto result = resolveAncestorFallback<StubTile>(
      TileCoord{4, 1, 2}, [&](const TileCoord& c) { return finder(cache, c); });
  EXPECT_FALSE(result.has_value());
}

TEST(AncestorFallback, SkipsAncestorsWithoutOwnedTexture) {
  StubCache cache;
  cache[{5, 10, 12}] = {/*owns=*/false};  // present but no texture
  cache[{4, 5, 6}]   = {/*owns=*/true};

  auto result = resolveAncestorFallback<StubTile>(
      TileCoord{5, 10, 12}, [&](const TileCoord& c) { return finder(cache, c); });

  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->source_coord.z, 4);
  EXPECT_FLOAT_EQ(result->scale.x, 0.5f);
  EXPECT_FLOAT_EQ(result->scale.y, 0.5f);
  // Parent of (5,10,12) is (4,5,6): ox=10-(5<<1)=0, oy=12-(6<<1)=0 → offset=(0,0)
  EXPECT_FLOAT_EQ(result->offset.x, 0.0f);
  EXPECT_FLOAT_EQ(result->offset.y, 0.0f);
}

TEST(AncestorFallback, RootLeafSelfHit) {
  StubCache cache;
  cache[{0, 0, 0}] = {/*owns=*/true};
  auto result = resolveAncestorFallback<StubTile>(
      TileCoord{0, 0, 0}, [&](const TileCoord& c) { return finder(cache, c); });
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->source_coord.z, 0);
  EXPECT_FLOAT_EQ(result->scale.x, 1.0f);
  EXPECT_FLOAT_EQ(result->offset.x, 0.0f);
}

TEST(AncestorFallback, RootLeafEmptyCacheReturnsNullopt) {
  StubCache cache;  // empty
  auto result = resolveAncestorFallback<StubTile>(
      TileCoord{0, 0, 0}, [&](const TileCoord& c) { return finder(cache, c); });
  EXPECT_FALSE(result.has_value());
}

TEST(AncestorFallback, RespectsMaxDzCap) {
  // Only z=3 ancestor is loaded; leaf is at z=18 (dz=15).
  // Default max_dz=4 should reject this fallback (returns nullopt).
  StubCache cache;
  cache[{3, 0, 0}] = {/*owns=*/true};

  auto result_default = resolveAncestorFallback<StubTile>(
      TileCoord{18, 0, 0},
      [&](const TileCoord& c) { return finder(cache, c); });
  EXPECT_FALSE(result_default.has_value())
      << "Default max_dz=4 should reject z=18 leaf with only z=3 ancestor";

  // Same setup with max_dz=20 should find the z=3 ancestor.
  auto result_loose = resolveAncestorFallback<StubTile>(
      TileCoord{18, 0, 0},
      [&](const TileCoord& c) { return finder(cache, c); },
      /*max_dz=*/20);
  ASSERT_TRUE(result_loose.has_value());
  EXPECT_EQ(result_loose->source_coord.z, 3);

  // With a closer ancestor (z=15, dz=3), default max_dz=4 should accept.
  cache[{15, 0, 0}] = {/*owns=*/true};
  auto result_close = resolveAncestorFallback<StubTile>(
      TileCoord{18, 0, 0},
      [&](const TileCoord& c) { return finder(cache, c); });
  ASSERT_TRUE(result_close.has_value());
  EXPECT_EQ(result_close->source_coord.z, 15);
}
