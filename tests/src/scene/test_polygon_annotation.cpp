#include <gtest/gtest.h>
#include <hongtu/scene/annotation/polygon_annotation.hpp>
#include <hongtu/scene/annotation/polygon_smooth.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>

#include <nlohmann/json.hpp>

using hongtu::scene::AnnotationKind;
using hongtu::scene::AnnotationMode;
using hongtu::scene::AnnotationStyle;
using hongtu::scene::GeoPolygonAnnotation;
using hongtu::scene::LocalPolygonAnnotation;
using hongtu::coordinate::Geodetic;
using hongtu::coordinate::PrecisionOrigin;

TEST(GeoPolygonAnnotationTest, KindAndMode) {
  GeoPolygonAnnotation a;
  EXPECT_EQ(a.getKind(), AnnotationKind::Polygon);
  EXPECT_EQ(a.getMode(), AnnotationMode::Geo);
}

TEST(LocalPolygonAnnotationTest, KindAndMode) {
  LocalPolygonAnnotation a;
  EXPECT_EQ(a.getKind(), AnnotationKind::Polygon);
  EXPECT_EQ(a.getMode(), AnnotationMode::Local);
}

TEST(GeoPolygonAnnotationTest, VertexBulkSet) {
  GeoPolygonAnnotation a;
  a.setVertices({
      Geodetic::fromDegrees(39.9, 116.4),
      Geodetic::fromDegrees(39.91, 116.41),
      Geodetic::fromDegrees(39.91, 116.40),
  });
  EXPECT_EQ(a.vertexCount(), 3u);
}

TEST(LocalPolygonAnnotationTest, RtcLoweringSubtractsOrigin) {
  LocalPolygonAnnotation a;
  a.setVertices({{0,0,0}, {10,0,0}, {10,10,0}, {0,10,0}});
  PrecisionOrigin origin;
  origin.setOriginECEF({0, 0, 0});
  auto payload = a.toRenderPayload(origin);
  ASSERT_EQ(payload.rtc_vertices.size(), 4u);
  EXPECT_FLOAT_EQ(payload.rtc_vertices[2].x, 10.0f);
  EXPECT_FLOAT_EQ(payload.rtc_vertices[2].y, 10.0f);
}

TEST(GeoPolygonAnnotationTest, JsonRoundTripWithStyle) {
  GeoPolygonAnnotation a;
  a.setId(3);
  AnnotationStyle s; s.filled = true; s.color = {0.5f, 0, 0.5f, 0.7f};
  a.setStyle(s);
  a.setVertices({
      Geodetic::fromDegrees(39.9, 116.4),
      Geodetic::fromDegrees(39.91, 116.41),
      Geodetic::fromDegrees(39.91, 116.40),
  });
  auto j = a.toJson();
  GeoPolygonAnnotation b;
  b.fromJson(j);
  EXPECT_EQ(b.id(), 3u);
  EXPECT_TRUE(b.style().filled);
  EXPECT_EQ(b.vertexCount(), 3u);
}

TEST(PolygonSmoothTest, ChaikinDoublesVerticesAndRoundsInsideHull) {
  // 单位方形,1 次迭代 → 8 顶点;所有点仍在原凸包内(Chaikin 切角不外扩)。
  std::vector<glm::vec3> sq = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
  auto s1 = hongtu::scene::smoothPolygonChaikin(sq, 1);
  ASSERT_EQ(s1.size(), 8u);
  for (const auto& p : s1) {
    EXPECT_GE(p.x, 0.0f); EXPECT_LE(p.x, 1.0f);
    EXPECT_GE(p.y, 0.0f); EXPECT_LE(p.y, 1.0f);
  }
  // 角点被切掉:不再有 (0,0) 这样的原始角。
  for (const auto& p : s1) {
    EXPECT_GT(glm::length(p - glm::vec3(0, 0, 0)), 0.2f);
  }
  // 0 次迭代 / 退化环:原样返回。
  EXPECT_EQ(hongtu::scene::smoothPolygonChaikin(sq, 0).size(), 4u);
  std::vector<glm::vec3> two = {{0, 0, 0}, {1, 0, 0}};
  EXPECT_EQ(hongtu::scene::smoothPolygonChaikin(two, 3).size(), 2u);
}

TEST(PolygonSmoothTest, GeodeticOverloadPreservesUniformAlt) {
  // 水体边界:统一水位经过圆滑后必须保持不变(alt 是水位语义)。
  std::vector<Geodetic> ring = {
      Geodetic::fromDegrees(22.40, 114.00, 31.0),
      Geodetic::fromDegrees(22.40, 114.02, 31.0),
      Geodetic::fromDegrees(22.42, 114.02, 31.0),
      Geodetic::fromDegrees(22.42, 114.00, 31.0)};
  auto s2 = hongtu::scene::smoothPolygonChaikin(ring, 2);
  ASSERT_EQ(s2.size(), 16u);
  for (const auto& g : s2) EXPECT_DOUBLE_EQ(g.alt, 31.0);
}

TEST(GeoPolygonAnnotationTest, JsonRoundTripCornerSmooth) {
  GeoPolygonAnnotation a;
  AnnotationStyle s; s.corner_smooth = 3;
  a.setStyle(s);
  a.setVertices({Geodetic::fromDegrees(22.40, 114.00),
                 Geodetic::fromDegrees(22.40, 114.02),
                 Geodetic::fromDegrees(22.42, 114.02)});
  GeoPolygonAnnotation b;
  b.fromJson(a.toJson());
  EXPECT_EQ(b.style().corner_smooth, 3);
  EXPECT_EQ(AnnotationStyle{}.corner_smooth, 0);  // 默认不圆滑
}

TEST(GeoPolygonAnnotationTest, JsonRoundTripWaterFlag) {
  // 水体标志(M3.5 编辑器水体)随标注 JSON 持久化;顶点 alt 即水位。
  GeoPolygonAnnotation a;
  a.setId(7);
  AnnotationStyle s; s.water = true;
  a.setStyle(s);
  a.setVertices({
      Geodetic::fromDegrees(22.40, 114.00, 31.0),
      Geodetic::fromDegrees(22.40, 114.02, 31.0),
      Geodetic::fromDegrees(22.42, 114.02, 31.0),
  });
  auto j = a.toJson();
  GeoPolygonAnnotation b;
  b.fromJson(j);
  EXPECT_TRUE(b.style().water);
  ASSERT_EQ(b.vertexCount(), 3u);
  EXPECT_DOUBLE_EQ(b.getVertices()[0].alt, 31.0);
  // 默认不是水体 — 旧文档反序列化不会凭空多出水面。
  EXPECT_FALSE(AnnotationStyle{}.water);
}

TEST(LocalPolygonAnnotationTest, JsonRoundTrip) {
  LocalPolygonAnnotation a;
  a.setId(11);
  a.setVertices({{0,0,0}, {1,0,0}, {1,1,0}});
  auto j = a.toJson();
  LocalPolygonAnnotation b;
  b.fromJson(j);
  EXPECT_EQ(b.id(), 11u);
  EXPECT_EQ(b.vertexCount(), 3u);
  EXPECT_DOUBLE_EQ(b.getVertices()[2].y, 1.0);
}

TEST(CircleRegionTest, LocalRingRadiusCentroidAndCount) {
  using hongtu::scene::makeCircleRingLocal;
  using hongtu::scene::ringCentroidLocal;
  glm::dvec3 center(10.0, -4.0, 2.0);
  const double r = 7.5;
  auto ring = makeCircleRingLocal(center, r, 48);
  ASSERT_EQ(ring.size(), 48u);
  // 每个顶点到圆心的水平距离都等于半径,z 保持圆心高度。
  for (const auto& p : ring) {
    double d = std::sqrt((p.x - center.x) * (p.x - center.x) +
                         (p.y - center.y) * (p.y - center.y));
    EXPECT_NEAR(d, r, 1e-6);
    EXPECT_NEAR(p.z, center.z, 1e-9);
  }
  // 均匀环的形心精确等于圆心 —— 改半径据此重建。
  glm::dvec3 c = ringCentroidLocal(ring);
  EXPECT_NEAR(c.x, center.x, 1e-6);
  EXPECT_NEAR(c.y, center.y, 1e-6);
  EXPECT_NEAR(c.z, center.z, 1e-6);
}

TEST(CircleRegionTest, SegmentsClampedAndStyleSerializes) {
  using hongtu::scene::makeCircleRingLocal;
  EXPECT_EQ(makeCircleRingLocal({0,0,0}, 1.0, 2).size(), 8u);    // 下限 8
  EXPECT_EQ(makeCircleRingLocal({0,0,0}, 1.0, 999).size(), 256u);// 上限 256
  // circle / circle_radius_m 往返序列化。
  LocalPolygonAnnotation a;
  a.setVertices(makeCircleRingLocal({0,0,0}, 3.0, 48));
  a.mutableStyle().circle = true;
  a.mutableStyle().circle_radius_m = 3.0f;
  auto j = a.toJson();
  LocalPolygonAnnotation b;
  b.fromJson(j);
  EXPECT_TRUE(b.style().circle);
  EXPECT_FLOAT_EQ(b.style().circle_radius_m, 3.0f);
}
