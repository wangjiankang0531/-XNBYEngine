#include <gtest/gtest.h>
#include <hongtu/renderer/globe/index_buffer_table.hpp>
#include <set>
#include <utility>
#include <map>

using namespace hongtu::renderer;

namespace {

// Edge of triangle (i,j) → unordered pair (i,j) with i<j.
using Edge = std::pair<uint32_t, uint32_t>;
Edge makeEdge(uint32_t a, uint32_t b) {
  return a < b ? Edge{a, b} : Edge{b, a};
}

}  // namespace

TEST(IndexBufferTable, AllSixteenTopologiesNonEmpty) {
  for (int ls : {1, 2}) for (int rs : {1, 2})
  for (int ts : {1, 2}) for (int bs : {1, 2}) {
    auto indices = generateStitchingIndices(ls, rs, ts, bs);
    EXPECT_GT(indices.size(), 0u) << "ls=" << ls << " rs=" << rs
                                  << " ts=" << ts << " bs=" << bs;
    EXPECT_EQ(indices.size() % 3u, 0u);
  }
}

TEST(IndexBufferTable, IndicesInRange) {
  // 33×33 vertex grid: valid indices 0..1088.
  for (int ls : {1, 2}) for (int rs : {1, 2})
  for (int ts : {1, 2}) for (int bs : {1, 2}) {
    auto indices = generateStitchingIndices(ls, rs, ts, bs);
    for (uint32_t i : indices) {
      EXPECT_LT(i, 33u * 33u);
    }
  }
}

TEST(IndexBufferTable, EveryEdgeSharedOrBoundary) {
  // For each topology, every edge must appear either:
  // - exactly twice (shared interior edge), OR
  // - exactly once (boundary edge along the tile perimeter).
  // Anything else is a T-junction or duplicated triangle.
  for (int ls : {1, 2}) for (int rs : {1, 2})
  for (int ts : {1, 2}) for (int bs : {1, 2}) {
    auto indices = generateStitchingIndices(ls, rs, ts, bs);
    std::map<Edge, int> edge_count;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
      uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
      ++edge_count[makeEdge(a, b)];
      ++edge_count[makeEdge(b, c)];
      ++edge_count[makeEdge(c, a)];
    }
    for (const auto& [e, n] : edge_count) {
      EXPECT_LE(n, 2) << "ls=" << ls << " rs=" << rs
                      << " ts=" << ts << " bs=" << bs
                      << " edge (" << e.first << "," << e.second
                      << ") shared " << n << " times";
      EXPECT_GE(n, 1);
    }
  }
}

TEST(IndexBufferTable, IdentityCaseHasInteriorCoverage) {
  // ls=rs=ts=bs=1 — the "all-same-LOD" case — must cover all 32×32 cells.
  auto indices = generateStitchingIndices(1, 1, 1, 1);
  // 32×32 cells × 2 triangles = 2048 triangles = 6144 indices.
  EXPECT_EQ(indices.size(), 32u * 32u * 2u * 3u);
}

TEST(IndexBufferTable, AllTrianglesHaveConsistentWinding) {
  // Renderer uses CullMode::Back + frontCounterClockwise=false (= VK_FRONT_FACE_CLOCKWISE).
  // In the row-down 2D grid coordinate system, the front face is clockwise,
  // which has NEGATIVE 2D signed area. Every triangle in every topology
  // must have signed area < 0 to be rendered (not backface-culled).
  for (int ls : {1, 2}) for (int rs : {1, 2})
  for (int ts : {1, 2}) for (int bs : {1, 2}) {
    auto idx = generateStitchingIndices(ls, rs, ts, bs);
    auto sa = [&](uint32_t a, uint32_t b, uint32_t c) {
      int ax = a % 33, ay = a / 33;
      int bx = b % 33, by = b / 33;
      int cx = c % 33, cy = c / 33;
      return (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
    };
    for (size_t i = 0; i + 2 < idx.size(); i += 3) {
      EXPECT_LT(sa(idx[i], idx[i + 1], idx[i + 2]), 0)
          << "ls=" << ls << " rs=" << rs << " ts=" << ts << " bs=" << bs
          << " tri " << (i / 3);
    }
  }
}
