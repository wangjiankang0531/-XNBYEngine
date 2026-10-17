#include <gtest/gtest.h>
#include <hongtu/asset/indexed_mesh_compaction.hpp>

using hongtu::asset::compactMeshIndices;
using hongtu::asset::MeshIndexRange;

TEST(IndexedMeshCompaction, ParentDrawRangesPreserveSkyAndUnloadedRoots) {
  const auto draws = hongtu::asset::retainedMeshIndexRanges(30, std::vector<MeshIndexRange>{{9, 6}, {21, 3}});
  ASSERT_EQ(draws.size(), 3);
  EXPECT_EQ(draws[0].first, 0);
  EXPECT_EQ(draws[0].count, 9);
  EXPECT_EQ(draws[1].first, 15);
  EXPECT_EQ(draws[1].count, 6);
  EXPECT_EQ(draws[2].first, 24);
  EXPECT_EQ(draws[2].count, 6);
  const auto fallback = hongtu::asset::retainedMeshIndexRanges(30, {});
  ASSERT_EQ(fallback.size(), 1);
  EXPECT_EQ(fallback[0].count, 30);
  EXPECT_TRUE(hongtu::asset::retainedMeshIndexRanges(30, std::vector<MeshIndexRange>{{0, 30}}).empty());
}

TEST(IndexedMeshCompaction, ParentDrawRangesRejectInvalidReplacementSpans) {
  using hongtu::asset::retainedMeshIndexRanges;
  EXPECT_THROW(retainedMeshIndexRanges(9, std::vector<MeshIndexRange>{{3, 6}, {6, 3}}), std::invalid_argument);
  EXPECT_THROW(retainedMeshIndexRanges(9, std::vector<MeshIndexRange>{{6, 6}}), std::invalid_argument);
  EXPECT_THROW(retainedMeshIndexRanges(9, std::vector<MeshIndexRange>{{1, 3}}), std::invalid_argument);
  EXPECT_THROW(retainedMeshIndexRanges(8, {}), std::invalid_argument);
}

TEST(IndexedMeshCompaction, RemovesOnlyUnreferencedVerticesAndPreservesTriangleOrder) {
  const std::vector<uint32_t> indices = {4, 1, 7, 2, 3, 6, 7, 1, 8};
  const std::vector<MeshIndexRange> removed = {{3, 3}};
  const auto result = compactMeshIndices(10, indices, removed);
  EXPECT_EQ(result.source_vertex_indices, (std::vector<uint32_t>{4, 1, 7, 8}));
  EXPECT_EQ(result.indices, (std::vector<uint32_t>{0, 1, 2, 2, 1, 3}));
  std::vector<uint32_t> reconstructed;
  for (auto index : result.indices)
    reconstructed.push_back(result.source_vertex_indices[index]);
  EXPECT_EQ(reconstructed, (std::vector<uint32_t>{4, 1, 7, 7, 1, 8}));
}

TEST(IndexedMeshCompaction, KeepsDistinctSourceVerticesAtAttributeSeams) {
  const std::vector<uint32_t> indices = {0, 1, 2, 3, 4, 5};
  const auto result = compactMeshIndices(6, indices, {});
  EXPECT_EQ(result.source_vertex_indices, indices);
  EXPECT_EQ(result.indices, indices);
}

TEST(IndexedMeshCompaction, HandlesMultipleRangesAndFullyRemovedGeometry) {
  const std::vector<uint32_t> indices = {0, 1, 2, 3, 4, 5, 6, 7, 8};
  auto result = compactMeshIndices(9, indices, std::vector<MeshIndexRange>{{0, 3}, {6, 3}});
  EXPECT_EQ(result.source_vertex_indices, (std::vector<uint32_t>{3, 4, 5}));
  result = compactMeshIndices(9, indices, std::vector<MeshIndexRange>{{0, 9}});
  EXPECT_TRUE(result.indices.empty());
  EXPECT_TRUE(result.source_vertex_indices.empty());
}

TEST(IndexedMeshCompaction, RejectsMalformedRangesAndRetainedIndices) {
  const std::vector<uint32_t> indices = {0, 1, 2, 3, 4, 5};
  EXPECT_THROW(compactMeshIndices(6, indices, std::vector<MeshIndexRange>{{1, 3}}), std::invalid_argument);
  EXPECT_THROW(compactMeshIndices(6, indices, std::vector<MeshIndexRange>{{0, 4}}), std::invalid_argument);
  EXPECT_THROW(compactMeshIndices(6, indices, std::vector<MeshIndexRange>{{3, 3}, {0, 3}}), std::invalid_argument);
  EXPECT_THROW(compactMeshIndices(6, indices, std::vector<MeshIndexRange>{{0, 6}, {3, 3}}), std::invalid_argument);
  EXPECT_THROW(compactMeshIndices(6, indices, std::vector<MeshIndexRange>{{3, 6}}), std::invalid_argument);
  EXPECT_THROW(compactMeshIndices(5, indices, {}), std::invalid_argument);
  EXPECT_THROW(compactMeshIndices(6, std::vector<uint32_t>{0, 1}, {}), std::invalid_argument);
}
