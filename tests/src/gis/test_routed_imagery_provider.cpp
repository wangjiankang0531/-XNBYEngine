#include <gtest/gtest.h>
#include <hongtu/gis/imagery/imagery_provider.hpp>
#include <hongtu/gis/imagery/routed_imagery_provider.hpp>

#include <memory>
#include <string>
#include <unordered_set>

using namespace hongtu;
using namespace hongtu::gis;

namespace {

  class SparseProvider final : public ImageryProvider {
  public:
    SparseProvider(std::string name,
                   std::unordered_set<TileCoord, TileCoord::Hash> available,
                   i32 min_level,
                   i32 max_level)
        : name_(std::move(name)), available_(std::move(available)), min_(min_level), max_(max_level) {
    }

    std::string buildTileUrl(const TileCoord& coord) const override {
      return "file:///" + name_ + '/' + coord.toString() + ".jpg";
    }
    i32 getMinLevel() const override {
      return min_;
    }
    i32 getMaxLevel() const override {
      return max_;
    }
    i32 getTileSize() const override {
      return 256;
    }
    std::string getName() const override {
      return name_;
    }
    bool supportsTile(const TileCoord& coord) const override {
      return ImageryProvider::supportsTile(coord) && available_.contains(coord);
    }

  private:
    std::string name_;
    std::unordered_set<TileCoord, TileCoord::Hash> available_;
    i32 min_;
    i32 max_;
  };

  class WorldProvider final : public ImageryProvider {
  public:
    std::string buildTileUrl(const TileCoord& coord) const override {
      return "https://fallback.invalid/" + coord.toString();
    }
    i32 getMinLevel() const override {
      return 0;
    }
    i32 getMaxLevel() const override {
      return 19;
    }
    i32 getTileSize() const override {
      return 256;
    }
    std::string getName() const override {
      return "fallback";
    }
  };

} // namespace

TEST(RoutedImageryProviderTest, RoutesByCoverageZoomPriorityAndAvailability) {
  const TileCoord huilian_z8 = TileCoord::fromGeodetic(22.7477685, 114.5996405, 8);
  const TileCoord huilian_z10 = TileCoord::fromGeodetic(22.7477685, 114.5996405, 10);
  const TileCoord beijing_z10 = TileCoord::fromGeodetic(39.9042, 116.4074, 10);

  auto regional_z8 =
      std::make_shared<SparseProvider>("regional-z8", std::unordered_set<TileCoord, TileCoord::Hash>{huilian_z8}, 8, 8);
  auto regional_z10 = std::make_shared<SparseProvider>(
      "regional-z10", std::unordered_set<TileCoord, TileCoord::Hash>{huilian_z10}, 10, 10);
  auto fallback = std::make_shared<WorldProvider>();

  RoutedImageryProvider routed("HuilianComposite-v1", fallback);
  const GeoBounds coverage = RoutedImageryProvider::boundsFromDegrees(108.0, 17.0, 120.0, 29.0);
  EXPECT_TRUE(routed.addRoute({"south-china-z8", coverage, 8, 8, 100, regional_z8}));
  EXPECT_TRUE(routed.addRoute({"huilian-z10", coverage, 10, 10, 200, regional_z10}));

  EXPECT_EQ(routed.resolveRouteName(huilian_z8), "south-china-z8");
  EXPECT_EQ(routed.resolveRouteName(huilian_z10), "huilian-z10");
  EXPECT_EQ(routed.resolveRouteName(beijing_z10), "fallback");

  EXPECT_NE(routed.buildTileUrl(huilian_z8).find("regional-z8"), std::string::npos);
  EXPECT_NE(routed.buildTileUrl(huilian_z10).find("regional-z10"), std::string::npos);
  EXPECT_NE(routed.buildTileUrl(beijing_z10).find("fallback.invalid"), std::string::npos);
  EXPECT_EQ(routed.getRouteRequestCount("south-china-z8"), 1u);
  EXPECT_EQ(routed.getRouteRequestCount("huilian-z10"), 1u);
  EXPECT_EQ(routed.getFallbackRequestCount(), 1u);
  EXPECT_EQ(routed.getCacheNamespace(), "HuilianComposite-v1");
  EXPECT_EQ(routed.getMinLevel(), 0);
  EXPECT_EQ(routed.getMaxLevel(), 19);
}

TEST(RoutedImageryProviderTest, SparseRouteFallsBackWhenTileIsMissing) {
  const TileCoord present = TileCoord::fromGeodetic(22.75, 114.60, 8);
  const TileCoord adjacent{present.z, present.x + 1, present.y};
  auto sparse =
      std::make_shared<SparseProvider>("sparse", std::unordered_set<TileCoord, TileCoord::Hash>{present}, 8, 8);

  RoutedImageryProvider routed("SparseWithFallback", std::make_shared<WorldProvider>());
  EXPECT_TRUE(routed.addRoute(
      {"offline", RoutedImageryProvider::boundsFromDegrees(-180.0, -85.0, 180.0, 85.0), 8, 8, 100, sparse}));

  EXPECT_EQ(routed.resolveRouteName(present), "offline");
  EXPECT_EQ(routed.resolveRouteName(adjacent), "fallback");
  EXPECT_NE(routed.buildTileUrl(adjacent).find("fallback.invalid"), std::string::npos);
  EXPECT_EQ(routed.getSourceTransitionEdges(present) & TileSourceTransitionRight, TileSourceTransitionRight);
  EXPECT_EQ(routed.getSourceTransitionEdges(adjacent) & TileSourceTransitionLeft, TileSourceTransitionLeft);
}

TEST(RoutedImageryProviderTest, SourceTransitionsIgnoreSameRouteAndWorldWrap) {
  const TileCoord west{3, 0, 3};
  const TileCoord east{3, 7, 3};
  auto sparse =
      std::make_shared<SparseProvider>("dateline", std::unordered_set<TileCoord, TileCoord::Hash>{west, east}, 3, 3);

  RoutedImageryProvider routed("DatelineComposite", std::make_shared<WorldProvider>());
  EXPECT_TRUE(routed.addRoute(
      {"offline", RoutedImageryProvider::boundsFromDegrees(-180.0, -85.0, 180.0, 85.0), 3, 3, 100, sparse}));

  EXPECT_EQ(routed.resolveRouteName(west), "offline");
  EXPECT_EQ(routed.resolveRouteName(east), "offline");
  EXPECT_EQ(routed.getSourceTransitionEdges(west) & TileSourceTransitionLeft, 0u);
  EXPECT_EQ(routed.getSourceTransitionEdges(east) & TileSourceTransitionRight, 0u);
  EXPECT_NE(routed.getSourceTransitionEdges(west) & TileSourceTransitionRight, 0u);
  EXPECT_NE(routed.getSourceTransitionEdges(east) & TileSourceTransitionLeft, 0u);
}

TEST(RoutedImageryProviderTest, RejectsInvalidRoutes) {
  RoutedImageryProvider routed("NoFallback");
  EXPECT_FALSE(routed.supportsTile({8, 205, 111}));
  EXPECT_FALSE(routed.addRoute({"", {}, 8, 8, 0, std::make_shared<WorldProvider>()}));
  EXPECT_FALSE(routed.addRoute({"null", {}, 8, 8, 0, nullptr}));
  EXPECT_FALSE(routed.addRoute({"reversed",
                                RoutedImageryProvider::boundsFromDegrees(120.0, 20.0, 110.0, 30.0),
                                8,
                                8,
                                0,
                                std::make_shared<WorldProvider>()}));
}
