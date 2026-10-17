#include <gtest/gtest.h>
#include <hongtu/gis/tile/tile_coord.hpp>
#include <cmath>
#include <unordered_set>

using namespace hongtu;
using namespace hongtu::gis;

static constexpr double kPI = 3.14159265358979323846;
static constexpr double kEpsilon = 1e-10;

// ============================================================================
// GeoBounds Tests
// ============================================================================

TEST(GeoBoundsTest, ContainsPointInside) {
  GeoBounds b{-1.0, -0.5, 1.0, 0.5};
  EXPECT_TRUE(b.contains(0.0, 0.0));
  EXPECT_TRUE(b.contains(-0.5, -1.0));  // corner
  EXPECT_TRUE(b.contains(0.5, 1.0));    // corner
}

TEST(GeoBoundsTest, ContainsPointOutside) {
  GeoBounds b{-1.0, -0.5, 1.0, 0.5};
  EXPECT_FALSE(b.contains(1.0, 0.0));   // above north
  EXPECT_FALSE(b.contains(0.0, 2.0));   // east of east
  EXPECT_FALSE(b.contains(-1.0, 0.0));  // below south
  EXPECT_FALSE(b.contains(0.0, -2.0));  // west of west
}

TEST(GeoBoundsTest, IntersectsOverlapping) {
  GeoBounds a{-1.0, -0.5, 1.0, 0.5};
  GeoBounds b{0.0, 0.0, 2.0, 1.0};
  EXPECT_TRUE(a.intersects(b));
  EXPECT_TRUE(b.intersects(a));
}

TEST(GeoBoundsTest, IntersectsNonOverlapping) {
  GeoBounds a{-1.0, -0.5, 0.0, 0.5};
  GeoBounds b{1.0, -0.5, 2.0, 0.5};
  EXPECT_FALSE(a.intersects(b));
  EXPECT_FALSE(b.intersects(a));
}

TEST(GeoBoundsTest, IntersectsTouching) {
  GeoBounds a{-1.0, -0.5, 0.0, 0.5};
  GeoBounds b{0.0, -0.5, 1.0, 0.5};
  EXPECT_TRUE(a.intersects(b));  // edge touching
}

TEST(GeoBoundsTest, Center) {
  GeoBounds b{-1.0, -0.5, 1.0, 0.5};
  f64 lat, lon;
  b.center(lat, lon);
  EXPECT_NEAR(lat, 0.0, kEpsilon);
  EXPECT_NEAR(lon, 0.0, kEpsilon);
}

TEST(GeoBoundsTest, WidthAndHeight) {
  GeoBounds b{-1.0, -0.5, 1.0, 0.5};
  EXPECT_NEAR(b.width(), 2.0, kEpsilon);
  EXPECT_NEAR(b.height(), 1.0, kEpsilon);
}

// ============================================================================
// TileCoord Basic Tests
// ============================================================================

TEST(TileCoordTest, DefaultConstruction) {
  TileCoord t;
  EXPECT_EQ(t.z, 0);
  EXPECT_EQ(t.x, 0);
  EXPECT_EQ(t.y, 0);
  EXPECT_TRUE(t.isValid());
}

TEST(TileCoordTest, MaxTilesAtLevel) {
  EXPECT_EQ(TileCoord::maxTilesAtLevel(0), 1);
  EXPECT_EQ(TileCoord::maxTilesAtLevel(1), 2);
  EXPECT_EQ(TileCoord::maxTilesAtLevel(2), 4);
  EXPECT_EQ(TileCoord::maxTilesAtLevel(3), 8);
  EXPECT_EQ(TileCoord::maxTilesAtLevel(10), 1024);
  EXPECT_EQ(TileCoord::maxTilesAtLevel(-1), 0);
}

TEST(TileCoordTest, IsValid) {
  EXPECT_TRUE((TileCoord{0, 0, 0}).isValid());
  EXPECT_TRUE((TileCoord{1, 0, 0}).isValid());
  EXPECT_TRUE((TileCoord{1, 1, 1}).isValid());
  EXPECT_TRUE((TileCoord{2, 3, 3}).isValid());

  EXPECT_FALSE((TileCoord{-1, 0, 0}).isValid());
  EXPECT_FALSE((TileCoord{0, 1, 0}).isValid());   // x >= n at z=0
  EXPECT_FALSE((TileCoord{0, 0, 1}).isValid());   // y >= n at z=0
  EXPECT_FALSE((TileCoord{1, 2, 0}).isValid());   // x >= n at z=1
  EXPECT_FALSE((TileCoord{1, 0, -1}).isValid());  // negative y
  EXPECT_FALSE((TileCoord{1, -1, 0}).isValid());  // negative x
}

TEST(TileCoordTest, Equality) {
  TileCoord a{5, 10, 20};
  TileCoord b{5, 10, 20};
  TileCoord c{5, 10, 21};

  EXPECT_TRUE(a == b);
  EXPECT_FALSE(a != b);
  EXPECT_FALSE(a == c);
  EXPECT_TRUE(a != c);
}

TEST(TileCoordTest, ToString) {
  EXPECT_EQ((TileCoord{0, 0, 0}).toString(), "0/0/0");
  EXPECT_EQ((TileCoord{5, 10, 20}).toString(), "5/10/20");
  EXPECT_EQ((TileCoord{18, 215637, 99070}).toString(), "18/215637/99070");
}

// ============================================================================
// Parent / Child Relationships
// ============================================================================

TEST(TileCoordTest, ParentOfRoot) {
  TileCoord root{0, 0, 0};
  auto p = root.parent();
  EXPECT_EQ(p.z, -1);  // invalid, but that's expected
}

TEST(TileCoordTest, ParentAtZoom1) {
  // All 4 tiles at zoom 1 should have the same parent: 0/0/0
  for (int x = 0; x < 2; ++x) {
    for (int y = 0; y < 2; ++y) {
      auto p = (TileCoord{1, x, y}).parent();
      EXPECT_EQ(p, (TileCoord{0, 0, 0}));
    }
  }
}

TEST(TileCoordTest, ParentAtHigherZoom) {
  TileCoord t{5, 10, 20};
  auto p = t.parent();
  EXPECT_EQ(p, (TileCoord{4, 5, 10}));

  auto pp = p.parent();
  EXPECT_EQ(pp, (TileCoord{3, 2, 5}));
}

TEST(TileCoordTest, ChildrenOfRoot) {
  TileCoord root{0, 0, 0};
  auto ch = root.children();

  EXPECT_EQ(ch[0], (TileCoord{1, 0, 0}));
  EXPECT_EQ(ch[1], (TileCoord{1, 1, 0}));
  EXPECT_EQ(ch[2], (TileCoord{1, 0, 1}));
  EXPECT_EQ(ch[3], (TileCoord{1, 1, 1}));

  for (const auto& c : ch) {
    EXPECT_TRUE(c.isValid());
  }
}

TEST(TileCoordTest, ChildrenParentRoundtrip) {
  TileCoord t{3, 5, 6};
  auto ch = t.children();

  for (const auto& c : ch) {
    EXPECT_EQ(c.parent(), t);
  }
}

TEST(TileCoordTest, AllChildrenAreValid) {
  TileCoord t{10, 500, 300};
  auto ch = t.children();
  for (const auto& c : ch) {
    EXPECT_TRUE(c.isValid());
  }
}

// ============================================================================
// toBounds Tests
// ============================================================================

TEST(TileCoordTest, ToBoundsZoom0) {
  TileCoord root{0, 0, 0};
  auto b = root.toBounds();

  EXPECT_NEAR(b.west, -kPI, kEpsilon);
  EXPECT_NEAR(b.east, kPI, kEpsilon);
  // Web Mercator max latitude: atan(sinh(PI)) ~ 85.05 degrees
  EXPECT_NEAR(b.north, std::atan(std::sinh(kPI)), kEpsilon);
  EXPECT_NEAR(b.south, -std::atan(std::sinh(kPI)), kEpsilon);
}

TEST(TileCoordTest, ToBoundsZoom1Quadrants) {
  // At zoom 1, there are 4 tiles (2x2)
  auto b00 = (TileCoord{1, 0, 0}).toBounds();  // NW
  auto b10 = (TileCoord{1, 1, 0}).toBounds();  // NE
  auto b01 = (TileCoord{1, 0, 1}).toBounds();  // SW
  auto b11 = (TileCoord{1, 1, 1}).toBounds();  // SE

  // West column has west = -PI
  EXPECT_NEAR(b00.west, -kPI, kEpsilon);
  EXPECT_NEAR(b01.west, -kPI, kEpsilon);
  // East column has east = PI
  EXPECT_NEAR(b10.east, kPI, kEpsilon);
  EXPECT_NEAR(b11.east, kPI, kEpsilon);
  // Meridian at x boundary
  EXPECT_NEAR(b00.east, 0.0, kEpsilon);
  EXPECT_NEAR(b10.west, 0.0, kEpsilon);
  // Equator at y boundary
  EXPECT_NEAR(b00.south, 0.0, kEpsilon);
  EXPECT_NEAR(b01.north, 0.0, kEpsilon);
}

TEST(TileCoordTest, ToBoundsAdjacentTilesShareEdges) {
  // Two horizontally adjacent tiles at zoom 5
  TileCoord left{5, 10, 15};
  TileCoord right{5, 11, 15};
  auto bl = left.toBounds();
  auto br = right.toBounds();

  EXPECT_NEAR(bl.east, br.west, kEpsilon);
  EXPECT_NEAR(bl.north, br.north, kEpsilon);
  EXPECT_NEAR(bl.south, br.south, kEpsilon);

  // Two vertically adjacent tiles
  TileCoord top{5, 10, 15};
  TileCoord bottom{5, 10, 16};
  auto bt = top.toBounds();
  auto bb = bottom.toBounds();

  EXPECT_NEAR(bt.south, bb.north, kEpsilon);
  EXPECT_NEAR(bt.west, bb.west, kEpsilon);
  EXPECT_NEAR(bt.east, bb.east, kEpsilon);
}

TEST(TileCoordTest, ToBoundsChildrenCoverParent) {
  TileCoord parent{3, 5, 6};
  auto pb = parent.toBounds();
  auto ch = parent.children();

  // Children bounds should cover the parent bounds exactly
  f64 min_west = ch[0].toBounds().west;
  f64 max_east = ch[0].toBounds().east;
  f64 min_south = ch[0].toBounds().south;
  f64 max_north = ch[0].toBounds().north;

  for (const auto& c : ch) {
    auto cb = c.toBounds();
    min_west = std::min(min_west, cb.west);
    max_east = std::max(max_east, cb.east);
    min_south = std::min(min_south, cb.south);
    max_north = std::max(max_north, cb.north);
  }

  EXPECT_NEAR(min_west, pb.west, kEpsilon);
  EXPECT_NEAR(max_east, pb.east, kEpsilon);
  EXPECT_NEAR(min_south, pb.south, kEpsilon);
  EXPECT_NEAR(max_north, pb.north, kEpsilon);
}

// ============================================================================
// fromGeodetic Tests
// ============================================================================

TEST(TileCoordTest, FromGeodeticOriginZoom0) {
  auto t = TileCoord::fromGeodetic(0.0, 0.0, 0);
  EXPECT_EQ(t.z, 0);
  EXPECT_EQ(t.x, 0);
  EXPECT_EQ(t.y, 0);
}

TEST(TileCoordTest, FromGeodeticQuadrantsZoom1) {
  // NW quadrant: positive lat, negative lon
  auto nw = TileCoord::fromGeodetic(45.0, -90.0, 1);
  EXPECT_EQ(nw, (TileCoord{1, 0, 0}));

  // NE quadrant
  auto ne = TileCoord::fromGeodetic(45.0, 90.0, 1);
  EXPECT_EQ(ne, (TileCoord{1, 1, 0}));

  // SW quadrant
  auto sw = TileCoord::fromGeodetic(-45.0, -90.0, 1);
  EXPECT_EQ(sw, (TileCoord{1, 0, 1}));

  // SE quadrant
  auto se = TileCoord::fromGeodetic(-45.0, 90.0, 1);
  EXPECT_EQ(se, (TileCoord{1, 1, 1}));
}

TEST(TileCoordTest, FromGeodeticKnownTiles) {
  // Beijing: ~39.9N, 116.4E
  // At zoom 4: should be in tile 4/13/6 (well-known)
  auto beijing = TileCoord::fromGeodetic(39.9042, 116.4074, 4);
  EXPECT_EQ(beijing.z, 4);
  EXPECT_EQ(beijing.x, 13);
  EXPECT_EQ(beijing.y, 6);
}

TEST(TileCoordTest, FromGeodeticResultIsValid) {
  // Test various points at various zoom levels
  struct TestCase {
    f64 lat, lon;
    i32 zoom;
  };
  TestCase cases[] = {
      {0.0, 0.0, 0},        {0.0, 0.0, 10},         {85.0, 180.0, 5},
      {-85.0, -180.0, 5},   {39.9042, 116.4074, 18}, {-33.8688, 151.2093, 15},
      {51.5074, -0.1278, 8},
  };

  for (const auto& tc : cases) {
    auto t = TileCoord::fromGeodetic(tc.lat, tc.lon, tc.zoom);
    EXPECT_TRUE(t.isValid()) << "lat=" << tc.lat << " lon=" << tc.lon << " zoom=" << tc.zoom
                             << " -> " << t.toString();
  }
}

TEST(TileCoordTest, FromGeodeticPointInsideTileBounds) {
  // The generated tile's bounds should contain the original point
  f64 lat = 39.9042;
  f64 lon = 116.4074;
  i32 zoom = 10;

  auto t = TileCoord::fromGeodetic(lat, lon, zoom);
  auto b = t.toBounds();

  f64 lat_rad = lat * kPI / 180.0;
  f64 lon_rad = lon * kPI / 180.0;

  EXPECT_TRUE(b.contains(lat_rad, lon_rad))
      << "Tile " << t.toString() << " bounds [" << b.west << "," << b.south << "," << b.east << ","
      << b.north << "] should contain (" << lat_rad << "," << lon_rad << ")";
}

TEST(TileCoordTest, FromGeodeticRoundtripMultiplePoints) {
  // For various points, the tile should contain the original point
  struct TestCase {
    f64 lat, lon;
  };
  TestCase cases[] = {
      {0.0, 0.0},       {51.5074, -0.1278},   {40.7128, -74.0060}, {-33.8688, 151.2093},
      {35.6762, 139.6503}, {55.7558, 37.6173}, {-22.9068, -43.1729},
  };

  for (i32 zoom = 1; zoom <= 15; zoom += 2) {
    for (const auto& tc : cases) {
      auto t = TileCoord::fromGeodetic(tc.lat, tc.lon, zoom);
      auto b = t.toBounds();
      f64 lat_rad = tc.lat * kPI / 180.0;
      f64 lon_rad = tc.lon * kPI / 180.0;

      EXPECT_TRUE(b.contains(lat_rad, lon_rad))
          << "zoom=" << zoom << " lat=" << tc.lat << " lon=" << tc.lon << " tile=" << t.toString();
    }
  }
}

TEST(TileCoordTest, FromGeodeticClampsExtremeLatitude) {
  // Latitudes beyond Web Mercator range should be clamped
  auto t_north = TileCoord::fromGeodetic(89.0, 0.0, 5);
  EXPECT_TRUE(t_north.isValid());
  EXPECT_EQ(t_north.y, 0);  // Extreme north should map to top row

  auto t_south = TileCoord::fromGeodetic(-89.0, 0.0, 5);
  EXPECT_TRUE(t_south.isValid());
  EXPECT_EQ(t_south.y, TileCoord::maxTilesAtLevel(5) - 1);  // Extreme south should map to bottom
}

// ============================================================================
// Hash Tests
// ============================================================================

TEST(TileCoordTest, HashConsistency) {
  TileCoord::Hash hasher;
  TileCoord a{5, 10, 20};
  TileCoord b{5, 10, 20};

  EXPECT_EQ(hasher(a), hasher(b));
}

TEST(TileCoordTest, HashDifferentTiles) {
  TileCoord::Hash hasher;
  TileCoord a{5, 10, 20};
  TileCoord b{5, 10, 21};
  TileCoord c{5, 11, 20};
  TileCoord d{6, 10, 20};

  // Different tiles should (very likely) have different hashes
  EXPECT_NE(hasher(a), hasher(b));
  EXPECT_NE(hasher(a), hasher(c));
  EXPECT_NE(hasher(a), hasher(d));
}

TEST(TileCoordTest, UnorderedSetUsage) {
  std::unordered_set<TileCoord, TileCoord::Hash> tile_set;

  tile_set.insert({0, 0, 0});
  tile_set.insert({1, 0, 0});
  tile_set.insert({1, 1, 0});
  tile_set.insert({0, 0, 0});  // duplicate

  EXPECT_EQ(tile_set.size(), 3u);
  EXPECT_TRUE(tile_set.count({0, 0, 0}) == 1);
  EXPECT_TRUE(tile_set.count({1, 0, 0}) == 1);
  EXPECT_TRUE(tile_set.count({2, 0, 0}) == 0);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(TileCoordTest, WebMercatorMaxLatitude) {
  // atan(sinh(PI)) should equal kWebMercatorMaxLatRad
  f64 computed = std::atan(std::sinh(kPI));
  EXPECT_NEAR(computed, kWebMercatorMaxLatRad, 1e-12);

  // In degrees
  f64 computed_deg = computed * 180.0 / kPI;
  EXPECT_NEAR(computed_deg, kWebMercatorMaxLatDeg, 1e-8);
}

TEST(TileCoordTest, HighZoomLevel) {
  // Zoom 20 should still work
  auto t = TileCoord::fromGeodetic(39.9042, 116.4074, 20);
  EXPECT_TRUE(t.isValid());
  EXPECT_EQ(t.z, 20);
  EXPECT_GE(t.x, 0);
  EXPECT_LT(t.x, TileCoord::maxTilesAtLevel(20));
}

TEST(TileCoordTest, BoundsAreInRadians) {
  TileCoord t{0, 0, 0};
  auto b = t.toBounds();

  // All values should be in radians (roughly [-PI, PI] for lon, smaller for lat)
  EXPECT_GT(b.west, -4.0);
  EXPECT_LT(b.east, 4.0);
  EXPECT_GT(b.south, -2.0);
  EXPECT_LT(b.north, 2.0);
}
