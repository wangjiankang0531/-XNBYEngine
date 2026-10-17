#include <gtest/gtest.h>
#include <hongtu/scene/annotation/symbol_annotation.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>

#include <nlohmann/json.hpp>

using hongtu::scene::AnnotationKind;
using hongtu::scene::AnnotationMode;
using hongtu::scene::AnnotationStyle;
using hongtu::scene::GeoSymbolAnnotation;
using hongtu::scene::LocalSymbolAnnotation;
using hongtu::coordinate::Geodetic;
using hongtu::coordinate::PrecisionOrigin;

TEST(GeoSymbolAnnotationTest, KindAndMode) {
  GeoSymbolAnnotation a;
  EXPECT_EQ(a.getKind(), AnnotationKind::Symbol);
  EXPECT_EQ(a.getMode(), AnnotationMode::Geo);
}

TEST(LocalSymbolAnnotationTest, KindAndMode) {
  LocalSymbolAnnotation a;
  EXPECT_EQ(a.getKind(), AnnotationKind::Symbol);
  EXPECT_EQ(a.getMode(), AnnotationMode::Local);
}

TEST(GeoSymbolAnnotationTest, RtcAnchorAtOriginIsZero) {
  GeoSymbolAnnotation a;
  Geodetic anchor = Geodetic::fromDegrees(39.9, 116.4);
  a.setAnchor(anchor);
  PrecisionOrigin origin; origin.setOriginGeodetic(anchor);
  auto p = a.toRenderPayload(origin);
  ASSERT_EQ(p.rtc_vertices.size(), 1u);
  EXPECT_NEAR(glm::length(p.rtc_vertices[0]), 0.0f, 1e-3f);
}

TEST(LocalSymbolAnnotationTest, PayloadHasSingleAnchor) {
  LocalSymbolAnnotation a;
  a.setAnchor({5, 10, 2});
  PrecisionOrigin origin; origin.setOriginECEF({0, 0, 0});
  auto p = a.toRenderPayload(origin);
  ASSERT_EQ(p.rtc_vertices.size(), 1u);
  EXPECT_FLOAT_EQ(p.rtc_vertices[0].x, 5.0f);
}

TEST(GeoSymbolAnnotationTest, JsonRoundTrip) {
  GeoSymbolAnnotation a;
  a.setAnchor(Geodetic::fromDegrees(39.9, 116.4, 5.0));
  AnnotationStyle s; s.symbol_index = 7; s.symbol_size_m = 3.5f;
  a.setStyle(s);
  auto j = a.toJson();
  GeoSymbolAnnotation b; b.fromJson(j);
  EXPECT_EQ(b.style().symbol_index, 7u);
  EXPECT_FLOAT_EQ(b.style().symbol_size_m, 3.5f);
  EXPECT_NEAR(b.getAnchor().alt, 5.0, 1e-6);
}

TEST(LocalSymbolAnnotationTest, JsonRoundTrip) {
  LocalSymbolAnnotation a;
  a.setAnchor({7, 8, 9});
  a.setId(99);
  auto j = a.toJson();
  LocalSymbolAnnotation b; b.fromJson(j);
  EXPECT_EQ(b.id(), 99u);
  EXPECT_DOUBLE_EQ(b.getAnchor().z, 9.0);
}
