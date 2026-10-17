// Unit tests for all 5 DrawingTools using LocalCamera + AnnotationMode::Local
// for deterministic geometry. Pure host — no Vulkan.

#include <gtest/gtest.h>
#include <hongtu/scene/annotation/drawing_tool.hpp>
#include <hongtu/scene/annotation/polyline_annotation.hpp>
#include <hongtu/scene/annotation/polygon_annotation.hpp>
#include <hongtu/scene/annotation/symbol_annotation.hpp>
#include <hongtu/scene/annotation/text_annotation.hpp>
#include <hongtu/scene/annotation/arrow_annotation.hpp>
#include <hongtu/renderer/camera/local_camera.hpp>

using hongtu::scene::ArrowTool;
using hongtu::scene::DrawInputState;
using hongtu::scene::DrawingTool;
using hongtu::scene::PolygonTool;
using hongtu::scene::PolylineTool;
using hongtu::scene::SymbolTool;
using hongtu::scene::TextTool;
using hongtu::scene::AnnotationMode;
using hongtu::scene::AnnotationKind;
using hongtu::scene::LocalPolylineAnnotation;
using hongtu::scene::LocalPolygonAnnotation;
using hongtu::scene::LocalSymbolAnnotation;
using hongtu::scene::LocalTextAnnotation;
using hongtu::scene::LocalArrowAnnotation;
using hongtu::renderer::LocalCamera;

namespace {

void configureTestCamera(LocalCamera& c) {
  c.setScreenSize(1280.0f, 720.0f);
  c.setMode(LocalCamera::Mode::Orbit);
  c.lookAtTarget({0.0, 0.0, 0.0}, /*distance=*/22.0, /*yaw=*/0.0, /*pitch=*/-0.5);
}

DrawInputState clickAt(int x, int y) {
  DrawInputState s;
  s.mouse_x = x; s.mouse_y = y;
  s.viewport_w = 1280; s.viewport_h = 720;
  s.left_clicked = true;
  return s;
}

DrawInputState rightClickFrame() {
  DrawInputState s;
  s.viewport_w = 1280; s.viewport_h = 720;
  s.right_clicked = true;
  return s;
}

DrawInputState escapeFrame() {
  DrawInputState s;
  s.viewport_w = 1280; s.viewport_h = 720;
  s.escape_pressed = true;
  return s;
}

}  // namespace

// =============================================================================
// PolylineTool
// =============================================================================

TEST(PolylineToolTest, ThreeClicksThenRightEmitsPolyline) {
  PolylineTool t;
  LocalCamera cam; configureTestCamera(cam);

  EXPECT_FALSE(t.isDrawing());
  t.tick(clickAt(600, 360), cam, AnnotationMode::Local);
  EXPECT_TRUE(t.isDrawing());
  t.tick(clickAt(640, 360), cam, AnnotationMode::Local);
  t.tick(clickAt(680, 360), cam, AnnotationMode::Local);
  t.tick(rightClickFrame(), cam, AnnotationMode::Local);

  EXPECT_FALSE(t.isDrawing());
  auto a = t.takeCompleted();
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->getKind(), AnnotationKind::Polyline);
  EXPECT_EQ(dynamic_cast<LocalPolylineAnnotation*>(a.get())->vertexCount(), 3u);
}

TEST(PolylineToolTest, EscapeCancelsInProgress) {
  PolylineTool t;
  LocalCamera cam; configureTestCamera(cam);
  t.tick(clickAt(640, 360), cam, AnnotationMode::Local);
  t.tick(clickAt(680, 360), cam, AnnotationMode::Local);
  EXPECT_TRUE(t.isDrawing());
  t.tick(escapeFrame(), cam, AnnotationMode::Local);
  EXPECT_FALSE(t.isDrawing());
  EXPECT_EQ(t.takeCompleted(), nullptr);
}

TEST(PolylineToolTest, RightClickBeforeTwoPointsEmitsNothing) {
  PolylineTool t;
  LocalCamera cam; configureTestCamera(cam);
  t.tick(clickAt(640, 360), cam, AnnotationMode::Local);
  t.tick(rightClickFrame(), cam, AnnotationMode::Local);
  EXPECT_EQ(t.takeCompleted(), nullptr);
}

TEST(PolylineToolTest, PreviewMatchesClickedPoints) {
  PolylineTool t;
  LocalCamera cam; configureTestCamera(cam);
  hongtu::coordinate::PrecisionOrigin origin;

  EXPECT_FALSE(t.previewPayload(origin).has_value());
  t.tick(clickAt(640, 360), cam, AnnotationMode::Local);
  auto prev = t.previewPayload(origin);
  ASSERT_TRUE(prev.has_value());
  EXPECT_EQ(prev->rtc_vertices.size(), 1u);
}

// =============================================================================
// PolygonTool
// =============================================================================

TEST(PolygonToolTest, ThreePointsEmits) {
  PolygonTool t;
  LocalCamera cam; configureTestCamera(cam);
  t.tick(clickAt(600, 350), cam, AnnotationMode::Local);
  t.tick(clickAt(640, 350), cam, AnnotationMode::Local);
  t.tick(clickAt(640, 380), cam, AnnotationMode::Local);
  t.tick(rightClickFrame(), cam, AnnotationMode::Local);

  auto a = t.takeCompleted();
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->getKind(), AnnotationKind::Polygon);
  EXPECT_EQ(dynamic_cast<LocalPolygonAnnotation*>(a.get())->vertexCount(), 3u);
}

TEST(PolygonToolTest, TwoPointsRightDoesNotEmit) {
  PolygonTool t;
  LocalCamera cam; configureTestCamera(cam);
  t.tick(clickAt(640, 360), cam, AnnotationMode::Local);
  t.tick(clickAt(680, 360), cam, AnnotationMode::Local);
  t.tick(rightClickFrame(), cam, AnnotationMode::Local);
  EXPECT_EQ(t.takeCompleted(), nullptr);
}

// =============================================================================
// SymbolTool
// =============================================================================

TEST(SymbolToolTest, SingleClickEmits) {
  SymbolTool t;
  t.setSymbolIndex(5);
  LocalCamera cam; configureTestCamera(cam);
  t.tick(clickAt(640, 360), cam, AnnotationMode::Local);
  auto a = t.takeCompleted();
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->getKind(), AnnotationKind::Symbol);
  EXPECT_EQ(a->style().symbol_index, 5u);
}

TEST(SymbolToolTest, EscapeDropsCompleted) {
  SymbolTool t;
  LocalCamera cam; configureTestCamera(cam);
  t.tick(clickAt(640, 360), cam, AnnotationMode::Local);
  t.cancel();
  EXPECT_EQ(t.takeCompleted(), nullptr);
}

// =============================================================================
// TextTool
// =============================================================================

TEST(TextToolTest, ClickEmitsWithPresetText) {
  TextTool t;
  t.setText("EXIT");
  LocalCamera cam; configureTestCamera(cam);
  t.tick(clickAt(640, 360), cam, AnnotationMode::Local);
  auto a = t.takeCompleted();
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->style().text, "EXIT");
}

// =============================================================================
// ArrowTool
// =============================================================================

TEST(ArrowToolTest, TwoClicksEmitsWithTwoEndpoints) {
  ArrowTool t;
  LocalCamera cam; configureTestCamera(cam);
  // Use clicks that vary in both screen X and Y so the resulting world
  // ground hits are reliably distinct regardless of orbital orientation.
  t.tick(clickAt(500, 300), cam, AnnotationMode::Local);
  EXPECT_TRUE(t.isDrawing());
  t.tick(clickAt(780, 420), cam, AnnotationMode::Local);
  EXPECT_FALSE(t.isDrawing());
  auto a = t.takeCompleted();
  ASSERT_NE(a, nullptr);
  auto* arrow = dynamic_cast<LocalArrowAnnotation*>(a.get());
  ASSERT_NE(arrow, nullptr);
  // Distance between start/end should be non-trivial — both pixels are
  // far apart on screen, so their ground hits must differ.
  glm::dvec3 diff = arrow->getEnd() - arrow->getStart();
  EXPECT_GT(glm::length(diff), 1.0);
}

TEST(ArrowToolTest, EscapeAfterFirstClickResetsStart) {
  ArrowTool t;
  LocalCamera cam; configureTestCamera(cam);
  t.tick(clickAt(600, 360), cam, AnnotationMode::Local);
  EXPECT_TRUE(t.isDrawing());
  t.tick(escapeFrame(), cam, AnnotationMode::Local);
  EXPECT_FALSE(t.isDrawing());
}

TEST(ArrowToolTest, PreviewAfterFirstClick) {
  ArrowTool t;
  LocalCamera cam; configureTestCamera(cam);
  hongtu::coordinate::PrecisionOrigin origin;
  EXPECT_FALSE(t.previewPayload(origin).has_value());
  t.tick(clickAt(640, 360), cam, AnnotationMode::Local);
  EXPECT_TRUE(t.previewPayload(origin).has_value());
}
