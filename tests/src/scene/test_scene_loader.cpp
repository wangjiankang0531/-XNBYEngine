// SceneLoader unit tests — load + save + round-trip + schema validation.
// Pure host — no Vulkan. Builds JSON fixtures inline so tests don't depend
// on filesystem state beyond /tmp.

#include <gtest/gtest.h>
#include <hongtu/scene/scene_loader.hpp>
#include <hongtu/scene/scene_graph.hpp>
#include <hongtu/scene/emitter_entity.hpp>

#include <fstream>
#include <string>

using hongtu::scene::LocalEmitter;
using hongtu::scene::SceneGraph;
using hongtu::scene::SceneLoader;

namespace {

std::string writeFixture(const char* body) {
  static int n = 0;
  std::string path = "/tmp/test_scene_loader_" + std::to_string(n++) + ".json";
  std::ofstream f(path);
  f << body;
  f.close();
  return path;
}

}  // namespace

TEST(SceneLoaderTest, LoadsMinimalSchema) {
  auto p = writeFixture(R"({
    "version": 1, "mode": "local",
    "annotations": [], "emitters": [],
    "timeline": { "duration_seconds": 10, "tracks": {} }
  })");
  SceneLoader l;
  auto sg = l.load(p);
  ASSERT_TRUE(sg.has_value()) << l.lastError();
  EXPECT_FLOAT_EQ((*sg)->timeline().getDuration(), 10.0f);
  EXPECT_TRUE((*sg)->empty());
}

TEST(SceneLoaderTest, LoadsEmitterByPreset) {
  auto p = writeFixture(R"({
    "version": 1, "mode": "local",
    "annotations": [],
    "emitters": [
      { "id": "fire1", "preset": "fire", "local": [1, 2, 3],
        "params": { "emit_rate": 200.0 }, "enabled": true }
    ],
    "timeline": { "duration_seconds": 0, "tracks": {} }
  })");
  SceneLoader l;
  auto sg = l.load(p);
  ASSERT_TRUE(sg.has_value()) << l.lastError();
  auto* em = (*sg)->getAsByName<LocalEmitter>("fire1");
  ASSERT_NE(em, nullptr);
  EXPECT_DOUBLE_EQ(em->getPosition().x, 1.0);
  EXPECT_FLOAT_EQ(em->getDesc().emit_rate, 200.0f);
  EXPECT_TRUE(em->getDesc().enabled);
}

TEST(SceneLoaderTest, LoadsAnnotations) {
  auto p = writeFixture(R"({
    "version": 1, "mode": "local",
    "emitters": [],
    "annotations": [
      { "id": 7, "kind": "polyline", "mode": "local",
        "vertices": [[0,0,0], [1,1,1]] }
    ],
    "timeline": { "duration_seconds": 0, "tracks": {} }
  })");
  SceneLoader l;
  auto sg = l.load(p);
  ASSERT_TRUE(sg.has_value()) << l.lastError();
  EXPECT_EQ((*sg)->annotations().size(), 1u);
  EXPECT_NE((*sg)->annotations().get(7), nullptr);
}

TEST(SceneLoaderTest, WiresTimelineEnableTrack) {
  auto p = writeFixture(R"({
    "version": 1, "mode": "local",
    "annotations": [],
    "emitters": [
      { "id": "fire1", "preset": "fire", "local": [0,0,0],
        "params": { "emit_rate": 100 }, "enabled": false }
    ],
    "timeline": { "duration_seconds": 10,
      "tracks": {
        "fire1.enabled": {
          "type": "bool",
          "keys": [{"t":0,"v":false}, {"t":5,"v":true}]
        }
      } }
  })");
  SceneLoader l;
  auto sg = l.load(p);
  ASSERT_TRUE(sg.has_value()) << l.lastError();
  EXPECT_EQ((*sg)->timeline().driverCount(), 1u);

  // Drive the timeline to t=4 — fire still disabled.
  auto* em = (*sg)->getAsByName<LocalEmitter>("fire1");
  ASSERT_NE(em, nullptr);
  (*sg)->timeline().play();
  (*sg)->timeline().update(4.0f);
  EXPECT_FALSE(em->getDesc().enabled);
  // Then to t=6 — fire flipped on.
  (*sg)->timeline().update(2.0f);
  EXPECT_TRUE(em->getDesc().enabled);
}

TEST(SceneLoaderTest, RejectsMissingVersion) {
  auto p = writeFixture(R"({ "mode": "local" })");
  SceneLoader l;
  EXPECT_FALSE(l.load(p).has_value());
  EXPECT_FALSE(l.lastError().empty());
}

TEST(SceneLoaderTest, RejectsBadVersion) {
  auto p = writeFixture(R"({ "version": 99, "mode": "local" })");
  SceneLoader l;
  EXPECT_FALSE(l.load(p).has_value());
}

TEST(SceneLoaderTest, RejectsUnknownMode) {
  auto p = writeFixture(R"({ "version": 1, "mode": "ufo" })");
  SceneLoader l;
  EXPECT_FALSE(l.load(p).has_value());
}

TEST(SceneLoaderTest, SkipsBadTimelineTrack) {
  // Track field other than `enabled` should be warned + skipped, not crash.
  auto p = writeFixture(R"({
    "version": 1, "mode": "local",
    "annotations": [],
    "emitters": [
      { "id": "fire1", "preset": "fire", "local": [0,0,0],
        "params": {}, "enabled": false }
    ],
    "timeline": { "duration_seconds": 10,
      "tracks": {
        "fire1.color": { "type": "vec4", "keys": [{"t":0,"v":[1,1,1,1]}] }
      } }
  })");
  SceneLoader l;
  auto sg = l.load(p);
  ASSERT_TRUE(sg.has_value());
  // The malformed track was warned and skipped.
  EXPECT_EQ((*sg)->timeline().driverCount(), 0u);
}

TEST(SceneLoaderTest, RoundTripPreservesEmitterAndAnnotation) {
  auto p1 = writeFixture(R"({
    "version": 1, "mode": "local",
    "annotations": [
      { "id": 1, "kind": "polyline", "mode": "local",
        "vertices": [[0,0,0], [5,0,0]] }
    ],
    "emitters": [
      { "id": "fire1", "preset": "smoke", "local": [3, 4, 0],
        "params": { "emit_rate": 75 }, "enabled": true }
    ],
    "timeline": { "duration_seconds": 20, "tracks": {} }
  })");
  SceneLoader l;
  auto sg = l.load(p1);
  ASSERT_TRUE(sg.has_value()) << l.lastError();

  std::string p2 = "/tmp/test_scene_loader_roundtrip.json";
  ASSERT_TRUE(l.save(**sg, p2));

  auto sg2 = l.load(p2);
  ASSERT_TRUE(sg2.has_value()) << l.lastError();

  auto* em = (*sg2)->getAsByName<LocalEmitter>("fire1");
  ASSERT_NE(em, nullptr);
  EXPECT_DOUBLE_EQ(em->getPosition().x, 3.0);
  EXPECT_FLOAT_EQ(em->getDesc().emit_rate, 75.0f);
  EXPECT_TRUE(em->getDesc().enabled);
  EXPECT_EQ((*sg2)->annotations().size(), 1u);
  EXPECT_FLOAT_EQ((*sg2)->timeline().getDuration(), 20.0f);
}

TEST(SceneLoaderTest, LoadsCommittedFixture) {
  // The fixture in data/scenes/park_minimal.scene.json should load cleanly.
  // Tests run from build/tests/ so we anchor against HT_ENGINE_SOURCE_DIR.
  std::string path = std::string(HT_ENGINE_SOURCE_DIR)
                   + "/data/scenes/park_minimal.scene.json";
  SceneLoader l;
  auto sg = l.load(path);
  ASSERT_TRUE(sg.has_value()) << l.lastError() << " (path=" << path << ")";
  EXPECT_NE((*sg)->getAsByName<LocalEmitter>("main_fire"), nullptr);
  EXPECT_EQ((*sg)->annotations().size(), 1u);
}
