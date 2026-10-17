#include <gtest/gtest.h>
#include <hongtu/renderer/culling/occlusion_culling.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

using namespace hongtu::renderer;

TEST(HiZDepthBufferTest, BuildAndQuery) {
  // Create a simple 4x4 depth buffer
  std::vector<float> depth(16, 0.5f);
  depth[0] = 0.1f; // One near pixel

  HiZDepthBuffer hiz;
  hiz.build(depth.data(), 4, 4, 3);

  EXPECT_GE(hiz.getMipCount(), 2u);

  uint32_t w, h;
  hiz.getMipSize(0, w, h);
  EXPECT_EQ(w, 4u);
  EXPECT_EQ(h, 4u);
}

TEST(HiZDepthBufferTest, MaxReduction) {
  // 4x4 buffer with varying depth
  float depth[] = {
    0.1f, 0.2f, 0.3f, 0.4f,
    0.5f, 0.6f, 0.7f, 0.8f,
    0.1f, 0.2f, 0.3f, 0.4f,
    0.5f, 0.6f, 0.7f, 0.8f,
  };

  HiZDepthBuffer hiz;
  hiz.build(depth, 4, 4, 3);

  // Mip 1 (2x2) should have max of each 2x2 block
  auto& mip1 = hiz.getMipData(1);
  EXPECT_EQ(mip1.size(), 4u);
  EXPECT_FLOAT_EQ(mip1[0], 0.6f); // max(0.1, 0.2, 0.5, 0.6)
}

TEST(OcclusionCullingTest, FrustumCullOnly) {
  auto view = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
  auto proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
  auto frustum = InstanceBuffer::extractFrustum(proj * view);

  std::vector<CullObject> objects;
  // 5 visible objects at origin
  for (int i = 0; i < 5; ++i) {
    float x = static_cast<float>(i - 2);
    objects.push_back({{{x-0.5f, -0.5f, -0.5f}, {x+0.5f, 0.5f, 0.5f}}, static_cast<uint32_t>(i)});
  }
  // 5 invisible objects far away
  for (int i = 0; i < 5; ++i) {
    float x = 500.0f + static_cast<float>(i);
    objects.push_back({{{x, x, x}, {x+1, x+1, x+1}}, static_cast<uint32_t>(5+i)});
  }

  OcclusionCullingSystem sys;
  auto result = sys.frustumCull(objects, frustum);

  EXPECT_EQ(result.visible_objects.size(), 5u);
  EXPECT_EQ(result.total_objects, 10u);
  EXPECT_EQ(result.culled_by_frustum, 5u);
  EXPECT_GT(result.cull_efficiency, 0.0f);
}

TEST(OcclusionCullingTest, GenerateDrawCommands) {
  OcclusionCullingSystem sys;
  CullingResult result;
  result.visible_objects = {0, 3, 7};

  auto cmds = sys.generateDrawCommands(result, 36);
  EXPECT_EQ(cmds.size(), 3u);
  EXPECT_EQ(cmds[0].index_count, 36u);
  EXPECT_EQ(cmds[0].instance_count, 1u);
}

TEST(OcclusionCullingTest, EmptyScene) {
  auto frustum = InstanceBuffer::extractFrustum(glm::mat4(1.0f));
  OcclusionCullingSystem sys;
  auto result = sys.frustumCull({}, frustum);
  EXPECT_EQ(result.visible_objects.size(), 0u);
}
