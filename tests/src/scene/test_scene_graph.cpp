// Pure unit tests for SceneGraph — id allocation, name aliasing, type-safe
// access, lifecycle. Type-erased storage means non-Entity primitives
// (EmitterEntity etc) live in the same container too — verified here.
//
// No Vulkan needed.

#include <gtest/gtest.h>
#include <hongtu/scene/scene_graph.hpp>
#include <hongtu/scene/local_entity.hpp>
#include <hongtu/scene/emitter_entity.hpp>

using hongtu::scene::LocalEmitter;
using hongtu::scene::LocalEntity;
using hongtu::scene::SceneGraph;

TEST(SceneGraphTest, EmptyByDefault) {
  SceneGraph sg;
  EXPECT_TRUE(sg.empty());
  EXPECT_EQ(sg.size(), 0u);
}

TEST(SceneGraphTest, CreateAssignsIncrementingIds) {
  SceneGraph sg;
  auto* a = sg.create<LocalEntity>("a");
  auto* b = sg.create<LocalEntity>("b");
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  EXPECT_NE(a, b);
  auto id_a = sg.idOfName("a");
  auto id_b = sg.idOfName("b");
  EXPECT_NE(id_a, id_b);
  EXPECT_NE(id_a, SceneGraph::kInvalidId);
  EXPECT_EQ(sg.size(), 2u);
}

TEST(SceneGraphTest, GetAsByIdAndName) {
  SceneGraph sg;
  auto* a = sg.create<LocalEntity>("fire1");
  auto id = sg.idOfName("fire1");
  EXPECT_EQ(sg.getAs<LocalEntity>(id), a);
  EXPECT_EQ(sg.getAsByName<LocalEntity>("fire1"), a);
  EXPECT_EQ(sg.nameOfId(id), "fire1");
}

TEST(SceneGraphTest, EmptyNameOK) {
  // Primitives without a display name are still addressable by id (just not
  // by name).
  SceneGraph sg;
  auto* a = sg.create<LocalEntity>("");
  auto v = sg.all<LocalEntity>();
  EXPECT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], a);
}

TEST(SceneGraphTest, GetAsTypeMismatchReturnsNull) {
  SceneGraph sg;
  sg.create<LocalEntity>("entity1");
  auto id = sg.idOfName("entity1");
  // Wrong type: ask for LocalEmitter when the stored thing is LocalEntity.
  EXPECT_EQ(sg.getAs<LocalEmitter>(id), nullptr);
}

TEST(SceneGraphTest, NonEntityPrimitiveStorable) {
  // EmitterEntity does NOT derive from scene::Entity — it has its own base.
  // The type-erased SceneGraph still accepts it.
  SceneGraph sg;
  auto* e = sg.create<LocalEmitter>("fire1");
  ASSERT_NE(e, nullptr);
  auto id = sg.idOfName("fire1");
  EXPECT_EQ(sg.getAs<LocalEmitter>(id), e);
  EXPECT_EQ(sg.all<LocalEmitter>().size(), 1u);
}

TEST(SceneGraphTest, MixedTypesCoexist) {
  // Both Entity-derived and non-Entity primitives in the same SceneGraph,
  // each retrievable via getAs<T>.
  SceneGraph sg;
  sg.create<LocalEntity>("model");
  sg.create<LocalEmitter>("fire");
  EXPECT_EQ(sg.size(), 2u);
  EXPECT_EQ(sg.all<LocalEntity>().size(), 1u);
  EXPECT_EQ(sg.all<LocalEmitter>().size(), 1u);
}

TEST(SceneGraphTest, CreateWithIdRespectsPreassigned) {
  SceneGraph sg;
  auto* preset = sg.createWithId<LocalEntity>(100, "preset");
  ASSERT_NE(preset, nullptr);
  sg.create<LocalEntity>("auto");
  EXPECT_GT(sg.idOfName("auto"), 100u);
}

TEST(SceneGraphTest, CreateWithIdRejectsZeroAndDuplicate) {
  SceneGraph sg;
  EXPECT_EQ(sg.createWithId<LocalEntity>(0, "zero"), nullptr);
  sg.createWithId<LocalEntity>(42, "a");
  EXPECT_EQ(sg.createWithId<LocalEntity>(42, "b"), nullptr);
}

TEST(SceneGraphTest, RemoveById) {
  SceneGraph sg;
  sg.create<LocalEntity>("x");
  auto id = sg.idOfName("x");
  EXPECT_TRUE(sg.remove(id));
  EXPECT_FALSE(sg.contains(id));
  EXPECT_EQ(sg.idOfName("x"), SceneGraph::kInvalidId);
  EXPECT_FALSE(sg.remove(id));
}

TEST(SceneGraphTest, RemoveByName) {
  SceneGraph sg;
  sg.create<LocalEntity>("y");
  EXPECT_TRUE(sg.removeByName("y"));
  EXPECT_EQ(sg.size(), 0u);
  EXPECT_FALSE(sg.removeByName("y"));
}

TEST(SceneGraphTest, AllOfType) {
  SceneGraph sg;
  sg.create<LocalEntity>("a");
  sg.create<LocalEntity>("b");
  sg.create<LocalEntity>("c");
  EXPECT_EQ(sg.all<LocalEntity>().size(), 3u);
}

TEST(SceneGraphTest, ClearResetsIdCounter) {
  SceneGraph sg;
  sg.create<LocalEntity>("first");
  sg.clear();
  EXPECT_EQ(sg.size(), 0u);
  auto* fresh = sg.create<LocalEntity>("after");
  ASSERT_NE(fresh, nullptr);
  EXPECT_EQ(sg.idOfName("after"), 1u);
}

TEST(SceneGraphTest, AnnotationsAndTimelineAreOwned) {
  SceneGraph sg;
  EXPECT_EQ(sg.annotations().size(), 0u);
  EXPECT_EQ(sg.timeline().driverCount(), 0u);
  sg.timeline().setDuration(15.0f);
  EXPECT_FLOAT_EQ(sg.timeline().getDuration(), 15.0f);
}
