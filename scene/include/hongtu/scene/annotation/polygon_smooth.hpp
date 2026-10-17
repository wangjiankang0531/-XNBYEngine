#pragma once

#include <hongtu/coordinate/geodetic.hpp>

#include <glm/glm.hpp>

#include <cmath>
#include <vector>

namespace hongtu::scene {

// =============================================================================
// Chaikin corner-cutting for CLOSED polygon rings — render-time smoothing for
// the AnnotationStyle::corner_smooth option. Each iteration replaces every
// vertex with two points at 1/4 and 3/4 of its adjacent edges, so corners get
// progressively rounded while the shape stays inside the original hull.
// Control vertices are untouched — smoothing happens on a copy at draw time,
// so editing (vertex drag / inspector rows) keeps operating on the originals.
//
// Header-only, no Vulkan dependency — unit-testable like triangulateFan.
// =============================================================================

/// Closed-ring Chaikin on RTC/local positions. `iterations` is clamped to
/// [0, 5] (each doubles the vertex count). Rings with < 3 vertices pass
/// through unchanged.
inline std::vector<glm::vec3> smoothPolygonChaikin(
    const std::vector<glm::vec3>& ring, int iterations) {
  if (ring.size() < 3 || iterations <= 0) return ring;
  if (iterations > 5) iterations = 5;
  std::vector<glm::vec3> cur = ring;
  for (int it = 0; it < iterations; ++it) {
    std::vector<glm::vec3> next;
    next.reserve(cur.size() * 2);
    for (size_t i = 0; i < cur.size(); ++i) {
      const glm::vec3& a = cur[i];
      const glm::vec3& b = cur[(i + 1) % cur.size()];
      next.push_back(a * 0.75f + b * 0.25f);
      next.push_back(a * 0.25f + b * 0.75f);
    }
    cur = std::move(next);
  }
  return cur;
}

/// Open-polyline Chaikin on RTC/local positions (折线圆滑,端点保持)。与闭
/// 环版的区别:不 wrap,首尾点原样保留,只切内段拐角。`iterations` clamp
/// [0,5]。< 3 点直接原样返回。
inline std::vector<glm::vec3> smoothPolylineChaikin(
    const std::vector<glm::vec3>& pts, int iterations) {
  if (pts.size() < 3 || iterations <= 0) return pts;
  if (iterations > 5) iterations = 5;
  std::vector<glm::vec3> cur = pts;
  for (int it = 0; it < iterations; ++it) {
    std::vector<glm::vec3> next;
    next.reserve(cur.size() * 2);
    next.push_back(cur.front());
    for (size_t i = 0; i + 1 < cur.size(); ++i) {
      const glm::vec3& a = cur[i];
      const glm::vec3& b = cur[i + 1];
      next.push_back(a * 0.75f + b * 0.25f);
      next.push_back(a * 0.25f + b * 0.75f);
    }
    next.push_back(cur.back());
    cur = std::move(next);
  }
  return cur;
}

/// Closed-ring Chaikin on geodetic boundaries (water bodies). Component-wise
/// lerp on lat/lon/alt — fine for local footprints; NOT antimeridian-safe
/// (a ring spanning lon ±π would lerp the long way around).
inline std::vector<coordinate::Geodetic> smoothPolygonChaikin(
    const std::vector<coordinate::Geodetic>& ring, int iterations) {
  if (ring.size() < 3 || iterations <= 0) return ring;
  if (iterations > 5) iterations = 5;
  auto lerp = [](const coordinate::Geodetic& a, const coordinate::Geodetic& b,
                 double t) {
    coordinate::Geodetic g;
    g.lat = a.lat + (b.lat - a.lat) * t;
    g.lon = a.lon + (b.lon - a.lon) * t;
    g.alt = a.alt + (b.alt - a.alt) * t;
    return g;
  };
  std::vector<coordinate::Geodetic> cur = ring;
  for (int it = 0; it < iterations; ++it) {
    std::vector<coordinate::Geodetic> next;
    next.reserve(cur.size() * 2);
    for (size_t i = 0; i < cur.size(); ++i) {
      const auto& a = cur[i];
      const auto& b = cur[(i + 1) % cur.size()];
      next.push_back(lerp(a, b, 0.25));
      next.push_back(lerp(a, b, 0.75));
    }
    cur = std::move(next);
  }
  return cur;
}

// =============================================================================
// 圆形区域 —— 在中心周围生成正多边形环逼近圆。圆形区域本质就是一条多边形环
// (顶点数足够多即视觉为圆),因此渲染/填充/围栏/描边全部复用多边形管线;
// 编辑器只需存「中心 + 半径」并在半径变化时重建环。
// =============================================================================

/// 形心(环顶点均值)—— 圆环的形心精确等于圆心,据此重建环。
inline glm::dvec3 ringCentroidLocal(const std::vector<glm::dvec3>& ring) {
  if (ring.empty()) return glm::dvec3(0.0);
  glm::dvec3 c(0.0);
  for (const auto& p : ring) c += p;
  return c / static_cast<double>(ring.size());
}

/// 本地坐标圆环:在水平面(XY)上绕 center 生成 `segments` 个等角顶点,z 取
/// center.z。segments 夹到 [8, 256]。
inline std::vector<glm::dvec3> makeCircleRingLocal(const glm::dvec3& center,
                                                   double radius, int segments) {
  if (segments < 8) segments = 8;
  if (segments > 256) segments = 256;
  if (radius < 1e-4) radius = 1e-4;
  std::vector<glm::dvec3> ring;
  ring.reserve(segments);
  const double two_pi = 6.283185307179586;
  for (int i = 0; i < segments; ++i) {
    const double a = two_pi * static_cast<double>(i) / segments;
    ring.emplace_back(center.x + radius * std::cos(a),
                      center.y + radius * std::sin(a), center.z);
  }
  return ring;
}

/// 大地坐标圆环:绕 center 按米制半径在东/北方向铺环(小范围足够精确)。
inline std::vector<coordinate::Geodetic> makeCircleRingGeo(
    const coordinate::Geodetic& center, double radius, int segments) {
  if (segments < 8) segments = 8;
  if (segments > 256) segments = 256;
  if (radius < 1e-4) radius = 1e-4;
  constexpr double kR = 6378137.0;
  const double cl = std::cos(center.lat);
  std::vector<coordinate::Geodetic> ring;
  ring.reserve(segments);
  const double two_pi = 6.283185307179586;
  for (int i = 0; i < segments; ++i) {
    const double a = two_pi * static_cast<double>(i) / segments;
    const double east  = radius * std::cos(a);
    const double north = radius * std::sin(a);
    coordinate::Geodetic g;
    g.lat = center.lat + north / kR;
    g.lon = center.lon + ((std::abs(cl) > 1e-9) ? east / (kR * cl) : 0.0);
    g.alt = center.alt;
    ring.push_back(g);
  }
  return ring;
}

}  // namespace hongtu::scene
