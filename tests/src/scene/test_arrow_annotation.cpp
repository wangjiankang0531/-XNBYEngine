#include <gtest/gtest.h>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/scene/annotation/arrow_annotation.hpp>
#include <nlohmann/json.hpp>

#include <numbers>

using hongtu::coordinate::Geodetic;
using hongtu::coordinate::PrecisionOrigin;
using hongtu::scene::AnnotationKind;
using hongtu::scene::AnnotationMode;
using hongtu::scene::AnnotationStyle;
using hongtu::scene::GeoArrowAnnotation;
using hongtu::scene::LocalArrowAnnotation;

TEST(GeoArrowAnnotationTest, KindAndMode) {
  GeoArrowAnnotation a;
  EXPECT_EQ(a.getKind(), AnnotationKind::Arrow);
  EXPECT_EQ(a.getMode(), AnnotationMode::Geo);
}

TEST(LocalArrowAnnotationTest, KindAndMode) {
  LocalArrowAnnotation a;
  EXPECT_EQ(a.getKind(), AnnotationKind::Arrow);
  EXPECT_EQ(a.getMode(), AnnotationMode::Local);
}

TEST(LocalArrowAnnotationTest, PayloadHasTwoEndpoints) {
  LocalArrowAnnotation a;
  a.setEndpoints({0, 0, 0}, {10, 5, 0});
  PrecisionOrigin origin;
  origin.setOriginECEF({0, 0, 0});
  auto p = a.toRenderPayload(origin);
  ASSERT_EQ(p.rtc_vertices.size(), 2u);
  EXPECT_FLOAT_EQ(p.rtc_vertices[0].x, 0.0f);
  EXPECT_FLOAT_EQ(p.rtc_vertices[1].x, 10.0f);
}

TEST(GeoArrowAnnotationTest, RtcEndpointsAtAnchorAreZero) {
  GeoArrowAnnotation a;
  Geodetic start = Geodetic::fromDegrees(39.9, 116.4);
  Geodetic end = Geodetic::fromDegrees(39.91, 116.41);
  a.setEndpoints(start, end);
  PrecisionOrigin origin;
  origin.setOriginGeodetic(start);
  auto p = a.toRenderPayload(origin);
  ASSERT_EQ(p.rtc_vertices.size(), 2u);
  EXPECT_NEAR(glm::length(p.rtc_vertices[0]), 0.0f, 1e-3f);
  EXPECT_GT(glm::length(p.rtc_vertices[1]), 100.0f); // ~1km apart
}

TEST(LocalArrowAnnotationTest, JsonRoundTrip) {
  LocalArrowAnnotation a;
  a.setEndpoints({1, 2, 3}, {4, 5, 6});
  AnnotationStyle s;
  s.color = {1, 0.5f, 0, 1};
  s.stroke_width_m = 0.4f;
  a.setStyle(s);
  auto j = a.toJson();
  LocalArrowAnnotation b;
  b.fromJson(j);
  EXPECT_DOUBLE_EQ(b.getStart().x, 1.0);
  EXPECT_DOUBLE_EQ(b.getEnd().z, 6.0);
  EXPECT_FLOAT_EQ(b.style().stroke_width_m, 0.4f);
}

TEST(GeoArrowAnnotationTest, JsonRoundTrip) {
  GeoArrowAnnotation a;
  a.setId(5);
  a.setEndpoints(Geodetic::fromDegrees(39.9, 116.4), Geodetic::fromDegrees(39.91, 116.41));
  auto j = a.toJson();
  GeoArrowAnnotation b;
  b.fromJson(j);
  EXPECT_EQ(b.id(), 5u);
  constexpr double kRadToDeg = 180.0 / std::numbers::pi_v<double>;
  EXPECT_NEAR(b.getEnd().lat * kRadToDeg, 39.91, 1e-6);
}
