#include <gtest/gtest.h>
#include <hongtu/renderer/picking/triangle_bvh.hpp>

#include <array>
#include <random>

using namespace hongtu::renderer;

TEST(TriangleBvh, NearestSurfaceAndBackFace) {
  TriangleBvh mesh(2);
  mesh.addTriangle({0, 0, 0}, {2, 0, 0}, {0, 2, 0});
  mesh.addTriangle({0, 0, 5}, {2, 0, 5}, {0, 2, 5});
  mesh.build();
  auto hit = mesh.intersect({{.5, .5, 10}, {0, 0, -1}});
  ASSERT_TRUE(hit);
  EXPECT_DOUBLE_EQ(hit->t, 5);
  EXPECT_DOUBLE_EQ(hit->point.z, 5);
  hit = mesh.intersect({{.5, .5, -1}, {0, 0, 1}});
  ASSERT_TRUE(hit);
  EXPECT_DOUBLE_EQ(hit->t, 1);
  EXPECT_FALSE(mesh.intersect({{.5, .5, 10}, {0, 0, -1}}, 4.99));
  EXPECT_FALSE(mesh.intersect({{.5, .5, 10}, {0, 0, 1}}));
}

TEST(TriangleBvh, ParallelAxesThinFacesAndEmptyMeshes) {
  TriangleBvh empty;
  empty.build();
  EXPECT_FALSE(empty.intersect({{0, 0, 1}, {0, 0, -1}}));
  TriangleBvh mesh;
  mesh.addTriangle({0, 0, 0}, {1, 0, 0}, {0, .0001f, 0});
  mesh.addTriangle({0, 0, 2}, {0, 0, 2}, {0, 0, 2});
  mesh.build();
  EXPECT_EQ(mesh.triangleCount(), 2u);
  EXPECT_TRUE(mesh.intersect({{.1, .00001, 1}, {0, 0, -1}}));
  EXPECT_FALSE(mesh.intersect({{2, 0, 1}, {0, 0, -1}}));
  EXPECT_FALSE(mesh.intersect({{.1, .00001, 1}, {1, 0, 0}}));
  EXPECT_FALSE(mesh.intersect({{0, 0, 1}, {0, 0, 0}}));
  EXPECT_FALSE(mesh.intersect({{std::numeric_limits<double>::quiet_NaN(), 0, 1}, {0, 0, -1}}));
}

TEST(TriangleBvh, BuildOwnershipAndInvalidInput) {
  TriangleBvh mesh;
  EXPECT_THROW(mesh.intersect({{0, 0, 1}, {0, 0, -1}}), std::logic_error);
  EXPECT_THROW(mesh.addTriangle({0, 0, std::numeric_limits<float>::infinity()}, {1, 0, 0}, {0, 1, 0}),
               std::invalid_argument);
  EXPECT_EQ(mesh.triangleCount(), 0u);
  mesh.addTriangle({0, 0, 0}, {1, 0, 0}, {0, 1, 0});
  mesh.build();
  EXPECT_THROW(mesh.addTriangle({0, 0, 1}, {1, 0, 1}, {0, 1, 1}), std::logic_error);
  EXPECT_THROW(mesh.build(), std::logic_error);
}

TEST(TriangleBvh, IndependentMeshReferenceAndSpatialPruning) {
  std::vector<glm::vec3> positions;
  std::vector<uint32_t> indices;
  TriangleBvh mesh;
  for (int y = 0; y < 32; ++y)
    for (int x = 0; x < 32; ++x) {
      const float z = std::sin(float(x + y));
      const glm::vec3 a{x, y, z}, b{x + 1, y, z}, c{x, y + 1, z}, d{x + 1, y + 1, z};
      for (auto triangle : {std::array{a, b, c}, std::array{c, b, d}}) {
        for (auto p : triangle) {
          indices.push_back(static_cast<uint32_t>(positions.size()));
          positions.push_back(p);
        }
        mesh.addTriangle(triangle[0], triangle[1], triangle[2]);
      }
    }
  mesh.build();
  std::mt19937 generator(804);
  std::uniform_real_distribution<double> coord(-2, 34);
  for (int i = 0; i < 250; ++i) {
    WorldRay ray{{coord(generator), coord(generator), 20}, glm::normalize(glm::dvec3(.02, -.01, -1))};
    auto expected = intersectRayMesh(ray, positions, indices, glm::dmat4(1));
    TriangleBvh::QueryStats stats;
    auto actual = mesh.intersect(ray, std::numeric_limits<double>::infinity(), &stats);
    ASSERT_EQ(bool(actual), bool(expected));
    if (actual)
      EXPECT_NEAR(actual->t, *expected, 1e-10);
    EXPECT_LT(stats.triangleTests, 64u);
  }
  EXPECT_EQ(mesh.triangleCount(), indices.size() / 3);
}
