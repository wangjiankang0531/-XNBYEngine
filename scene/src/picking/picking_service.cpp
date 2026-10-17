#include <hongtu/scene/picking/picking_service.hpp>
#include <hongtu/scene/entity.hpp>

#include <algorithm>
#include <limits>

namespace hongtu::scene {

void PickingService::registerEntity(Entity* entity, const hongtu::renderer::AABB& local_aabb) {
  if (!entity) return;
  for (auto& e : entries_) {
    if (e.entity == entity) {
      e.local_aabb = local_aabb;
      return;
    }
  }
  entries_.push_back({entity, local_aabb});
}

void PickingService::unregisterEntity(Entity* entity) {
  entries_.erase(
      std::remove_if(entries_.begin(), entries_.end(),
                     [entity](const Entry& e) { return e.entity == entity; }),
      entries_.end());
}

void PickingService::clear() { entries_.clear(); }

namespace {

hongtu::renderer::WorldRay buildRayFromCamera(double sx, double sy,
                                              const hongtu::renderer::CameraBase& cam,
                                              double viewport_w, double viewport_h) {
  const double aspect = viewport_w / std::max(1.0, viewport_h);
  const glm::dmat4 proj   = glm::dmat4(cam.getProjectionMatrix(static_cast<float>(aspect)));
  const glm::dmat4 view   = cam.getViewMatrix();
  const glm::dmat4 inv_vp = glm::inverse(proj * view);
  return hongtu::renderer::screenToWorldRay(sx, sy, viewport_w, viewport_h, inv_vp);
}

}  // namespace

PickResult PickingService::pickAtScreen(double sx, double sy,
                                        const hongtu::renderer::CameraBase& cam,
                                        double viewport_w, double viewport_h) const {
  PickResult result;
  if (entries_.empty()) return result;

  const hongtu::renderer::WorldRay ray = buildRayFromCamera(sx, sy, cam, viewport_w, viewport_h);

  double best_t = std::numeric_limits<double>::infinity();
  Entity* best_entity = nullptr;

  for (const auto& e : entries_) {
    const glm::dmat4 model = e.entity->getModelMatrix();
    const glm::dvec3 lo(e.local_aabb.min_point);
    const glm::dvec3 hi(e.local_aabb.max_point);
    auto t = hongtu::renderer::pickEntityAABB(ray, model, lo, hi);
    if (t.has_value() && *t < best_t) {
      best_t = *t;
      best_entity = e.entity;
    }
  }

  if (best_entity) {
    result.entity    = best_entity;
    result.ray_t     = best_t;
    result.world_hit = ray.origin + ray.direction * best_t;
    result.hit       = true;
  }
  return result;
}

PickResult PickingService::pickGround(double sx, double sy,
                                      const hongtu::renderer::CameraBase& cam,
                                      double viewport_w, double viewport_h) const {
  PickResult result;
  auto hit = cam.pickWorld(sx, sy, viewport_w, viewport_h);
  if (!hit.has_value()) return result;
  result.world_hit = hit->point;
  result.ray_t     = hit->t;
  result.hit       = true;
  return result;
}

}  // namespace hongtu::scene
