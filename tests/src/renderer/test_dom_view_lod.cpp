#include <gtest/gtest.h>
#include <hongtu/renderer/dom_view_lod.hpp>

using namespace hongtu::renderer;

TEST(DomViewLod, ObliqueFocusFollowsVisibleGroundRatherThanCameraFoot) {
  auto flat = [](float, float) {
    return 0.0f;
  };
  auto footprint = domViewFootprint({0, 0, 350}, {0, 900, 0}, 50, 1.6f, 900, flat);
  ASSERT_TRUE(footprint.valid);
  EXPECT_NEAR(footprint.focus.y, 900, 0.01f);
  EXPECT_NEAR(footprint.focus.z, 0, 0.001f);
  EXPECT_EQ(selectDomViewLod(footprint.meters_per_pixel), 0);
}

TEST(DomViewLod, ViewportResolutionAndFieldOfViewDriveRefinement) {
  auto flat = [](float, float) {
    return 0.0f;
  };
  auto low = domViewFootprint({0, 0, 1000}, {0, 0, 0}, 60, 1.6f, 400, flat);
  auto high = domViewFootprint({0, 0, 1000}, {0, 0, 0}, 60, 1.6f, 1600, flat);
  auto narrow = domViewFootprint({0, 0, 1000}, {0, 0, 0}, 20, 1.6f, 400, flat);
  ASSERT_TRUE(low.valid && high.valid && narrow.valid);
  EXPECT_NEAR(low.meters_per_pixel, 1000 * 2 * std::tan(glm::radians(30.0f)) / 400, 0.002f);
  EXPECT_EQ(selectDomViewLod(low.meters_per_pixel), 2);
  EXPECT_EQ(selectDomViewLod(high.meters_per_pixel), 0);
  EXPECT_EQ(selectDomViewLod(narrow.meters_per_pixel), 0);
}

TEST(DomViewLod, UphillTerrainIntersectionAndHorizonAreBounded) {
  auto slope = [](float, float y) {
    return y * 0.1f;
  };
  auto hit = intersectDomTerrain(glm::vec3(0, 0, 100), glm::vec3(0, 1, -0.1f), slope);
  ASSERT_TRUE(hit);
  EXPECT_NEAR(hit->y, 500, 0.01f);
  EXPECT_NEAR(hit->z, 50, 0.01f);
  EXPECT_FALSE(intersectDomTerrain(glm::vec3(0, 0, 100), glm::vec3(0, 1, 0), slope));
  EXPECT_FALSE(domViewFootprint({0, 0, 100}, {0, 100, 100}, 50, 1.6f, 0, slope).valid);
}

TEST(DomViewLod, HysteresisAndNativeResolutionCeiling) {
  EXPECT_EQ(selectDomViewLod(0.95f, 1), 1);
  EXPECT_EQ(selectDomViewLod(0.8f, 1), 0);
  EXPECT_EQ(selectDomViewLod(1.1f, 0), 0);
  EXPECT_EQ(selectDomViewLod(1.3f, 0), 1);
  EXPECT_EQ(selectDomViewLod(0.001f, 2), 0);
  EXPECT_EQ(selectDomViewLod(NAN, 1), 1);
}
