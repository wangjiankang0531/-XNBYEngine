#include <gtest/gtest.h>
#include <hongtu/renderer/native_geometry_lod.hpp>

using namespace hongtu::renderer;

TEST(NativeGeometryLod, ResolutionAndFovDriveRefinement) {
  const float edge = projectedNativeEdge(3, 400, 50, 720);
  EXPECT_NEAR(projectedNativeEdge(3, 400, 50, 1440), edge * 2, 0.001f);
  EXPECT_GT(projectedNativeEdge(3, 400, 25, 720), edge);
  EXPECT_GT(projectedNativeEdge(3, 200, 50, 720), edge);
  EXPECT_EQ(selectNativeGeometryDepth(200, 6.01f, 3, 7), 4);
  EXPECT_EQ(selectNativeGeometryDepth(200, 6.0f, 3, 7), 3);
  EXPECT_EQ(selectNativeGeometryDepth(200, 20, 7, 7), 7);
}

TEST(NativeGeometryLod, VisibleRootHysteresisPreservesFallback) {
  EXPECT_EQ(selectNativeGeometryDepth(31, 0, 0, 7), 0);
  EXPECT_EQ(selectNativeGeometryDepth(32, 0, 0, 7), 3);
  EXPECT_EQ(selectNativeGeometryDepth(25, 2, 3, 7), 3);
  EXPECT_EQ(selectNativeGeometryDepth(23, 2, 3, 7), 0);
  EXPECT_EQ(selectNativeGeometryDepth(100, 2, 0, 2), 2);
  EXPECT_EQ(projectedNativeEdge(3, 500, 50, 0), 0);
  EXPECT_EQ(projectedNativeEdge(3, 500, 0, 720), 0);
}

TEST(NativeGeometryLod, AdaptiveDistanceRetreatAndBaseQuality) {
  // A large bounding box does not justify loading detail that is subpixel.
  EXPECT_EQ(selectAdaptiveNativeDepth(300, 2, 0, 0, 7), 0);
  EXPECT_EQ(selectAdaptiveNativeDepth(300, 7, 0, 0, 7), 3);
  EXPECT_EQ(selectAdaptiveNativeDepth(300, 40, 9, 4, 7), 5);
  EXPECT_EQ(selectAdaptiveNativeDepth(300, 10, 1.5f, 6, 7, 3), 5);
  EXPECT_EQ(selectAdaptiveNativeDepth(300, 10, 1.5f, 6, 7, 20), 6);
  EXPECT_EQ(selectAdaptiveNativeDepth(300, 10, 1.5f, 6, 7), 6);
  EXPECT_EQ(selectAdaptiveNativeDepth(300, 10, 3, 5, 7), 5);
  EXPECT_EQ(selectAdaptiveNativeDepth(300, 2.9f, 1, 5, 7), 0);
  // Zoom and viewport resolution may require fine data above an altitude gate.
  const float base = projectedNativeEdge(20, 3500, 10, 2160);
  EXPECT_EQ(selectAdaptiveNativeDepth(500, base, 0, 0, 7), 3);
}

TEST(NativeGeometryLod, TextureMagnificationDrivesDenseGeometry) {
  EXPECT_EQ(selectAdaptiveNativeDepth(300, 2, 0, 0, 7, 0, 2), 3);
  EXPECT_EQ(selectAdaptiveNativeDepth(300, 10, 2, 4, 7, 0, 0, 2), 5);
  EXPECT_EQ(selectAdaptiveNativeDepth(300, 10, 1, 6, 7, 3, 0, .1f, 2), 6);
  EXPECT_EQ(selectAdaptiveNativeDepth(300, 10, 1, 6, 7, 3, 0, .1f, .1f), 5);
}
