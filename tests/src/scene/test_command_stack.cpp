#include <gtest/gtest.h>
#include <hongtu/scene/annotation/command_stack.hpp>
#include <hongtu/scene/annotation/polyline_annotation.hpp>

#include <nlohmann/json.hpp>

using hongtu::scene::AddAnnotationCommand;
using hongtu::scene::AnnotationManager;
using hongtu::scene::CommandStack;
using hongtu::scene::LocalPolylineAnnotation;
using hongtu::scene::ModifyAnnotationCommand;
using hongtu::scene::RemoveAnnotationCommand;

namespace {
std::unique_ptr<LocalPolylineAnnotation> makePoly(std::vector<glm::dvec3> v) {
  auto p = std::make_unique<LocalPolylineAnnotation>();
  p->setVertices(std::move(v));
  return p;
}
}

TEST(CommandStackTest, AddUndoRedo) {
  AnnotationManager m;
  CommandStack s;

  s.doCommand(std::make_unique<AddAnnotationCommand>(makePoly({{0,0,0}, {1,1,0}})), m);
  EXPECT_EQ(m.size(), 1u);

  EXPECT_TRUE(s.undo(m));
  EXPECT_EQ(m.size(), 0u);

  EXPECT_TRUE(s.redo(m));
  EXPECT_EQ(m.size(), 1u);
}

TEST(CommandStackTest, RemoveUndoRedo) {
  AnnotationManager m;
  CommandStack s;
  uint64_t id = m.add(makePoly({{0,0,0}, {2,2,0}}));

  s.doCommand(std::make_unique<RemoveAnnotationCommand>(id), m);
  EXPECT_EQ(m.size(), 0u);

  EXPECT_TRUE(s.undo(m));
  EXPECT_NE(m.get(id), nullptr);   // restored

  EXPECT_TRUE(s.redo(m));
  EXPECT_EQ(m.get(id), nullptr);
}

TEST(CommandStackTest, ModifyUndoRedo) {
  AnnotationManager m;
  CommandStack s;
  uint64_t id = m.add(makePoly({{0,0,0}, {1,0,0}}));

  auto* poly = dynamic_cast<LocalPolylineAnnotation*>(m.get(id));
  ASSERT_NE(poly, nullptr);
  auto before = poly->toJson();
  // build the "after" version directly (don't mutate in place — Modify
  // expects two distinct snapshots).
  LocalPolylineAnnotation modified;
  modified.setId(id);
  modified.setVertices({{0,0,0}, {2,0,0}, {2,2,0}});
  auto after = modified.toJson();

  s.doCommand(std::make_unique<ModifyAnnotationCommand>(id, before, after), m);
  EXPECT_EQ(dynamic_cast<LocalPolylineAnnotation*>(m.get(id))->vertexCount(), 3u);

  EXPECT_TRUE(s.undo(m));
  EXPECT_EQ(dynamic_cast<LocalPolylineAnnotation*>(m.get(id))->vertexCount(), 2u);
}

TEST(CommandStackTest, NewCommandClearsRedo) {
  AnnotationManager m;
  CommandStack s;
  s.doCommand(std::make_unique<AddAnnotationCommand>(makePoly({{0,0,0}})), m);
  s.undo(m);                                // undo → redo has 1
  EXPECT_EQ(s.redoDepth(), 1u);
  s.doCommand(std::make_unique<AddAnnotationCommand>(makePoly({{1,1,0}})), m);
  EXPECT_EQ(s.redoDepth(), 0u);             // new branch wipes redo
}

TEST(CommandStackTest, MaxDepthDropsOldest) {
  AnnotationManager m;
  CommandStack s;
  s.setMaxDepth(2);
  s.doCommand(std::make_unique<AddAnnotationCommand>(makePoly({{0,0,0}})), m);  // 1
  s.doCommand(std::make_unique<AddAnnotationCommand>(makePoly({{1,0,0}})), m);  // 2
  s.doCommand(std::make_unique<AddAnnotationCommand>(makePoly({{2,0,0}})), m);  // 3 → drops 1
  EXPECT_EQ(s.undoDepth(), 2u);
}

TEST(CommandStackTest, ClearWipesBothStacks) {
  AnnotationManager m;
  CommandStack s;
  s.doCommand(std::make_unique<AddAnnotationCommand>(makePoly({{0,0,0}})), m);
  s.undo(m);
  s.clear();
  EXPECT_EQ(s.undoDepth(), 0u);
  EXPECT_EQ(s.redoDepth(), 0u);
  EXPECT_FALSE(s.undo(m));
  EXPECT_FALSE(s.redo(m));
}
