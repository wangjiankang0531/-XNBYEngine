// Pure unit tests for the PolylineRenderer ribbon-expansion math. Exercises
// `expandPolylineRibbon()` directly so we don't need a Vulkan device.
//
// Test coverage:
//   - empty / single-point input produces zero output
//   - two-point ribbon: 6 vertices forming two triangles, ribbon width = full
//   - three-point ribbon: 12 vertices, two segments
//   - closed-loop: N segments, not N-1
//   - side direction is perpendicular to both segment tangent and view ray
//   - color is propagated to every emitted vertex

#include <gtest/gtest.h>
#include <hongtu/renderer/effects/polyline_renderer.hpp>
#include <glm/glm.hpp>

#include <cmath>

using hongtu::renderer::RibbonVertex;
using hongtu::renderer::expandPolylineRibbon;

namespace {

constexpr float kEps = 1e-4f;

bool nearVec(const glm::vec3& a, const glm::vec3& b, float eps = kEps) {
  return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps &&
         std::abs(a.z - b.z) < eps;
}

}  // namespace

TEST(PolylineRibbonTest, EmptyInputProducesNothing) {
  auto v = expandPolylineRibbon({}, 0.5f, {0, 0, 10}, {1, 1, 1, 1}, false);
  EXPECT_TRUE(v.empty());
}

TEST(PolylineRibbonTest, SinglePointProducesNothing) {
  auto v = expandPolylineRibbon({{0, 0, 0}}, 0.5f, {0, 0, 10}, {1, 1, 1, 1},
                                 false);
  EXPECT_TRUE(v.empty());
}

TEST(PolylineRibbonTest, TwoPointsEmitOneSegment) {
  // Horizontal segment along +X. Camera straight up (+Z) at (0,0,10).
  // For a segment along +X viewed from +Z, the side direction is along ±Y.
  std::vector<glm::vec3> pts = {{0, 0, 0}, {1, 0, 0}};
  auto v = expandPolylineRibbon(pts, 0.5f, {0.5f, 0.0f, 10.0f},
                                 {0.2f, 0.4f, 0.6f, 0.8f}, false);
  ASSERT_EQ(v.size(), 6u);  // 1 segment × 2 triangles × 3 verts

  // All Z coords stay 0 (ribbon in the xy-plane when the camera is straight up).
  for (const auto& rv : v) EXPECT_NEAR(rv.position.z, 0.0f, kEps);

  // Both endpoints of the ribbon at x=0 and x=1, offset ±0.5 along Y.
  // Triangle 0: L0, R0, L1.
  EXPECT_TRUE(nearVec(v[0].position, {0.0f, 0.5f, 0.0f}) ||
              nearVec(v[0].position, {0.0f, -0.5f, 0.0f}));
  EXPECT_NEAR(std::abs(v[0].position.y), 0.5f, kEps);
  EXPECT_NEAR(v[1].position.y, -v[0].position.y, kEps);

  // Color is broadcast.
  for (const auto& rv : v) {
    EXPECT_NEAR(rv.color.r, 0.2f, kEps);
    EXPECT_NEAR(rv.color.g, 0.4f, kEps);
    EXPECT_NEAR(rv.color.b, 0.6f, kEps);
    EXPECT_NEAR(rv.color.a, 0.8f, kEps);
  }
}

TEST(PolylineRibbonTest, ThreePointsEmitTwoSegments) {
  std::vector<glm::vec3> pts = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
  auto v = expandPolylineRibbon(pts, 0.5f, {1.0f, 0.0f, 10.0f},
                                 {1, 1, 1, 1}, false);
  EXPECT_EQ(v.size(), 12u);  // 2 segments × 6
}

TEST(PolylineRibbonTest, ClosedLoopAddsOneSegment) {
  // Three-point loop (triangle in XY plane). Closed → 3 segments, not 2.
  std::vector<glm::vec3> pts = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  auto open = expandPolylineRibbon(pts, 0.1f, {0.3f, 0.3f, 5.0f},
                                    {1, 1, 1, 1}, false);
  auto closed = expandPolylineRibbon(pts, 0.1f, {0.3f, 0.3f, 5.0f},
                                      {1, 1, 1, 1}, true);
  EXPECT_EQ(open.size(),   12u);  // 2 segments
  EXPECT_EQ(closed.size(), 18u);  // 3 segments
}

TEST(PolylineRibbonTest, SidePerpendicularToTangent) {
  // For a horizontal segment, the side offset at each endpoint should be
  // perpendicular to the segment direction. Tangent here is (1,0,0); side
  // must have dot product ≈ 0 with it.
  std::vector<glm::vec3> pts = {{0, 0, 0}, {1, 0, 0}};
  auto v = expandPolylineRibbon(pts, 0.5f, {0.5f, 0.0f, 10.0f},
                                 {1, 1, 1, 1}, false);
  const glm::vec3 tangent(1, 0, 0);
  // v[0] is L0 = point[0] + side(0)*half_width.
  // side = v[0].position - point[0] (normalised gives ±Y).
  glm::vec3 side0 = v[0].position - pts[0];
  EXPECT_NEAR(glm::dot(glm::normalize(side0), tangent), 0.0f, kEps);
}

TEST(PolylineRibbonTest, WidthZeroProducesDegenerateRibbon) {
  // half_width == 0 → both edges collapse to the polyline itself.
  std::vector<glm::vec3> pts = {{0, 0, 0}, {1, 0, 0}};
  auto v = expandPolylineRibbon(pts, 0.0f, {0, 0, 10}, {1, 1, 1, 1}, false);
  ASSERT_EQ(v.size(), 6u);
  // Every vertex sits exactly on the input segment.
  for (const auto& rv : v) {
    EXPECT_NEAR(rv.position.y, 0.0f, kEps);
    EXPECT_NEAR(rv.position.z, 0.0f, kEps);
  }
}

// =============================================================================
// Arc length (used by dash pattern in the fragment shader)
// =============================================================================

TEST(PolylineRibbonTest, ArcLengthIsZeroAtFirstPoint) {
  // Three points → ribbon emits arc_length per segment endpoint. The very
  // first two vertices (L0, R0 of segment 0) are at arc_length = 0.
  std::vector<glm::vec3> pts = {{0, 0, 0}, {3, 0, 0}, {3, 4, 0}};
  auto v = expandPolylineRibbon(pts, 0.5f, {1, 1, 10}, {1, 1, 1, 1}, false);
  ASSERT_FALSE(v.empty());
  EXPECT_NEAR(v[0].arc_length, 0.0f, kEps);
  EXPECT_NEAR(v[1].arc_length, 0.0f, kEps);  // R0 also at arc 0
}

TEST(PolylineArcTest, LiftsMidpointAndKeepsEndpoints) {
  using hongtu::renderer::buildArcPolyline;
  std::vector<glm::vec3> pts = {{0, 0, 0}, {10, 0, 0}};
  auto arc = buildArcPolyline(pts, 0.3f, 16);
  ASSERT_GE(arc.size(), 17u);              // 1 + 16 segments
  // 端点不动。
  EXPECT_NEAR(arc.front().x, 0.0f, kEps);  EXPECT_NEAR(arc.front().z, 0.0f, kEps);
  EXPECT_NEAR(arc.back().x, 10.0f, kEps);  EXPECT_NEAR(arc.back().z, 0.0f, kEps);
  // 峰高 ≈ span(10) × ratio(0.3) = 3，在中点附近。
  float maxz = 0.0f; glm::vec3 peak{};
  for (auto& p : arc) if (p.z > maxz) { maxz = p.z; peak = p; }
  EXPECT_NEAR(maxz, 3.0f, 0.05f);
  EXPECT_NEAR(peak.x, 5.0f, 0.5f);
}

TEST(PolylineArcTest, ZeroRatioPassesThrough) {
  using hongtu::renderer::buildArcPolyline;
  std::vector<glm::vec3> pts = {{0, 0, 0}, {10, 0, 0}, {10, 5, 0}};
  EXPECT_EQ(buildArcPolyline(pts, 0.0f, 16).size(), pts.size());
}

TEST(PolylineRibbonTest, SideTagsLeftRightForLineStyles) {
  // 描边/发光线靠 |side| 分芯/缘:每段 6 顶点按 L/R 序应是
  // (+1,-1,+1, -1,-1,+1)。横切对称,| side | 必为 1(边缘)。
  std::vector<glm::vec3> pts = {{0, 0, 0}, {3, 0, 0}};
  auto v = expandPolylineRibbon(pts, 0.5f, {1, 1, 10}, {1, 1, 1, 1}, false);
  ASSERT_EQ(v.size(), 6u);
  const float expect[6] = {+1, -1, +1, -1, -1, +1};
  for (int i = 0; i < 6; ++i) EXPECT_NEAR(v[i].side, expect[i], kEps);
}

TEST(PolylineRibbonTest, ArrowVerticesAreCenterSided) {
  // 箭头三角顶点 side=0 → 描边/发光当芯处理,不被边缘衰减吃掉。
  std::vector<glm::vec3> pts = {{0, 0, 0}, {3, 0, 0}};
  auto v = expandPolylineRibbon(pts, 0.5f, {1, 1, 10}, {1, 1, 1, 1}, false,
                                /*arrow_head=*/true);
  ASSERT_GE(v.size(), 9u);  // 6 ribbon + 3 arrow
  for (size_t i = 6; i < v.size(); ++i) {
    EXPECT_NEAR(v[i].side, 0.0f, kEps);
    EXPECT_LT(v[i].arc_length, 0.0f);  // sentinel:箭头恒实心
  }
}

TEST(PolylineRibbonTest, ArcLengthAccumulatesAlongPoints) {
  // Three points spaced 3m then 4m apart → endpoints at arc 0, 3, 7.
  std::vector<glm::vec3> pts = {{0, 0, 0}, {3, 0, 0}, {3, 4, 0}};
  auto v = expandPolylineRibbon(pts, 0.5f, {1, 1, 10}, {1, 1, 1, 1}, false);
  // Segment 0 emits 6 verts spanning arc 0→3. Segment 1 emits 6 verts
  // spanning arc 3→7. Pull the "L1" vert of segment 1 (index 8 = seg1*6+2)
  // and confirm it sits at arc 7.
  ASSERT_GE(v.size(), 12u);
  EXPECT_NEAR(v[6].arc_length, 3.0f, kEps);   // L0 of segment 1
  EXPECT_NEAR(v[8].arc_length, 7.0f, kEps);   // L1 of segment 1
}

// =============================================================================
// Arrows
// =============================================================================

TEST(PolylineRibbonTest, ArrowHeadAppendsThreeVerticesAtLastPoint) {
  std::vector<glm::vec3> pts = {{0, 0, 0}, {1, 0, 0}};
  auto base = expandPolylineRibbon(pts, 0.5f, {0.5f, 0.0f, 10.0f},
                                    {1, 1, 1, 1}, false);
  auto with_head = expandPolylineRibbon(pts, 0.5f, {0.5f, 0.0f, 10.0f},
                                         {1, 1, 1, 1}, false,
                                         /*arrow_head=*/true,
                                         /*arrow_tail=*/false,
                                         /*arrow_size=*/2.0f);
  EXPECT_EQ(with_head.size(), base.size() + 3u);
  // The 3 arrow verts come last and carry the sentinel arc_length.
  ASSERT_GE(with_head.size(), 3u);
  for (size_t i = with_head.size() - 3; i < with_head.size(); ++i) {
    EXPECT_LT(with_head[i].arc_length, 0.0f);
  }
  // Tip vertex sits at the last polyline point.
  const auto& tip = with_head[with_head.size() - 3];
  EXPECT_NEAR(tip.position.x, 1.0f, kEps);
  EXPECT_NEAR(tip.position.y, 0.0f, kEps);
}

TEST(PolylineRibbonTest, ArrowTailAppendsThreeVerticesAtFirstPoint) {
  std::vector<glm::vec3> pts = {{0, 0, 0}, {1, 0, 0}};
  auto v = expandPolylineRibbon(pts, 0.5f, {0.5f, 0.0f, 10.0f},
                                 {1, 1, 1, 1}, false,
                                 /*arrow_head=*/false,
                                 /*arrow_tail=*/true,
                                 /*arrow_size=*/2.0f);
  // 6 ribbon verts + 3 arrow verts.
  ASSERT_EQ(v.size(), 9u);
  // Tail-arrow tip is at the first polyline point.
  const auto& tip = v[6];
  EXPECT_NEAR(tip.position.x, 0.0f, kEps);
  EXPECT_NEAR(tip.position.y, 0.0f, kEps);
  EXPECT_LT(tip.arc_length, 0.0f);
}

TEST(PolylineRibbonTest, ArrowHeadAndTailTogetherAppendSixVertices) {
  std::vector<glm::vec3> pts = {{0, 0, 0}, {5, 0, 0}};
  auto v = expandPolylineRibbon(pts, 0.5f, {2.5f, 0.0f, 10.0f},
                                 {1, 1, 1, 1}, false,
                                 /*arrow_head=*/true,
                                 /*arrow_tail=*/true);
  // 6 ribbon + 3 head + 3 tail = 12.
  EXPECT_EQ(v.size(), 12u);
}

TEST(PolylineRibbonTest, ClosedPolylineIgnoresArrows) {
  // Loops have no head/tail — arrow flags should produce no extra verts.
  std::vector<glm::vec3> pts = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  auto open = expandPolylineRibbon(pts, 0.5f, {0.3f, 0.3f, 5.0f},
                                    {1, 1, 1, 1}, false);
  auto closed_with_arrows = expandPolylineRibbon(
      pts, 0.5f, {0.3f, 0.3f, 5.0f}, {1, 1, 1, 1},
      /*closed=*/true,
      /*arrow_head=*/true, /*arrow_tail=*/true);
  // Closed adds 1 extra segment (6 verts) and zero arrow verts.
  EXPECT_EQ(closed_with_arrows.size(), open.size() + 6u);
}

TEST(PolylineRibbonTest, ArrowSizeZeroFallsBackToDefault) {
  // arrow_size <= 0 → default a_len = max(full_w*2.4, half_width*5).
  // half_width=0.5 → full_w=1.0 → max(2.4, 2.5) = 2.5.
  std::vector<glm::vec3> pts = {{0, 0, 0}, {10, 0, 0}};
  auto v = expandPolylineRibbon(pts, 0.5f, {5.0f, 0.0f, 10.0f},
                                 {1, 1, 1, 1}, false,
                                 /*arrow_head=*/true);
  ASSERT_EQ(v.size(), 9u);  // 6 ribbon + 3 head
  const auto& tip    = v[6];
  const auto& base_l = v[7];
  // Tip stays at the original last point; base 2.5 units behind it.
  EXPECT_NEAR(tip.position.x,    10.0f, kEps);
  EXPECT_NEAR(base_l.position.x,  7.5f, 0.01f);
  // Ribbon body endpoint (L1 of the single segment) is pulled BACK to the
  // arrowhead base so a thick line never overlaps the head. v[2] is L1.
  EXPECT_NEAR(v[2].position.x, 7.5f, 0.01f);
}

TEST(PolylineRibbonTest, FlatRibbonStaysHorizontal) {
  // A horizontal line along +X, viewed from a near-grazing camera. With the
  // screen-facing ribbon the side vector tilts toward vertical and the ribbon
  // dips below z=0 ("一半在地上一半在地下"). The flat ribbon must keep every
  // vertex at the line's own z (side vector lies in the horizontal plane).
  std::vector<glm::vec3> pts = {{0, 0, 0}, {10, 0, 0}};
  const glm::vec3 grazing_cam(5.0f, -2.0f, 0.2f);  // almost level with the line

  auto flat = expandPolylineRibbon(pts, 1.0f, grazing_cam, {1, 1, 1, 1},
                                   /*closed=*/false, /*head=*/false,
                                   /*tail=*/false, /*arrow_size=*/0.0f,
                                   /*flat=*/true);
  ASSERT_GE(flat.size(), 6u);
  for (const auto& rv : flat) EXPECT_NEAR(rv.position.z, 0.0f, 1e-4f);  // 不入地

  // Screen-facing (flat=false) from the same grazing view spreads in z.
  auto facing = expandPolylineRibbon(pts, 1.0f, grazing_cam, {1, 1, 1, 1},
                                     /*closed=*/false, /*head=*/false,
                                     /*tail=*/false, /*arrow_size=*/0.0f,
                                     /*flat=*/false);
  float max_abs_z = 0.0f;
  for (const auto& rv : facing) max_abs_z = std::max(max_abs_z, std::abs(rv.position.z));
  EXPECT_GT(max_abs_z, 0.1f);  // 朝相机 ribbon 在掠射角下明显偏离水平面
}
