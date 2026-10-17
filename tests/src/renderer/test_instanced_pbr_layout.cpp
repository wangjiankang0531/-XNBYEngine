// Layout-verification tests for the per-instance VBO of InstancedPbrPipeline.
//
// We can't spin up a Vulkan device in the unit-test harness, so we verify
// the host-side promises the GPU side depends on:
//   * `InstanceVertex` is exactly 96 bytes (no compiler padding surprises)
//   * `PushConstants` is exactly 192 bytes
//   * Attribute offsets match the GLSL `layout(location=...)` assignments —
//     specifically that the 4-vec4 model matrix runs from offset 0 and that
//     `base_color` / `mr_params` follow without gaps.

#include <gtest/gtest.h>
#include <hongtu/renderer/pbr/instanced_pbr_pipeline.hpp>

#include <cstddef>

using hongtu::renderer::InstancedPbrPipeline;

TEST(InstancedPbrLayoutTest, InstanceVertexIs96Bytes) {
  EXPECT_EQ(sizeof(InstancedPbrPipeline::InstanceVertex), 96u);
}

TEST(InstancedPbrLayoutTest, PushConstantsIs192Bytes) {
  EXPECT_EQ(sizeof(InstancedPbrPipeline::PushConstants), 192u);
}

TEST(InstancedPbrLayoutTest, ModelMatrixStartsAtOffsetZero) {
  EXPECT_EQ(offsetof(InstancedPbrPipeline::InstanceVertex, model), 0u);
}

TEST(InstancedPbrLayoutTest, BaseColorImmediatelyAfterModel) {
  // 4×4 floats = 64 bytes.
  EXPECT_EQ(offsetof(InstancedPbrPipeline::InstanceVertex, base_color), 64u);
}

TEST(InstancedPbrLayoutTest, MrParamsImmediatelyAfterBaseColor) {
  EXPECT_EQ(offsetof(InstancedPbrPipeline::InstanceVertex, mr_params), 80u);
}

TEST(InstancedPbrLayoutTest, BindingDescriptionMatchesStruct) {
  auto bind = InstancedPbrPipeline::getInstanceVertexBinding();
  EXPECT_EQ(bind.binding,     1u);
  EXPECT_EQ(bind.stride,      96u);
  EXPECT_TRUE(bind.perInstance);
}

TEST(InstancedPbrLayoutTest, AttributeOffsetsMatchStructFields) {
  auto attrs = InstancedPbrPipeline::getInstanceVertexAttributes();
  ASSERT_EQ(attrs.size(), 6u);

  // model[0] at location 3, offset 0.
  EXPECT_EQ(attrs[0].location, 3u);
  EXPECT_EQ(attrs[0].offset,   0u);
  // model[1] at location 4, offset 16.
  EXPECT_EQ(attrs[1].location, 4u);
  EXPECT_EQ(attrs[1].offset,   16u);
  // model[2] at location 5, offset 32.
  EXPECT_EQ(attrs[2].location, 5u);
  EXPECT_EQ(attrs[2].offset,   32u);
  // model[3] at location 6, offset 48.
  EXPECT_EQ(attrs[3].location, 6u);
  EXPECT_EQ(attrs[3].offset,   48u);
  // base_color at location 7, offset 64.
  EXPECT_EQ(attrs[4].location, 7u);
  EXPECT_EQ(attrs[4].offset,   64u);
  // mr_params at location 8, offset 80.
  EXPECT_EQ(attrs[5].location, 8u);
  EXPECT_EQ(attrs[5].offset,   80u);

  // All bound to instance VBO (binding=1).
  for (const auto& a : attrs) EXPECT_EQ(a.binding, 1u);
}
