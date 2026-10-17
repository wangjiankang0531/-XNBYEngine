#include <hongtu/renderer/picking/ray_caster.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace hongtu::renderer {

WorldRay screenToWorldRay(double sx, double sy,
                          double screen_w, double screen_h,
                          const glm::dmat4& inv_view_proj) {
  // Convert pixel coords to NDC (-1..1). Y is flipped: pixel Y grows down,
  // NDC Y grows up.
  const double ndc_x =  (sx / screen_w) * 2.0 - 1.0;
  const double ndc_y = -((sy / screen_h) * 2.0 - 1.0);

  // Unproject near and far points (NDC z = -1 and +1 in OpenGL conventions,
  // 0 and 1 in Vulkan). We pass `inv(proj * view)` so the conventions are
  // already baked into the projection — we just need to pick two distinct
  // depths and let the matrix handle it.
  glm::dvec4 near_h = inv_view_proj * glm::dvec4(ndc_x, ndc_y, 0.0, 1.0);
  glm::dvec4 far_h  = inv_view_proj * glm::dvec4(ndc_x, ndc_y, 1.0, 1.0);

  if (near_h.w == 0.0 || far_h.w == 0.0) {
    // Degenerate matrix — return a sentinel ray along +Z so callers can
    // detect via direction != zero check.
    return {{0.0, 0.0, 0.0}, {0.0, 0.0, 1.0}};
  }

  glm::dvec3 near_p = glm::dvec3(near_h) / near_h.w;
  glm::dvec3 far_p  = glm::dvec3(far_h)  / far_h.w;

  WorldRay r;
  r.origin    = near_p;
  glm::dvec3 d = far_p - near_p;
  double len_sq = glm::dot(d, d);
  if (len_sq < 1e-30) {
    r.direction = {0.0, 0.0, 1.0};
  } else {
    r.direction = d * (1.0 / std::sqrt(len_sq));
  }
  return r;
}

std::optional<double>
intersectRayAABB(const WorldRay& ray,
                 const glm::dvec3& aabb_min,
                 const glm::dvec3& aabb_max) {
  // Slab method. For each axis, compute the intersections with the two
  // slabs perpendicular to that axis and shrink [tmin, tmax].
  double tmin = -std::numeric_limits<double>::infinity();
  double tmax =  std::numeric_limits<double>::infinity();
  for (int i = 0; i < 3; ++i) {
    double dir = ray.direction[i];
    double org = ray.origin[i];
    double lo  = aabb_min[i];
    double hi  = aabb_max[i];
    if (std::abs(dir) < 1e-12) {
      // Ray parallel to this slab — no hit unless origin is within it.
      if (org < lo || org > hi) return std::nullopt;
      continue;
    }
    double inv = 1.0 / dir;
    double t0 = (lo - org) * inv;
    double t1 = (hi - org) * inv;
    if (t0 > t1) std::swap(t0, t1);
    tmin = std::max(tmin, t0);
    tmax = std::min(tmax, t1);
    if (tmin > tmax) return std::nullopt;
  }
  if (tmax < 0.0) return std::nullopt;       // box entirely behind origin
  return tmin >= 0.0 ? tmin : tmax;          // entry point if outside,
                                              // exit if origin is inside
}

std::optional<double>
intersectRayAABB(const WorldRay& ray, const AABB& aabb) {
  return intersectRayAABB(ray,
      glm::dvec3(aabb.min_point.x, aabb.min_point.y, aabb.min_point.z),
      glm::dvec3(aabb.max_point.x, aabb.max_point.y, aabb.max_point.z));
}

std::optional<double>
intersectRaySphere(const WorldRay& ray,
                   const glm::dvec3& center,
                   double radius) {
  glm::dvec3 oc = ray.origin - center;
  double b = glm::dot(oc, ray.direction);
  double c = glm::dot(oc, oc) - radius * radius;
  if (c > 0.0 && b > 0.0) return std::nullopt;  // origin outside, pointing away
  double disc = b * b - c;
  if (disc < 0.0) return std::nullopt;
  double sq = std::sqrt(disc);
  double t = -b - sq;
  if (t < 0.0) t = -b + sq;
  return t >= 0.0 ? std::optional<double>(t) : std::nullopt;
}

std::optional<double>
intersectRayPlane(const WorldRay& ray,
                  const glm::dvec3& plane_point,
                  const glm::dvec3& plane_normal) {
  double denom = glm::dot(plane_normal, ray.direction);
  if (std::abs(denom) < 1e-12) return std::nullopt;     // parallel
  double t = glm::dot(plane_point - ray.origin, plane_normal) / denom;
  return t >= 0.0 ? std::optional<double>(t) : std::nullopt;
}

std::optional<double>
intersectRayTriangle(const WorldRay& ray, const glm::dvec3& v0,
                     const glm::dvec3& v1, const glm::dvec3& v2) {
  // Möller–Trumbore，无背面剔除。
  const glm::dvec3 e1 = v1 - v0;
  const glm::dvec3 e2 = v2 - v0;
  const glm::dvec3 pv = glm::cross(ray.direction, e2);
  const double det = glm::dot(e1, pv);
  if (std::abs(det) < 1e-12) return std::nullopt;  // 平行
  const double inv = 1.0 / det;
  const glm::dvec3 tv = ray.origin - v0;
  const double u = glm::dot(tv, pv) * inv;
  if (u < 0.0 || u > 1.0) return std::nullopt;
  const glm::dvec3 qv = glm::cross(tv, e1);
  const double v = glm::dot(ray.direction, qv) * inv;
  if (v < 0.0 || u + v > 1.0) return std::nullopt;
  const double t = glm::dot(e2, qv) * inv;
  return t > 1e-9 ? std::optional<double>(t) : std::nullopt;
}

std::optional<double>
intersectRayMesh(const WorldRay& ray, const std::vector<glm::vec3>& positions,
                 const std::vector<uint32_t>& indices,
                 const glm::dmat4& model_world) {
  return intersectRayMesh(ray, positions, indices, model_world, nullptr);
}

std::optional<double>
intersectRayMesh(const WorldRay& ray, const std::vector<glm::vec3>& positions,
                 const std::vector<uint32_t>& indices,
                 const glm::dmat4& model_world, glm::dvec3* out_normal) {
  if (positions.empty() || indices.size() < 3) return std::nullopt;
  // 把世界射线变到模型局部空间(一次逆变换),三角顶点保持局部 —— 比每个
  // 顶点变到世界便宜得多(城市级 50 万三角)。返回沿原世界射线的 t。
  const glm::dmat4 inv = glm::inverse(model_world);
  WorldRay lr;
  lr.origin = glm::dvec3(inv * glm::dvec4(ray.origin, 1.0));
  // 方向用 0-w 变换;不归一化,使局部 t 与世界 t 同尺度(刚性+均匀缩放成立)。
  lr.direction = glm::dvec3(inv * glm::dvec4(ray.direction, 0.0));
  double best = std::numeric_limits<double>::infinity();
  size_t best_i = 0;
  for (size_t i = 0; i + 2 < indices.size(); i += 3) {
    const auto a = glm::dvec3(positions[indices[i]]);
    const auto b = glm::dvec3(positions[indices[i + 1]]);
    const auto c = glm::dvec3(positions[indices[i + 2]]);
    if (auto t = intersectRayTriangle(lr, a, b, c)) {
      if (*t < best) { best = *t; best_i = i; }
    }
  }
  if (!std::isfinite(best)) return std::nullopt;
  if (out_normal) {
    // 命中三角的世界法线(法线矩阵 = 上 3×3 逆转置),朝向入射射线一侧。
    const auto a = glm::dvec3(positions[indices[best_i]]);
    const auto b = glm::dvec3(positions[indices[best_i + 1]]);
    const auto c = glm::dvec3(positions[indices[best_i + 2]]);
    const glm::dmat3 nmat = glm::transpose(glm::dmat3(inv));
    glm::dvec3 n = nmat * glm::cross(b - a, c - a);
    const double l = glm::length(n);
    n = l > 1e-12 ? n / l : glm::dvec3(0, 0, 1);
    if (glm::dot(n, ray.direction) > 0.0) n = -n;
    *out_normal = n;
  }
  return best;
}

std::optional<double>
pickEntityAABB(const WorldRay& ray,
               const glm::dmat4& model_world,
               const glm::dvec3& local_aabb_min,
               const glm::dvec3& local_aabb_max) {
  // Transform ray into local space and do AABB test there.
  glm::dmat4 inv = glm::inverse(model_world);
  glm::dvec4 lo  = inv * glm::dvec4(ray.origin, 1.0);
  glm::dvec4 ld  = inv * glm::dvec4(ray.direction, 0.0);

  WorldRay local;
  local.origin    = glm::dvec3(lo);
  glm::dvec3 d    = glm::dvec3(ld);
  double len_sq = glm::dot(d, d);
  if (len_sq < 1e-30) return std::nullopt;
  double inv_len = 1.0 / std::sqrt(len_sq);
  local.direction = d * inv_len;

  auto t_local = intersectRayAABB(local, local_aabb_min, local_aabb_max);
  if (!t_local) return std::nullopt;
  // Local-space `t` is in units of the local direction's original length —
  // we re-normalised, so the world-space `t` is `t_local / inv_len` =
  // `t_local * |d|` where `|d|` is the local direction magnitude.
  return *t_local / inv_len;
}

double projectDragToAxis(const glm::dmat4& inv_view_proj,
                         double sx, double sy, double screen_w, double screen_h,
                         const glm::dvec3& origin,
                         const glm::dvec3& axis_world,
                         const glm::dvec3& plane_normal) {
  WorldRay ray = screenToWorldRay(sx, sy, screen_w, screen_h, inv_view_proj);
  auto t = intersectRayPlane(ray, origin, plane_normal);
  if (!t.has_value()) return 0.0;
  glm::dvec3 hit = ray.origin + ray.direction * (*t);
  double alen = glm::length(axis_world);
  if (alen < 1e-12) return 0.0;
  return glm::dot(hit - origin, axis_world / alen);
}

}  // namespace hongtu::renderer
