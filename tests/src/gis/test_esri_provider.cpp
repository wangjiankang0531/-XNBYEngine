#include <gtest/gtest.h>
#include <hongtu/gis/imagery/esri_provider.hpp>

using namespace hongtu::gis;

TEST(EsriProviderTest, BuildsZyxUrl) {
  EsriImageryProvider p;
  TileCoord c{5, 10, 7};  // z=5, x=10, y=7
  const std::string expected =
      "https://server.arcgisonline.com/ArcGIS/rest/services/"
      "World_Imagery/MapServer/tile/5/7/10";
  EXPECT_EQ(p.buildTileUrl(c), expected);
}

TEST(EsriProviderTest, ReportsZoomAndTileSize) {
  EsriImageryProvider p;
  EXPECT_EQ(p.getMinLevel(), 0);
  EXPECT_EQ(p.getMaxLevel(), 19);
  EXPECT_EQ(p.getTileSize(), 256);
  EXPECT_EQ(p.getName(), "Esri World Imagery");
}
