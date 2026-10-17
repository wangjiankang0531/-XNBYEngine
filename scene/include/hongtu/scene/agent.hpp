#pragma once

#include <hongtu/scene/entity.hpp>
#include <hongtu/coordinate/ellipsoid.hpp>
#include <hongtu/coordinate/geodetic.hpp>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace hongtu::scene {

/// Common state shared by all crowd agents — position is delegated to
/// subclasses (Geo / Local), but velocity / target / radius / animation
/// state live on the base since they're coordinate-agnostic (all in meters /
/// scalar).
///
/// `target` is a *world-space* point in the same coordinate system as
/// `getWorldPosition()`:
///   - GeoAgent:ECEF dvec3
///   - LocalAgent:flat-world dvec3
class Agent : public Entity {
 public:
  void setVelocity(const glm::dvec3& v) { velocity_ = v; }
  const glm::dvec3& getVelocity() const  { return velocity_; }

  void setTarget(const glm::dvec3& t)    { target_ = t; }
  const glm::dvec3& getTarget() const    { return target_; }

  void setAnimationTime(float t) { anim_time_ = t; }
  float getAnimationTime() const { return anim_time_; }

  void  setRadiusMeters(float r)  { radius_m_ = r; }
  float getRadiusMeters() const   { return radius_m_; }

  void  setMaxSpeedMps(float s)   { max_speed_ = s; }
  float getMaxSpeedMps() const    { return max_speed_; }

  /// World-space position, in the subclass's coordinate system.
  virtual glm::dvec3 getWorldPosition() const = 0;

  /// Apply a velocity vector (m/s, in the subclass's world space) for `dt`
  /// seconds — moves the underlying position storage.
  virtual void integrate(const glm::dvec3& world_velocity, float dt) = 0;

  /// Heading direction (unit vector), used to orient the rendered character.
  /// Defaults to derived from velocity if non-zero; otherwise (1,0,0).
  glm::dvec3 getHeading() const {
    const double len2 = glm::dot(velocity_, velocity_);
    if (len2 < 1e-6) return {1.0, 0.0, 0.0};
    return velocity_ / std::sqrt(len2);
  }

 protected:
  Agent() = default;

  glm::dvec3 velocity_  {0.0};
  glm::dvec3 target_    {0.0};
  float      anim_time_ = 0.0f;
  float      radius_m_  = 0.4f;
  float      max_speed_ = 1.4f;     // 1.4 m/s ≈ casual walking
};

// =============================================================================
// LocalAgent — flat-world dvec3 position
// =============================================================================

class LocalAgent : public Agent {
 public:
  LocalAgent() = default;

  void setPosition(const glm::dvec3& p) { pos_ = p; }
  const glm::dvec3& getPosition() const { return pos_; }

  glm::dvec3 getWorldPosition() const override { return pos_; }
  glm::dmat4 getModelMatrix()   const override;

  void integrate(const glm::dvec3& world_velocity, float dt) override {
    pos_ += world_velocity * static_cast<double>(dt);
  }

 private:
  glm::dvec3 pos_ {0.0};
};

// =============================================================================
// GeoAgent — geodetic position, integrated in ENU tangent plane
// =============================================================================

class GeoAgent : public Agent {
 public:
  GeoAgent() = default;

  void setPositionGeo(const coordinate::Geodetic& g) { pos_geo_ = g; }
  const coordinate::Geodetic& getPositionGeo() const { return pos_geo_; }

  glm::dvec3 getWorldPosition() const override;   // ECEF
  glm::dmat4 getModelMatrix()   const override;

  /// `world_velocity` is in ECEF (m/s). We project it onto the ENU plane at
  /// the current geodetic position, accumulate position via geodetic ENU
  /// offset, and roundtrip back to Geodetic via `enuToGeodetic`. The
  /// vertical component is preserved into Geodetic.alt.
  void integrate(const glm::dvec3& world_velocity, float dt) override;

 private:
  coordinate::Geodetic pos_geo_ {};
};

}  // namespace hongtu::scene
