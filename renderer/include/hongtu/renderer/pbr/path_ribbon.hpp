#pragma once

#include <hongtu/renderer/pbr/mesh_vertex.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace hongtu::renderer {

/// 开放折线 Chaikin 圆角细分(端点保持)。iterations 0 = 原样;每次迭代
/// 把每条内段切成 1/4–3/4 两点,拐角逐次圆滑(2–3 次即接近圆弧)。
inline std::vector<glm::vec2> smoothOpenPolyline(
    const std::vector<glm::vec2>& points, int iterations) {
  std::vector<glm::vec2> cur = points;
  iterations = glm::clamp(iterations, 0, 4);
  for (int it = 0; it < iterations && cur.size() >= 3; ++it) {
    std::vector<glm::vec2> next;
    next.reserve(cur.size() * 2);
    next.push_back(cur.front());  // 端点保持
    for (size_t i = 0; i + 1 < cur.size(); ++i) {
      const glm::vec2 a = cur[i], b = cur[i + 1];
      next.push_back(a * 0.75f + b * 0.25f);
      next.push_back(a * 0.25f + b * 0.75f);
    }
    next.push_back(cur.back());
    cur = std::move(next);
  }
  return cur;
}

/// 折线 → 地面路径条带网格(园区步道)。
///
/// 每个顶点取相邻段的角平分线(miter)外扩 half_width,锐角处 miter 长度
/// 截断到 2×half_width 防尖刺;UV.x 沿弧长铺(uv_per_m),UV.y 0..1 横向。
/// 输出与 makeGround 同绕序约定(叉积 -Z 为正面),法线 +Z,微抬 z 防
/// 与草地 z-fighting。点数 < 2 时不输出。
inline void buildPathRibbon(const std::vector<glm::vec2>& points,
                            float half_width,
                            std::vector<MeshVertex>& verts,
                            std::vector<uint32_t>& idx,
                            float uv_per_m = 0.18f,
                            float z = 0.02f) {
  if (points.size() < 2 || half_width <= 0.0f) return;
  const uint32_t base = static_cast<uint32_t>(verts.size());
  const size_t n = points.size();

  float arc = 0.0f;
  for (size_t i = 0; i < n; ++i) {
    // 相邻段方向(端点取单侧)。
    glm::vec2 d_prev = (i > 0) ? glm::normalize(points[i] - points[i - 1])
                               : glm::normalize(points[1] - points[0]);
    glm::vec2 d_next = (i + 1 < n) ? glm::normalize(points[i + 1] - points[i])
                                   : d_prev;
    if (i > 0) arc += glm::length(points[i] - points[i - 1]);

    glm::vec2 tangent = d_prev + d_next;
    float tl = glm::length(tangent);
    tangent = (tl > 1e-5f) ? tangent / tl : d_prev;
    glm::vec2 normal(-tangent.y, tangent.x);
    // miter 长度补偿:normal·segment_normal 的余弦;截断防锐角尖刺。
    glm::vec2 seg_n(-d_prev.y, d_prev.x);
    float denom = glm::dot(normal, seg_n);
    float miter = (std::abs(denom) > 0.5f) ? 1.0f / denom : 2.0f;
    miter = glm::clamp(miter, -2.0f, 2.0f);
    glm::vec2 off = normal * (half_width * miter);

    for (int side = 0; side < 2; ++side) {
      MeshVertex v;
      const glm::vec2 p = points[i] + (side == 0 ? -off : off);
      v.position = {p.x, p.y, z};
      v.normal   = {0.0f, 0.0f, 1.0f};
      v.uv       = {arc * uv_per_m, static_cast<float>(side)};
      verts.push_back(v);
    }
  }
  for (size_t i = 0; i + 1 < n; ++i) {
    const uint32_t a = base + static_cast<uint32_t>(i) * 2, b = a + 1,
                   c = a + 2, d = c + 1;
    idx.insert(idx.end(), {a, b, c, b, d, c});  // 与 makeGround/makePath 同绕序
  }
}

}  // namespace hongtu::renderer
