#include <gtest/gtest.h>
#include <hongtu/scene/annotation/text_annotation.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>

#include <nlohmann/json.hpp>

using hongtu::scene::AnnotationKind;
using hongtu::scene::AnnotationMode;
using hongtu::scene::AnnotationStyle;
using hongtu::scene::GeoTextAnnotation;
using hongtu::scene::LocalTextAnnotation;
using hongtu::coordinate::Geodetic;
using hongtu::coordinate::PrecisionOrigin;

TEST(GeoTextAnnotationTest, KindAndMode) {
  GeoTextAnnotation a;
  EXPECT_EQ(a.getKind(), AnnotationKind::Text);
  EXPECT_EQ(a.getMode(), AnnotationMode::Geo);
}

TEST(LocalTextAnnotationTest, KindAndMode) {
  LocalTextAnnotation a;
  EXPECT_EQ(a.getKind(), AnnotationKind::Text);
  EXPECT_EQ(a.getMode(), AnnotationMode::Local);
}

TEST(GeoTextAnnotationTest, PayloadAtAnchor) {
  GeoTextAnnotation a;
  Geodetic anchor = Geodetic::fromDegrees(39.9, 116.4);
  a.setAnchor(anchor);
  PrecisionOrigin origin; origin.setOriginGeodetic(anchor);
  auto p = a.toRenderPayload(origin);
  ASSERT_EQ(p.rtc_vertices.size(), 1u);
  EXPECT_NEAR(glm::length(p.rtc_vertices[0]), 0.0f, 1e-3f);
}

TEST(LocalTextAnnotationTest, JsonRoundTripWithText) {
  LocalTextAnnotation a;
  a.setAnchor({1, 2, 3});
  AnnotationStyle s; s.text = "EXIT"; s.text_size_m = 2.5f;
  a.setStyle(s);
  auto j = a.toJson();
  LocalTextAnnotation b; b.fromJson(j);
  EXPECT_EQ(b.style().text, "EXIT");
  EXPECT_FLOAT_EQ(b.style().text_size_m, 2.5f);
  EXPECT_DOUBLE_EQ(b.getAnchor().y, 2.0);
}

TEST(GeoTextAnnotationTest, JsonRoundTripWithText) {
  GeoTextAnnotation a;
  a.setAnchor(Geodetic::fromDegrees(39.9, 116.4));
  AnnotationStyle s; s.text = "Sensor A"; a.setStyle(s);
  auto j = a.toJson();
  GeoTextAnnotation b; b.fromJson(j);
  EXPECT_EQ(b.style().text, "Sensor A");
}
