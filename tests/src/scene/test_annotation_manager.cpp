// AnnotationManager unit tests. Pure host — no Vulkan.

#include <gtest/gtest.h>
#include <hongtu/scene/annotation/annotation_manager.hpp>
#include <hongtu/scene/annotation/polyline_annotation.hpp>
#include <hongtu/scene/annotation/symbol_annotation.hpp>
#include <hongtu/scene/annotation/text_annotation.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>

#include <nlohmann/json.hpp>

using hongtu::scene::AnnotationManager;
using hongtu::scene::GeoPolylineAnnotation;
using hongtu::scene::LocalPolylineAnnotation;
using hongtu::scene::LocalSymbolAnnotation;
using hongtu::scene::LocalTextAnnotation;
using hongtu::scene::AnnotationStyle;
using hongtu::coordinate::Geodetic;
using hongtu::coordinate::PrecisionOrigin;

TEST(AnnotationManagerTest, EmptyByDefault) {
  AnnotationManager m;
  EXPECT_TRUE(m.empty());
  EXPECT_EQ(m.size(), 0u);
}

TEST(AnnotationManagerTest, AddAssignsIdAndStores) {
  AnnotationManager m;
  auto a = std::make_unique<LocalPolylineAnnotation>();
  a->setVertices({{0,0,0}, {1,1,1}});
  uint64_t id = m.add(std::move(a));
  EXPECT_EQ(id, 1u);
  EXPECT_EQ(m.size(), 1u);
  EXPECT_NE(m.get(id), nullptr);
}

TEST(AnnotationManagerTest, AddRespectsPreassignedId) {
  AnnotationManager m;
  auto a = std::make_unique<LocalPolylineAnnotation>();
  a->setId(100);
  m.add(std::move(a));
  // Next auto-assigned id should be > 100, not collide
  auto b = std::make_unique<LocalPolylineAnnotation>();
  uint64_t bid = m.add(std::move(b));
  EXPECT_GT(bid, 100u);
}

TEST(AnnotationManagerTest, RemoveDecreasesSize) {
  AnnotationManager m;
  uint64_t id = m.add(std::make_unique<LocalPolylineAnnotation>());
  EXPECT_TRUE(m.remove(id));
  EXPECT_EQ(m.size(), 0u);
  EXPECT_FALSE(m.remove(id));
}

TEST(AnnotationManagerTest, ClearWipesAndResetsIdCounter) {
  AnnotationManager m;
  m.add(std::make_unique<LocalPolylineAnnotation>());
  m.add(std::make_unique<LocalPolylineAnnotation>());
  m.clear();
  EXPECT_EQ(m.size(), 0u);
  uint64_t id = m.add(std::make_unique<LocalPolylineAnnotation>());
  EXPECT_EQ(id, 1u);  // counter reset
}

TEST(AnnotationManagerTest, JsonRoundTripMixedKinds) {
  AnnotationManager m;
  // polyline (local)
  auto p = std::make_unique<LocalPolylineAnnotation>();
  p->setVertices({{0,0,0}, {10,0,0}, {10,10,0}});
  AnnotationStyle ps; ps.color = {1, 0.9f, 0, 1}; ps.stroke_width_m = 0.3f;
  p->setStyle(ps);
  m.add(std::move(p));
  // symbol (local)
  auto s = std::make_unique<LocalSymbolAnnotation>();
  s->setAnchor({5, 5, 0});
  AnnotationStyle ss; ss.symbol_index = 3;
  s->setStyle(ss);
  m.add(std::move(s));
  // text (local)
  auto t = std::make_unique<LocalTextAnnotation>();
  t->setAnchor({5, 5, 2});
  AnnotationStyle ts; ts.text = "EXIT"; ts.text_size_m = 1.5f;
  t->setStyle(ts);
  m.add(std::move(t));

  auto j = m.toJson();
  EXPECT_EQ(j["version"], 1);
  EXPECT_EQ(j["annotations"].size(), 3u);

  AnnotationManager m2;
  EXPECT_TRUE(m2.fromJson(j));
  EXPECT_EQ(m2.size(), 3u);
}

TEST(AnnotationManagerTest, JsonRoundTripPreservesIds) {
  AnnotationManager m;
  m.add(std::make_unique<LocalPolylineAnnotation>());
  m.add(std::make_unique<LocalPolylineAnnotation>());
  m.add(std::make_unique<LocalPolylineAnnotation>());
  auto j = m.toJson();

  AnnotationManager m2;
  ASSERT_TRUE(m2.fromJson(j));
  // ids 1, 2, 3 should all be present
  EXPECT_NE(m2.get(1u), nullptr);
  EXPECT_NE(m2.get(2u), nullptr);
  EXPECT_NE(m2.get(3u), nullptr);
}

TEST(AnnotationManagerTest, BuildRenderPayloadsSkipsInvisible) {
  AnnotationManager m;
  auto a = std::make_unique<LocalPolylineAnnotation>();
  a->setVertices({{0,0,0}, {1,1,1}});
  uint64_t id_a = m.add(std::move(a));
  auto b = std::make_unique<LocalPolylineAnnotation>();
  b->setVertices({{0,0,0}, {1,1,1}});
  m.add(std::move(b));
  m.get(id_a)->setVisible(false);

  PrecisionOrigin origin;
  auto payloads = m.buildRenderPayloads(origin);
  EXPECT_EQ(payloads.size(), 1u);    // invisible one skipped
}

TEST(AnnotationManagerTest, FromJsonRejectsBadSchema) {
  AnnotationManager m;
  nlohmann::json bad = {{"foo", "bar"}};
  EXPECT_FALSE(m.fromJson(bad));
}
