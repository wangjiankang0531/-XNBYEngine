#pragma once

#include <hongtu/scene/agent.hpp>

#include <glm/glm.hpp>

#include <vector>

namespace hongtu::scene::crowd {

/// Axis-aligned bounding box obstacle in world space (same coordinate system
/// as the agents — ECEF for GeoAgent, flat dvec3 for LocalAgent).
struct ObstacleAABB {
  glm::dvec3 min {0.0};
  glm::dvec3 max {0.0};

  /// Closest point on the box to `p`. If `p` is inside, returns `p` itself.
  glm::dvec3 closestPoint(const glm::dvec3& p) const {
    return glm::clamp(p, min, max);
  }
};

/// Tunable crowd parameters. Defaults aimed at "indoor pedestrian evacuation"
/// where agents are ~0.4 m radius, walk ~1.4 m/s, target an exit, and need
/// to avoid each other + obstacles smoothly.
struct CrowdParams {
  float separation_radius_m = 1.0f;   ///< boid repulsion neighbourhood
  float separation_weight   = 2.0f;
  float cohesion_radius_m   = 4.0f;   ///< pull-toward-neighbour neighbourhood
  float cohesion_weight     = 0.3f;
  float alignment_weight    = 0.5f;
  float goal_weight         = 1.5f;
  float obstacle_weight     = 3.0f;
  float obstacle_radius_m   = 1.0f;   ///< force kicks in within this distance
                                       ///<  to AABB closest point
  float max_force           = 4.0f;   ///< per-frame acceleration ceiling
};

/// Steers a flock of `Agent` instances toward their targets while keeping
/// them apart and around obstacles. Agents are non-owned observer pointers;
/// the caller is responsible for their lifetime.
///
/// Coordinate-agnostic: works in whatever frame the agent's
/// `getWorldPosition()` returns (ECEF or flat-world). For ECEF scenes the
/// boid math is still done in world space — the small distances involved
/// (< 100 m typical) keep euclidean approximation valid.
class CrowdSimulator {
 public:
  void addAgent(Agent* a)              { if (a) agents_.push_back(a); }
  void clearAgents()                    { agents_.clear(); }
  std::size_t agentCount() const        { return agents_.size(); }

  void addObstacle(const ObstacleAABB& o) { obstacles_.push_back(o); }
  void clearObstacles()                   { obstacles_.clear(); }
  std::size_t obstacleCount() const       { return obstacles_.size(); }

  void setParams(const CrowdParams& p) { params_ = p; }
  const CrowdParams& getParams() const  { return params_; }

  /// Advance the simulation by `dt` seconds. Calls `agent->integrate()` for
  /// each agent after computing per-agent acceleration.
  void update(float dt);

 private:
  glm::dvec3 computeAcceleration(size_t i) const;

  std::vector<Agent*>      agents_;
  std::vector<ObstacleAABB> obstacles_;
  CrowdParams              params_;
};

}  // namespace hongtu::scene::crowd
