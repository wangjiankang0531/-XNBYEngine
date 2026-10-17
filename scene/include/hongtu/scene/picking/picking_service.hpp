#pragma once

#include <hongtu/renderer/picking/ray_caster.hpp>
#include <hongtu/renderer/camera/camera_base.hpp>

#include <glm/glm.hpp>

#include <vector>

namespace hongtu::scene {

class Entity;

/// Result of a pick query. `hit == true` iff something was actually hit; the
/// other fields are only meaningful in that case.
///
/// `entity` is a non-owning observer pointer — PickingService never extends
/// the lifetime of registered entities. The caller (scene/demo) keeps them
/// alive.
///
/// `world_hit` is in f64 world coordinates. The caller can lower it to RTC
/// via `origin.worldToLocal(world_hit)`.
struct PickResult {
  Entity*    entity     = nullptr;
  glm::dvec3 world_hit  { 0.0 };
  double     ray_t      = 0.0;
  bool       hit        = false;
};

/// Higher-level picking entry point on top of `renderer::picking::ray_caster`
/// + `cam.pickWorld`.
///
/// Lives in the scene module (not renderer) because it knows about
/// `scene::Entity`. The renderer-layer `ray_caster` stays pure math.
///
/// Usage:
///   PickingService svc;
///   svc.registerEntity(&fire_emitter, AABB{...});
///   svc.registerEntity(&building,     AABB{...});
///   ...
///   auto r = svc.pickAtScreen(mx, my, cam, w, h);
///   if (r.hit) ... // r.entity is one of the registered ones
///
/// Backing storage is a flat vector — O(N) per pick. For < ~1k entities this
/// is fine. Future scenes with 10k+ pickable objects should add a spatial
/// index here (octree per origin frame).
class PickingService {
 public:
  /// Register an entity for hit-testing. `local_aabb` is in the entity's local
  /// model space — the service transforms the ray into that space via
  /// `entity->getModelMatrix()` each pick.
  ///
  /// Re-registering the same entity replaces the AABB.
  void registerEntity(Entity* entity, const hongtu::renderer::AABB& local_aabb);

  /// Unregister an entity. No-op if not registered.
  void unregisterEntity(Entity* entity);

  /// Drop all registrations.
  void clear();

  /// How many entities are currently registered.
  std::size_t size() const { return entries_.size(); }

  /// Pick against all registered entities. Returns the nearest hit (smallest
  /// `t` along the ray), or `hit == false` if none.
  ///
  /// The world-space ray is constructed from `cam` + viewport. Entity matrices
  /// are sampled via `entity->getModelMatrix()` — make sure entities are in
  /// the same frame as the camera (i.e. lower to ECEF for GlobeCamera, or
  /// keep flat-world for LocalCamera).
  PickResult pickAtScreen(double sx, double sy,
                          const hongtu::renderer::CameraBase& cam,
                          double viewport_w, double viewport_h) const;

  /// Pick against the camera's ground primitive (plane for LocalCamera,
  /// WGS84 sphere for GlobeCamera). Doesn't consult registered entities.
  /// Useful for DrawingTool to drop points onto the ground.
  PickResult pickGround(double sx, double sy,
                        const hongtu::renderer::CameraBase& cam,
                        double viewport_w, double viewport_h) const;

 private:
  struct Entry {
    Entity*               entity;
    hongtu::renderer::AABB local_aabb;
  };
  std::vector<Entry> entries_;
};

}  // namespace hongtu::scene
