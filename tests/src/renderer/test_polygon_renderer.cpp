// Pure unit tests for the fan-triangulation math. Pipeline correctness is
// verified by the screenshot regression in scenario_demo (later phase 3 task).

#include <gtest/gtest.h>
#include <hongtu/renderer/effects/polygon_renderer.hpp>

using hongtu::renderer::PolygonVertex;
using hongtu::renderer::triangulateFan;

TEST(TriangulateFanTest, EmptyForLessThanThreePoints) {
  EXPECT_TRUE(triangulateFan({}, {1,1,1,1}).empty());
  EXPECT_TRUE(triangulateFan({{0,0,0}}, {1,1,1,1}).empty());
  EXPECT_TRUE(triangulateFan({{0,0,0}, {1,0,0}}, {1,1,1,1}).empty());
}

TEST(TriangulateFanTest, TriangleEmitsThreeVertices) {
  auto out = triangulateFan({{0,0,0}, {1,0,0}, {0,1,0}}, {1,1,1,1});
  ASSERT_EQ(out.size(), 3u);
  EXPECT_FLOAT_EQ(out[0].position.x, 0.0f);
  EXPECT_FLOAT_EQ(out[1].position.x, 1.0f);
  EXPECT_FLOAT_EQ(out[2].position.y, 1.0f);
}

TEST(TriangulateFanTest, QuadEmitsTwoTriangles) {
  // Square (0,0) (1,0) (1,1) (0,1) → triangles (0,1,2) (0,2,3)
  auto out = triangulateFan({{0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}}, {0.5f,0.5f,0.5f,1});
  ASSERT_EQ(out.size(), 6u);     // 2 triangles × 3 verts
  // First triangle starts at points[0]
  EXPECT_FLOAT_EQ(out[0].position.x, 0.0f);
  EXPECT_FLOAT_EQ(out[0].position.y, 0.0f);
  // Second triangle also starts at points[0] (fan)
  EXPECT_FLOAT_EQ(out[3].position.x, 0.0f);
  EXPECT_FLOAT_EQ(out[3].position.y, 0.0f);
  // Color propagated
  EXPECT_FLOAT_EQ(out[2].color.r, 0.5f);
}

TEST(TriangulateFanTest, PentagonEmitsThreeTriangles) {
  std::vector<glm::vec3> pts = {
      {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0.5f, 1.5f, 0}, {0, 1, 0}
  };
  auto out = triangulateFan(pts, {1,1,1,1});
  EXPECT_EQ(out.size(), 9u);    // 3 triangles × 3 verts
}
