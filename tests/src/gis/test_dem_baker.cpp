#include <gtest/gtest.h>
#include <hongtu/gis/tile/dem_baker.hpp>
#include <hongtu/gis/terrain/dem_provider.hpp>
#include <unordered_map>

using namespace hongtu;
using namespace hongtu::gis;

namespace {

DemTile makeFlatDem(i32 width, i32 height, f32 value) {
  DemTile t;
  t.width = width;
  t.height = height;
  t.heights.assign(static_cast<size_t>(width) * height, value);
  return t;
}

DemTile makeRampDem(i32 width, i32 height) {
  DemTile t;
  t.width = width;
  t.height = height;
  t.heights.resize(static_cast<size_t>(width) * height);
  for (i32 r = 0; r < height; ++r)
    for (i32 c = 0; c < width; ++c)
      t.heights[r * width + c] = static_cast<f32>(c) * 10.0f;
  return t;
}

using Lookup = std::unordered_map<TileCoord, DemTile, TileCoord::Hash>;

}  // namespace

TEST(DemBaker, FlatDemFillsAllHeights) {
  Lookup dem;
  dem[{5, 1, 2}] = makeFlatDem(256, 256, 1234.5f);
  GeometryTile geom;
  geom.coord = {5, 1, 2};

  bool baked = bakeDemIntoGeometry(geom, [&](const TileCoord& c) {
    auto it = dem.find(c);
    return it == dem.end() ? nullptr : &it->second;
  });

  EXPECT_TRUE(baked);
  for (f32 h : geom.heights) EXPECT_NEAR(h, 1234.5f, 1e-3f);
  EXPECT_NEAR(geom.min_height, 1234.5f, 1e-3f);
  EXPECT_NEAR(geom.max_height, 1234.5f, 1e-3f);
  EXPECT_TRUE(geom.dirty_height);
}

TEST(DemBaker, ParentDemFallsThrough) {
  // Leaf (6, 2, 4); parent (5, 1, 2) has DEM, leaf doesn't.
  Lookup dem;
  dem[{5, 1, 2}] = makeFlatDem(64, 64, 100.0f);
  GeometryTile geom;
  geom.coord = {6, 2, 4};

  bool baked = bakeDemIntoGeometry(geom, [&](const TileCoord& c) {
    auto it = dem.find(c);
    return it == dem.end() ? nullptr : &it->second;
  });

  EXPECT_TRUE(baked);
  for (f32 h : geom.heights) EXPECT_NEAR(h, 100.0f, 1e-3f);
}

TEST(DemBaker, NoDemAvailable) {
  Lookup dem;  // empty
  GeometryTile geom;
  geom.coord = {5, 1, 2};

  bool baked = bakeDemIntoGeometry(geom, [&](const TileCoord&) -> const DemTile* { return nullptr; });

  EXPECT_FALSE(baked);
  EXPECT_FALSE(geom.dirty_height);
}

TEST(DemBaker, RampDemInterpolatesMonotonically) {
  Lookup dem;
  dem[{5, 1, 2}] = makeRampDem(33, 33);  // height = col * 10
  GeometryTile geom;
  geom.coord = {5, 1, 2};
  bakeDemIntoGeometry(geom, [&](const TileCoord& c) {
    auto it = dem.find(c);
    return it == dem.end() ? nullptr : &it->second;
  });
  // West edge (col=0) ≈ 0, east edge (col=32) ≈ 320
  EXPECT_NEAR(geom.heights[0], 0.0f, 1.0f);
  EXPECT_NEAR(geom.heights[32], 320.0f, 1.0f);
}
