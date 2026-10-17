// Layout asserts for SkinnedMeshVertex + SkinnedPbrPipeline::PushConstants.
// Pure host — no Vulkan needed.

#include <gtest/gtest.h>
#include <hongtu/renderer/pbr/skinned_pbr_pipeline.hpp>

using hongtu::renderer::SkinnedMeshVertex;
using hongtu::renderer::SkinnedPbrPipeline;

TEST(SkinnedPbrLayoutTest, SkinnedMeshVertexIs80Bytes) {
  EXPECT_EQ(sizeof(SkinnedMeshVertex), 80u);
}

TEST(SkinnedPbrLayoutTest, SkinnedMeshVertexFieldOffsets) {
  EXPECT_EQ(offsetof(SkinnedMeshVertex, position),      0u);
  EXPECT_EQ(offsetof(SkinnedMeshVertex, normal),       12u);
  EXPECT_EQ(offsetof(SkinnedMeshVertex, uv),           24u);
  EXPECT_EQ(offsetof(SkinnedMeshVertex, tangent),      32u);
  EXPECT_EQ(offsetof(SkinnedMeshVertex, joint_indices),48u);
  EXPECT_EQ(offsetof(SkinnedMeshVertex, joint_weights),64u);
}

TEST(SkinnedPbrLayoutTest, PushConstantsUnder256Bytes) {
  // MoltenVK limit on Apple is 256B — explicit guardrail.
  EXPECT_LE(sizeof(SkinnedPbrPipeline::PushConstants), 256u);
  EXPECT_EQ(sizeof(SkinnedPbrPipeline::PushConstants), 176u);
}
