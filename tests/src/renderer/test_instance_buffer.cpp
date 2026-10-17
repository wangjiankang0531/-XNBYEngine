#include <gtest/gtest.h>
#include <hongtu/renderer/instancing/instance_buffer.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace hongtu::renderer;

TEST(InstanceBufferTest, AddAndCount) {
  InstanceBuffer buf;
  EXPECT_EQ(buf.getCount(), 0u);

  InstanceData data{};
  data.model_matrix = glm::mat4(1.0f);
  data.color = {1, 0, 0, 1};

  buf.addInstance(data);
  EXPECT_EQ(buf.getCount(), 1u);

  buf.addInstance(data);
  EXPECT_EQ(buf.getCount(), 2u);
}

TEST(InstanceBufferTest, RemoveSwapsWithLast) {
  InstanceBuffer buf;
  InstanceData d1{}, d2{}, d3{};
  d1.color = {1, 0, 0, 1};
  d2.color = {0, 1, 0, 1};
  d3.color = {0, 0, 1, 1};

  buf.addInstance(d1);
  buf.addInstance(d2);
  buf.addInstance(d3);
  EXPECT_EQ(buf.getCount(), 3u);

  buf.removeInstance(0); // d1 removed, d3 moved to index 0
  EXPECT_EQ(buf.getCount(), 2u);
  EXPECT_FLOAT_EQ(buf.getInstance(0).color.b, 1.0f); // was d3
}

TEST(InstanceBufferTest, UpdateInstance) {
  InstanceBuffer buf;
  InstanceData data{};
  data.color = {1, 0, 0, 1};
  buf.addInstance(data);

  data.color = {0, 1, 0, 1};
  buf.updateInstance(0, data);
  EXPECT_FLOAT_EQ(buf.getInstance(0).color.g, 1.0f);
}

TEST(InstanceBufferTest, SizeBytes) {
  InstanceBuffer buf;
  EXPECT_EQ(buf.getSizeBytes(), 0u);

  InstanceData data{};
  buf.addInstance(data);
  EXPECT_EQ(buf.getSizeBytes(), sizeof(InstanceData));
}

TEST(InstanceBufferTest, GenerateDrawCommand) {
  InstanceBuffer buf;
  InstanceData data{};
  for (int i = 0; i < 100; ++i) buf.addInstance(data);

  auto cmd = buf.generateDrawCommand(36, 0, 0); // 36 indices for a cube
  EXPECT_EQ(cmd.index_count, 36u);
  EXPECT_EQ(cmd.instance_count, 100u);
  EXPECT_EQ(cmd.first_index, 0u);
  EXPECT_EQ(cmd.vertex_offset, 0);
  EXPECT_EQ(cmd.first_instance, 0u);
}

TEST(InstanceBufferTest, Reserve) {
  InstanceBuffer buf;
  buf.reserve(10000);
  EXPECT_EQ(buf.getCount(), 0u); // Reserve doesn't add instances
}

TEST(InstanceBufferTest, Clear) {
  InstanceBuffer buf;
  InstanceData data{};
  for (int i = 0; i < 50; ++i) buf.addInstance(data);
  EXPECT_EQ(buf.getCount(), 50u);
  buf.clear();
  EXPECT_EQ(buf.getCount(), 0u);
}

// ============================================================================
// Frustum Culling
// ============================================================================

TEST(FrustumCullTest, ExtractFrustumFromViewProj) {
  auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
  auto proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
  auto vp = proj * view;

  auto frustum = InstanceBuffer::extractFrustum(vp);

  // All planes should be normalized (length ~1)
  for (const auto& plane : frustum.planes) {
    float len = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
    EXPECT_NEAR(len, 1.0f, 0.01f);
  }
}

TEST(FrustumCullTest, VisibleAABB) {
  auto view = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
  auto proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
  auto frustum = InstanceBuffer::extractFrustum(proj * view);

  // Box at origin - should be visible
  AABB visible_box = {{-1, -1, -1}, {1, 1, 1}};
  EXPECT_TRUE(InstanceBuffer::isAABBVisible(visible_box, frustum));

  // Box far behind camera - should not be visible
  AABB behind_box = {{-1, -1, 20}, {1, 1, 22}};
  EXPECT_FALSE(InstanceBuffer::isAABBVisible(behind_box, frustum));

  // Box far to the side - should not be visible
  AABB side_box = {{100, 100, -1}, {102, 102, 1}};
  EXPECT_FALSE(InstanceBuffer::isAABBVisible(side_box, frustum));
}

TEST(FrustumCullTest, CullInstances) {
  auto view = glm::lookAt(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
  auto proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
  auto frustum = InstanceBuffer::extractFrustum(proj * view);

  InstanceBuffer buf;
  InstanceData data{};
  data.model_matrix = glm::mat4(1.0f);

  // Add 10 instances
  for (int i = 0; i < 10; ++i) buf.addInstance(data);

  // 5 visible (near origin), 5 invisible (far away)
  std::vector<AABB> bounds(10);
  for (int i = 0; i < 5; ++i) {
    float x = static_cast<float>(i - 2);
    bounds[i] = {{x - 0.5f, -0.5f, -0.5f}, {x + 0.5f, 0.5f, 0.5f}};
  }
  for (int i = 5; i < 10; ++i) {
    float x = static_cast<float>((i - 5) * 100 + 200);
    bounds[i] = {{x, x, x}, {x + 1, x + 1, x + 1}};
  }

  auto visible = buf.frustumCull(bounds, frustum);
  EXPECT_EQ(visible.size(), 5u);
  for (auto idx : visible) {
    EXPECT_LT(idx, 5u);
  }
}
