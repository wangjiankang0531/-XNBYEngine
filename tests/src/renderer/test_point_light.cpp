// 点光源选择 —— 取离相机最近 ≤max 盏,过滤无效灯。
#include <hongtu/renderer/pbr/point_light.hpp>

#include <gtest/gtest.h>

using hongtu::renderer::PointLight;
using hongtu::renderer::selectNearestLights;

static PointLight mk(float x, float r = 10.0f, float inten = 5.0f) {
  return PointLight{glm::vec3(x, 0, 0), r, glm::vec3(1), inten};
}

TEST(PointLight, EmptyInputEmptyOutput) {
  EXPECT_TRUE(selectNearestLights(glm::vec3(0), {}, 16).empty());
}

TEST(PointLight, KeepsAllWhenUnderCap) {
  auto out = selectNearestLights(glm::vec3(0), {mk(1), mk(2), mk(3)}, 16);
  EXPECT_EQ(out.size(), 3u);
}

TEST(PointLight, TakesNearestWhenOverCap) {
  std::vector<PointLight> all;
  for (int i = 0; i < 30; ++i) all.push_back(mk(static_cast<float>(i + 1)));
  auto out = selectNearestLights(glm::vec3(0), all, 16);
  ASSERT_EQ(out.size(), 16u);
  EXPECT_FLOAT_EQ(out.front().position.x, 1.0f);
  float maxx = 0.0f;
  for (const auto& l : out) maxx = std::max(maxx, l.position.x);
  EXPECT_FLOAT_EQ(maxx, 16.0f);
}

TEST(PointLight, FiltersInvalid) {
  std::vector<PointLight> all = {mk(1, 10, 0.0f),   // intensity 0
                                 mk(2, 0.0f, 5.0f), // radius 0
                                 mk(3, 10, 5.0f)};  // ok
  auto out = selectNearestLights(glm::vec3(0), all, 16);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_FLOAT_EQ(out.front().position.x, 3.0f);
}
