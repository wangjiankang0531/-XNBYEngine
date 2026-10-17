#include <hongtu/renderer/globe/globe_renderer.hpp>

#include <gtest/gtest.h>

using hongtu::renderer::GlobeRenderer;

TEST(GlobeRendererTest, TileRenderingCanBeToggled) {
  GlobeRenderer renderer(nullptr);

  EXPECT_TRUE(renderer.isTileRenderingEnabled());

  renderer.setTileRenderingEnabled(false);
  EXPECT_FALSE(renderer.isTileRenderingEnabled());
  EXPECT_EQ(renderer.getVisibleTileCount(), 0);
  EXPECT_EQ(renderer.getLoadedTileCount(), 0);
  EXPECT_EQ(renderer.getPendingTileCount(), 0);

  renderer.setTileRenderingEnabled(true);
  EXPECT_TRUE(renderer.isTileRenderingEnabled());
}
