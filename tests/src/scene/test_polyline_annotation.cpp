// Unit tests for Geo/Local PolylineAnnotation. Pure host — no Vulkan.
//
// Covers: vertex setters, RTC lowering correctness, payload assembly, JSON
// round-trip including style + base fields. The same template applies to the
// other 4 annotation pairs (polygon / symbol / text / arrow).

#include <gtest/gtest.h>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/scene/annotation/polygon_smooth.hpp>
#include <hongtu/scene/annotation/polyline_annotation.hpp>
#include <nlohmann/json.hpp>

#include <numbers>

using hongtu::coordinate::Geodetic;
using hongtu::coordinate::geodeticToECEF;
using hongtu::coordinate::PrecisionOrigin;
using hongtu::scene::AnnotationKind;
using hongtu::scene::AnnotationMode;
using hongtu::scene::AnnotationStyle;
using hongtu::scene::GeoPolylineAnnotation;
using hongtu::scene::LocalPolylineAnnotation;

namespace {
  constexpr float kEps = 1e-3f;
}

// =============================================================================
// Construction + identity
// =============================================================================

TEST(GeoPolylineAnnotationTest, KindAndMode) {
  GeoPolylineAnnotation a;
  EXPECT_EQ(a.getKind(), AnnotationKind::Polyline);
  EXPECT_EQ(a.getMode(), AnnotationMode::Geo);
}

TEST(LocalPolylineAnnotationTest, KindAndMode) {
  LocalPolylineAnnotation a;
  EXPECT_EQ(a.getKind(), AnnotationKind::Polyline);
  EXPECT_EQ(a.getMode(), AnnotationMode::Local);
}

// =============================================================================
// Vertex management
// =============================================================================

TEST(GeoPolylineAnnotationTest, AddAndClearVertices) {
  GeoPolylineAnnotation a;
  a.addVertex(Geodetic::fromDegrees(39.9, 116.4));
  a.addVertex(Geodetic::fromDegrees(39.91, 116.41));
  EXPECT_EQ(a.vertexCount(), 2u);
  a.clearVertices();
  EXPECT_EQ(a.vertexCount(), 0u);
}

TEST(LocalPolylineAnnotationTest, SetVerticesBulk) {
  LocalPolylineAnnotation a;
  a.setVertices({{0, 0, 0}, {1, 2, 3}, {4, 5, 6}});
  EXPECT_EQ(a.vertexCount(), 3u);
  EXPECT_DOUBLE_EQ(a.getVertices()[1].y, 2.0);
}

// =============================================================================
// RTC lowering + payload
// =============================================================================

TEST(GeoPolylineAnnotationTest, RtcLoweringRoundTripsThroughOrigin) {
  GeoPolylineAnnotation a;
  Geodetic anchor = Geodetic::fromDegrees(39.9042, 116.4074);
  a.addVertex(anchor);

  PrecisionOrigin origin;
  origin.setOriginGeodetic(anchor);

  auto payload = a.toRenderPayload(origin);
  ASSERT_EQ(payload.rtc_vertices.size(), 1u);
  // Anchor vertex == origin → RTC vec3 is (0,0,0)
  EXPECT_NEAR(payload.rtc_vertices[0].x, 0.0f, kEps);
  EXPECT_NEAR(payload.rtc_vertices[0].y, 0.0f, kEps);
  EXPECT_NEAR(payload.rtc_vertices[0].z, 0.0f, kEps);
}

TEST(LocalPolylineAnnotationTest, RtcLoweringSubtractsOrigin) {
  LocalPolylineAnnotation a;
  a.setVertices({{10.0, 20.0, 5.0}, {12.0, 21.0, 5.0}});

  PrecisionOrigin origin;
  origin.setOriginECEF({10.0, 20.0, 5.0});

  auto payload = a.toRenderPayload(origin);
  ASSERT_EQ(payload.rtc_vertices.size(), 2u);
  EXPECT_NEAR(payload.rtc_vertices[0].x, 0.0f, kEps);
  EXPECT_NEAR(payload.rtc_vertices[1].x, 2.0f, kEps);
  EXPECT_NEAR(payload.rtc_vertices[1].y, 1.0f, kEps);
}

TEST(GeoPolylineAnnotationTest, PayloadCarriesStyleAndId) {
  GeoPolylineAnnotation a;
  a.setId(42);
  AnnotationStyle s;
  s.color = {1, 0, 0, 1};
  s.stroke_width_m = 0.7f;
  a.setStyle(s);

  PrecisionOrigin origin;
  auto p = a.toRenderPayload(origin);
  EXPECT_EQ(p.id, 42u);
  EXPECT_FLOAT_EQ(p.style.stroke_width_m, 0.7f);
  EXPECT_FLOAT_EQ(p.style.color.r, 1.0f);
}

// =============================================================================
// JSON round-trip
// =============================================================================

TEST(GeoPolylineAnnotationTest, JsonRoundTripPreservesVerticesAndStyle) {
  GeoPolylineAnnotation a;
  a.setId(7);
  a.setVisible(false);
  AnnotationStyle s;
  s.color = {0.2f, 0.4f, 0.6f, 0.8f};
  s.dash_period_m = 1.5f;
  a.setStyle(s);
  a.addVertex(Geodetic::fromDegrees(39.9, 116.4, 10.0));
  a.addVertex(Geodetic::fromDegrees(39.95, 116.45, 12.0));

  auto j = a.toJson();
  EXPECT_EQ(j["kind"], "polyline");
  EXPECT_EQ(j["mode"], "geo");

  GeoPolylineAnnotation b;
  b.fromJson(j);
  EXPECT_EQ(b.id(), 7u);
  EXPECT_FALSE(b.visible());
  EXPECT_FLOAT_EQ(b.style().dash_period_m, 1.5f);
  EXPECT_EQ(b.vertexCount(), 2u);
  // Geodetic lat back to degrees ≈ 39.9
  constexpr double kRadToDeg = 180.0 / std::numbers::pi_v<double>;
  EXPECT_NEAR(b.getVertices()[0].lat * kRadToDeg, 39.9, 1e-6);
  EXPECT_NEAR(b.getVertices()[0].alt, 10.0, 1e-6);
}

TEST(LocalPolylineAnnotationTest, JsonRoundTrip) {
  LocalPolylineAnnotation a;
  a.setId(13);
  a.setVertices({{1, 2, 3}, {4, 5, 6}});

  auto j = a.toJson();
  EXPECT_EQ(j["mode"], "local");

  LocalPolylineAnnotation b;
  b.fromJson(j);
  EXPECT_EQ(b.id(), 13u);
  EXPECT_EQ(b.vertexCount(), 2u);
  EXPECT_DOUBLE_EQ(b.getVertices()[1].z, 6.0);
}

TEST(LocalPolylineAnnotationTest, JsonOmitsDefaultStyleFields) {
  // Default-styled annotation → JSON shouldn't carry a heavy style block
  // (compact schema). Style key should be missing or empty.
  LocalPolylineAnnotation a;
  a.setVertices({{0, 0, 0}, {1, 1, 1}});
  auto j = a.toJson();
  EXPECT_TRUE(!j.contains("style") || j["style"].empty());
}

// =============================================================================
// 线类型扩展(P5 线要素系统)
// =============================================================================

TEST(PolylineLineStyleTest, JsonRoundTripPreservesLineTypeAndParams) {
  using hongtu::scene::LinePlacement;
  using hongtu::scene::LineType;
  LocalPolylineAnnotation a;
  a.setVertices({{0, 0, 0}, {1, 1, 1}});
  AnnotationStyle s;
  s.line_type = LineType::Flow;
  s.line_placement = LinePlacement::Ground;
  s.glow_power = 0.6f;
  s.flow_speed = 3.5f;
  s.line_tex_tiling = 0.8f;
  s.arc_height_ratio = 0.5f;
  s.outline_color = {0.1f, 0.2f, 0.3f, 1.0f};
  s.line_texture = "flow_arrow";
  a.setStyle(s);

  LocalPolylineAnnotation b;
  b.fromJson(a.toJson());
  const auto& bs = b.style();
  EXPECT_EQ(bs.line_type, LineType::Flow);
  EXPECT_EQ(bs.line_placement, LinePlacement::Ground);
  EXPECT_FLOAT_EQ(bs.glow_power, 0.6f);
  EXPECT_FLOAT_EQ(bs.flow_speed, 3.5f);
  EXPECT_FLOAT_EQ(bs.line_tex_tiling, 0.8f);
  EXPECT_FLOAT_EQ(bs.arc_height_ratio, 0.5f);
  EXPECT_FLOAT_EQ(bs.outline_color.b, 0.3f);
  EXPECT_EQ(bs.line_texture, "flow_arrow");
}

TEST(PolylineSmoothTest, OpenChaikinKeepsEndpointsAndRoundsInterior) {
  using hongtu::scene::smoothPolylineChaikin;
  // 直角折线:端点不动,点数增长,拐角离原角点越来越远(被削圆)。
  std::vector<glm::vec3> raw = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}};
  EXPECT_EQ(smoothPolylineChaikin(raw, 0).size(), raw.size()); // 0=原样

  float prev = 1e9f;
  for (int it = 1; it <= 3; ++it) {
    auto s = smoothPolylineChaikin(raw, it);
    EXPECT_GT(s.size(), raw.size());
    EXPECT_EQ(s.front(), raw.front()); // 端点保持
    EXPECT_EQ(s.back(), raw.back());
    float md = 1e9f;
    for (auto& q : s)
      md = std::min(md, glm::length(q - raw[1]));
    EXPECT_LE(md, prev); // 越迭代离角点越远
    EXPECT_GT(md, 1.5f);
    prev = md;
  }
  // < 3 点原样返回。
  std::vector<glm::vec3> two = {{0, 0, 0}, {1, 1, 1}};
  EXPECT_EQ(smoothPolylineChaikin(two, 3).size(), 2u);
}
