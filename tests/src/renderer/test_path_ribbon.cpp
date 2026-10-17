// buildPathRibbon — 折线 → 园区路径条带网格(P5b 路径工具的几何内核)。
#include <hongtu/renderer/pbr/path_ribbon.hpp>

#include <gtest/gtest.h>

#include <glm/glm.hpp>

using hongtu::renderer::buildPathRibbon;
using hongtu::renderer::MeshVertex;

namespace {

TEST(PathRibbon, StraightSegmentGeometry) {
  std::vector<glm::vec2> pts = {{0, 0}, {10, 0}, {20, 0}};
  std::vector<MeshVertex> verts;
  std::vector<uint32_t>   idx;
  buildPathRibbon(pts, 2.0f, verts, idx);

  ASSERT_EQ(verts.size(), pts.size() * 2);          // 每点左右两顶点
  ASSERT_EQ(idx.size(), (pts.size() - 1) * 6);      // 每段两三角

  // 直线段:宽度恰为 2×half_width,且沿 X 不变形。
  for (size_t i = 0; i < pts.size(); ++i) {
    const auto& l = verts[i * 2].position;
    const auto& r = verts[i * 2 + 1].position;
    EXPECT_NEAR(glm::distance(glm::vec2(l), glm::vec2(r)), 4.0f, 1e-4f);
    EXPECT_NEAR(l.x, pts[i].x, 1e-4f);
    EXPECT_NEAR(r.x, pts[i].x, 1e-4f);
  }
  // UV.x 沿弧长单调递增;法线 +Z;z 微抬。
  EXPECT_LT(verts[0].uv.x, verts[2].uv.x);
  EXPECT_LT(verts[2].uv.x, verts[4].uv.x);
  for (const auto& v : verts) {
    EXPECT_NEAR(v.normal.z, 1.0f, 1e-6f);
    EXPECT_GT(v.position.z, 0.0f);
  }
}

TEST(PathRibbon, WindingMatchesGroundConvention) {
  // 与 makeGround 同绕序约定:三角形叉积朝 -Z(gl_FrontFacing 翻法线用)。
  std::vector<glm::vec2> pts = {{0, 0}, {10, 0}};
  std::vector<MeshVertex> verts;
  std::vector<uint32_t>   idx;
  buildPathRibbon(pts, 1.5f, verts, idx);
  ASSERT_GE(idx.size(), 3u);
  for (size_t t = 0; t + 2 < idx.size(); t += 3) {
    const glm::vec3 a = verts[idx[t]].position;
    const glm::vec3 b = verts[idx[t + 1]].position;
    const glm::vec3 c = verts[idx[t + 2]].position;
    const glm::vec3 n = glm::cross(b - a, c - a);
    EXPECT_LT(n.z, 0.0f) << "triangle " << t / 3;
  }
}

TEST(PathRibbon, SharpCornerMiterClamped) {
  // 接近 180° 折返的锐角:miter 截断,不喷出超长尖刺。
  std::vector<glm::vec2> pts = {{0, 0}, {10, 0}, {0.5f, 0.8f}};
  std::vector<MeshVertex> verts;
  std::vector<uint32_t>   idx;
  buildPathRibbon(pts, 2.0f, verts, idx);
  ASSERT_EQ(verts.size(), 6u);
  // 角点(i=1)的左右两顶点:miter 截断在 2×half_width 以内,无尖刺。
  for (int k = 2; k <= 3; ++k) {
    EXPECT_LT(glm::length(glm::vec2(verts[k].position) - pts[1]),
              2.0f * 2.0f + 1e-3f)
        << "miter spike at corner vertex " << k;
  }
}

TEST(PathRibbon, SmoothOpenPolylinePreservesEndpointsAndRoundsCorner) {
  // 直角折线:圆滑后端点不动、点数增长、拐角处离原角点越来越远(被削圆)。
  const std::vector<glm::vec2> raw = {{0, 0}, {10, 0}, {10, 10}};
  const auto s0 = hongtu::renderer::smoothOpenPolyline(raw, 0);
  EXPECT_EQ(s0, raw);  // 0 次迭代 = 原样

  // Chaikin 收敛到二次 B 样条极限曲线:角点最近距离从首切 2.5 单调
  // 递减趋近 2.5·√2/2 ≈ 1.77 —— 始终被"削圆",不会塌回尖角。
  float prev_min_corner_dist = 1e9f;
  for (int it = 1; it <= 3; ++it) {
    const auto sm = hongtu::renderer::smoothOpenPolyline(raw, it);
    EXPECT_GT(sm.size(), raw.size());
    EXPECT_EQ(sm.front(), raw.front());
    EXPECT_EQ(sm.back(), raw.back());
    float min_d = 1e9f;
    for (const auto& q : sm) min_d = std::min(min_d, glm::distance(q, raw[1]));
    EXPECT_LE(min_d, prev_min_corner_dist);
    EXPECT_GT(min_d, 1.7f);
    prev_min_corner_dist = min_d;
  }
}

TEST(PathRibbon, DegenerateInputsProduceNothing) {
  std::vector<MeshVertex> verts;
  std::vector<uint32_t>   idx;
  buildPathRibbon({}, 2.0f, verts, idx);
  buildPathRibbon({{1, 1}}, 2.0f, verts, idx);
  buildPathRibbon({{1, 1}, {2, 2}}, 0.0f, verts, idx);
  EXPECT_TRUE(verts.empty());
  EXPECT_TRUE(idx.empty());
}

}  // namespace
