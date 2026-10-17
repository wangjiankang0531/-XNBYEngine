#include <hongtu/scene/crowd/crowd_simulator.hpp>

#include <algorithm>
#include <cmath>

namespace hongtu::scene::crowd {

namespace {

glm::dvec3 normalizeOrZero(const glm::dvec3& v) {
  double len2 = glm::dot(v, v);
  if (len2 < 1e-12) return {0.0, 0.0, 0.0};
  return v / std::sqrt(len2);
}

glm::dvec3 clampMagnitude(const glm::dvec3& v, double max_mag) {
  double len2 = glm::dot(v, v);
  if (len2 <= max_mag * max_mag) return v;
  return v * (max_mag / std::sqrt(len2));
}

}  // namespace

glm::dvec3 CrowdSimulator::computeAcceleration(size_t i) const {
  Agent* self = agents_[i];
  glm::dvec3 pos  = self->getWorldPosition();
  glm::dvec3 vel  = self->getVelocity();
  glm::dvec3 goal = self->getTarget();

  glm::dvec3 sep(0.0), coh(0.0), align(0.0);
  int neigh_count = 0;
  glm::dvec3 neigh_center(0.0);
  glm::dvec3 neigh_vel(0.0);

  const double sep_r2 = params_.separation_radius_m * params_.separation_radius_m;
  const double coh_r2 = params_.cohesion_radius_m   * params_.cohesion_radius_m;

  for (size_t j = 0; j < agents_.size(); ++j) {
    if (j == i) continue;
    glm::dvec3 dp = agents_[j]->getWorldPosition() - pos;
    double d2 = glm::dot(dp, dp);
    if (d2 < sep_r2 && d2 > 1e-9) {
      // Push away — magnitude grows as agents overlap.
      double d = std::sqrt(d2);
      sep -= dp * ((params_.separation_radius_m - d) / d);
    }
    if (d2 < coh_r2) {
      neigh_center += agents_[j]->getWorldPosition();
      neigh_vel    += agents_[j]->getVelocity();
      ++neigh_count;
    }
  }

  if (neigh_count > 0) {
    neigh_center /= static_cast<double>(neigh_count);
    neigh_vel    /= static_cast<double>(neigh_count);
    coh   = normalizeOrZero(neigh_center - pos);
    align = normalizeOrZero(neigh_vel - vel);
  }

  // Goal steer.
  glm::dvec3 goal_dir = normalizeOrZero(goal - pos);

  // Obstacle repulse. Two cases:
  //   (a) agent OUTSIDE AABB but within obstacle_radius_m → push away from
  //       closest point on the box, strength falls off linearly with distance
  //   (b) agent INSIDE AABB(closestPoint == p, d == 0) → push out along the
  //       nearest face normal at max strength (otherwise agents can slip
  //       into / through obstacles when goal force aligns with motion)
  glm::dvec3 obs(0.0);
  for (const auto& o : obstacles_) {
    glm::dvec3 cp = o.closestPoint(pos);
    glm::dvec3 d  = pos - cp;
    double dist2  = glm::dot(d, d);
    if (dist2 > 1e-9 &&
        dist2 < params_.obstacle_radius_m * params_.obstacle_radius_m) {
      double dist = std::sqrt(dist2);
      double strength = (params_.obstacle_radius_m - dist) / params_.obstacle_radius_m;
      obs += (d / dist) * strength;
    } else if (dist2 <= 1e-9) {
      // Inside the box — push along the closest face normal.
      glm::dvec3 center = (o.min + o.max) * 0.5;
      glm::dvec3 half   = (o.max - o.min) * 0.5;
      glm::dvec3 local  = pos - center;
      glm::dvec3 m      = glm::abs(local) - half;
      // Pick the axis with the largest m (smallest distance to that face).
      int axis = 0;
      if (m.y > m.x) axis = 1;
      if (m.z > std::max(m.x, m.y)) axis = 2;
      glm::dvec3 push(0.0);
      push[axis] = local[axis] >= 0 ? 1.0 : -1.0;
      obs += push;
    }
  }

  glm::dvec3 accel =
        sep      * static_cast<double>(params_.separation_weight)
      + coh      * static_cast<double>(params_.cohesion_weight)
      + align    * static_cast<double>(params_.alignment_weight)
      + goal_dir * static_cast<double>(params_.goal_weight)
      + obs      * static_cast<double>(params_.obstacle_weight);

  return clampMagnitude(accel, static_cast<double>(params_.max_force));
}

void CrowdSimulator::update(float dt) {
  if (agents_.empty() || dt <= 0.0f) return;

  // Two-phase update: compute all accelerations against pre-update state so
  // agents don't see each other's updated positions mid-tick (jitter).
  std::vector<glm::dvec3> accel(agents_.size());
  for (size_t i = 0; i < agents_.size(); ++i) {
    accel[i] = computeAcceleration(i);
  }
  for (size_t i = 0; i < agents_.size(); ++i) {
    Agent* a = agents_[i];
    glm::dvec3 v = a->getVelocity() + accel[i] * static_cast<double>(dt);
    v = clampMagnitude(v, a->getMaxSpeedMps());
    a->setVelocity(v);
    a->integrate(v, dt);
    a->setAnimationTime(a->getAnimationTime() + dt);
  }
}

}  // namespace hongtu::scene::crowd
