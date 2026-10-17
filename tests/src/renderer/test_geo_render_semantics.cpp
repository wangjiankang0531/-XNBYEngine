#include <gtest/gtest.h>

#include "geo_scene_fixture.hpp"

#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/renderer/picking/ray_caster.hpp>  // 完整 PickHit 定义

// RENDER-GLOBE-VISIBLE: 俯视地表时,屏幕中心不是纯黑(地球被渲染)。
TEST(GeoRender, GlobeVisibleAtCenter) {
  GeoSceneFixture fx;
  if (!fx.init()) GTEST_SKIP() << "no Vulkan device (headless)";
  fx.lookAtSite(33.5, 104.6, 5000.0);
  fx.render();
  auto c = fx.at(GeoSceneFixture::kW / 2, GeoSceneFixture::kH / 2);
  EXPECT_GT(c.r + c.g + c.b, 30) << "globe center is black — not rendered";
}

// RENDER-PLACE-AT-CURSOR(渲染侧):pickWorld(center) 投影回 ≈ center。
TEST(GeoRender, PickReprojectsToCursor) {
  GeoSceneFixture fx;
  if (!fx.init()) GTEST_SKIP() << "no Vulkan device (headless)";
  fx.lookAtSite(33.5, 104.6, 5000.0);
  fx.render();
  const double cx = GeoSceneFixture::kW / 2.0, cy = GeoSceneFixture::kH / 2.0;
  auto hit = fx.camera().pickWorld(cx, cy, GeoSceneFixture::kW, GeoSceneFixture::kH);
  ASSERT_TRUE(hit.has_value());
  glm::dvec2 sp = fx.project(hit->point);
  EXPECT_NEAR(sp.x, cx, 2.0);
  EXPECT_NEAR(sp.y, cy, 2.0);
}
