#include <gtest/gtest.h>

#include <hongtu/renderer/vegetation/vegetation_layer.hpp>

#include <glm/glm.hpp>

using hongtu::renderer::VegetationInstance;
using hongtu::renderer::VegetationLayer;

TEST(Vegetation, InstanceMatrixConvertsYUpAssetToZUpWorld) {
  VegetationInstance inst;
  inst.pos   = {10.0f, 20.0f, 0.0f};
  inst.scale = 2.0f;
  glm::mat4 m = VegetationLayer::composeInstanceMatrix(inst);

  // Asset-local "up" (glTF +Y) must land on world +Z, scaled.
  glm::vec4 tip = m * glm::vec4(0, 1, 0, 0);
  EXPECT_NEAR(tip.x, 0.0f, 1e-5f);
  EXPECT_NEAR(tip.y, 0.0f, 1e-5f);
  EXPECT_NEAR(tip.z, 2.0f, 1e-5f);

  // Origin lands at pos.
  glm::vec4 base = m * glm::vec4(0, 0, 0, 1);
  EXPECT_NEAR(base.x, 10.0f, 1e-5f);
  EXPECT_NEAR(base.y, 20.0f, 1e-5f);
  EXPECT_NEAR(base.z, 0.0f, 1e-5f);
}

TEST(Vegetation, YawRotatesAroundWorldZ) {
  VegetationInstance inst;
  inst.yaw = glm::half_pi<float>();  // 90°
  glm::mat4 m = VegetationLayer::composeInstanceMatrix(inst);
  // Asset-local +X (horizontal) → after Rx(90°) stays +X → after Rz(90°) → +Y.
  glm::vec4 v = m * glm::vec4(1, 0, 0, 0);
  EXPECT_NEAR(v.x, 0.0f, 1e-5f);
  EXPECT_NEAR(v.y, 1.0f, 1e-5f);
  EXPECT_NEAR(v.z, 0.0f, 1e-5f);
}

TEST(Vegetation, TintZeroIsIdentityAndVariationIsSubtle) {
  glm::vec4 c0 = VegetationLayer::tintToColor(0.0f);
  EXPECT_FLOAT_EQ(c0.r, 1.0f);
  EXPECT_FLOAT_EQ(c0.g, 1.0f);
  EXPECT_FLOAT_EQ(c0.b, 1.0f);

  glm::vec4 c1 = VegetationLayer::tintToColor(1.0f);
  // Darker, warmer, never garish: each channel stays within [0.6, 1.1].
  for (int i = 0; i < 3; ++i) {
    EXPECT_GT(c1[i], 0.6f);
    EXPECT_LT(c1[i], 1.1f);
  }
  EXPECT_LT(c1.g, c0.g);          // darker
  EXPECT_GT(c1.r, c1.b);          // warmer than blue
}

TEST(Vegetation, CellKeyBucketsOnGrid) {
  EXPECT_EQ(VegetationLayer::cellKey({0.0f, 0.0f, 5.0f}, 24.0f), glm::ivec2(0, 0));
  EXPECT_EQ(VegetationLayer::cellKey({23.9f, 0.0f, 0.0f}, 24.0f), glm::ivec2(0, 0));
  EXPECT_EQ(VegetationLayer::cellKey({24.1f, -0.1f, 0.0f}, 24.0f), glm::ivec2(1, -1));
  EXPECT_EQ(VegetationLayer::cellKey({-24.1f, 50.0f, 0.0f}, 24.0f), glm::ivec2(-2, 2));
}
