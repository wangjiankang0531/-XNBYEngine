#pragma once

#include <hongtu/renderer/instancing/instance_buffer.hpp>  // AABB
#include <glm/glm.hpp>

#include <optional>
#include <vector>

namespace hongtu::renderer {

/// A ray in world (f64) coordinates. Direction is unit-normalised.
struct WorldRay {
  glm::dvec3 origin;
  glm::dvec3 direction;
};

/// A successful pick hit.
struct PickHit {
  glm::dvec3 point;   ///< world-space hit position
  double     t = 0.0; ///< parameter along the ray (`point = origin + dir * t`)
};

// =============================================================================
// Screen → world ray
// =============================================================================

/// Build a world-space ray from a screen pixel coordinate.
///
/// `sx`, `sy`: top-left-origin pixel coordinates (sy grows downward).
/// `screen_w`, `screen_h`: viewport size in pixels.
/// `inv_view_proj`: `inverse(proj * view)` in f64. We unproject NDC (-1..1)
/// through this to get two world-space points on the near and far planes,
/// then build the ray as `near → far`.
WorldRay screenToWorldRay(double sx, double sy,
                          double screen_w, double screen_h,
                          const glm::dmat4& inv_view_proj);

// =============================================================================
// Intersection primitives — all in world (f64) space
// =============================================================================

/// Slab method ray–AABB intersection. Returns the entry distance `t` if the
/// ray hits, otherwise `nullopt`. `t` is non-negative for hits in front of
/// the ray origin (negative-`t` exits are not returned — picking shouldn't
/// pick objects behind the camera).
std::optional<double>
intersectRayAABB(const WorldRay& ray,
                 const glm::dvec3& aabb_min,
                 const glm::dvec3& aabb_max);

/// Convenience overload — uses the engine's f32 `AABB`. Internally widens
/// to f64 for the math.
std::optional<double>
intersectRayAABB(const WorldRay& ray, const AABB& aabb);

/// Ray–sphere intersection. Returns the smaller positive root.
std::optional<double>
intersectRaySphere(const WorldRay& ray,
                   const glm::dvec3& center,
                   double radius);

/// Ray–plane intersection. `plane_normal` must be non-zero (not necessarily
/// unit). Returns `nullopt` if ray is parallel to the plane or intersects
/// behind the origin.
std::optional<double>
intersectRayPlane(const WorldRay& ray,
                  const glm::dvec3& plane_point,
                  const glm::dvec3& plane_normal);

/// Ray–triangle (Möller–Trumbore). Returns `t` of the front/back hit (no
/// backface cull — picking wants either side). `nullopt` if miss or behind.
std::optional<double>
intersectRayTriangle(const WorldRay& ray,
                     const glm::dvec3& v0,
                     const glm::dvec3& v1,
                     const glm::dvec3& v2);

/// Nearest ray–mesh hit. `positions`/`indices` are the mesh's LOCAL geometry
/// (triangle list); `model_world` places it in the ray's space. The ray is
/// transformed into local space once (cheap) and tested per triangle, so the
/// returned `t` is along the ORIGINAL world ray. `nullopt` if no triangle hit.
/// O(triangles) — fine for click-rate picking even on city-scale meshes;
/// pre-reject with `pickEntityAABB` to skip non-candidates.
std::optional<double>
intersectRayMesh(const WorldRay& ray,
                 const std::vector<glm::vec3>& positions,
                 const std::vector<uint32_t>& indices,
                 const glm::dmat4& model_world);

/// Same, but also writes the WORLD-space unit normal of the nearest-hit
/// triangle into `*out_normal` (oriented to face the incoming ray, i.e.
/// `dot(normal, ray.direction) < 0`). Used by character collision to slide
/// along walls. `out_normal` may be null.
std::optional<double>
intersectRayMesh(const WorldRay& ray,
                 const std::vector<glm::vec3>& positions,
                 const std::vector<uint32_t>& indices,
                 const glm::dmat4& model_world,
                 glm::dvec3* out_normal);

// =============================================================================
// Entity pick — ray vs a model-transformed AABB
// =============================================================================

/// Test whether `ray` hits an entity defined by `model_world` × `local_aabb`.
/// Transforms the ray into local space (cheaper than transforming the 8 AABB
/// corners) and runs slab intersection there. Returns `t` along the original
/// world-space ray.
std::optional<double>
pickEntityAABB(const WorldRay& ray,
               const glm::dmat4& model_world,
               const glm::dvec3& local_aabb_min,
               const glm::dvec3& local_aabb_max);

// =============================================================================
// Gizmo 平移拖拽投影
// =============================================================================

/// 把屏幕光标投影到"过 origin、含 axis_world、法线 plane_normal"的平面上,
/// 返回命中点沿 axis_world(无需单位化)的有符号距离(米)。ray miss 返回 0。
/// 调用方用 (t_now - t_start) 得到该轴的位移增量。
/// `inv_view_proj` 须是 `inverse(proj*view)`,且 `proj` 已抵消 Vulkan Y-flip
/// (screenToWorldRay 假设 GL 风格 Y-up NDC)。
double projectDragToAxis(const glm::dmat4& inv_view_proj,
                         double sx, double sy, double screen_w, double screen_h,
                         const glm::dvec3& origin,
                         const glm::dvec3& axis_world,
                         const glm::dvec3& plane_normal);

}  // namespace hongtu::renderer
