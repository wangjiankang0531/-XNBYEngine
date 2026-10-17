#pragma once

#include <hongtu/coordinate/ellipsoid.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/renderer/effects/particle_system.hpp>
#include <glm/glm.hpp>

namespace hongtu::scene {

using hongtu::coordinate::Ellipsoid;
using hongtu::coordinate::Geodetic;
using hongtu::coordinate::RTCOrigin;
using hongtu::coordinate::WGS84;
using hongtu::renderer::EmitterDesc;

/// Abstract particle emitter entity — anything that "spawns particles at one
/// world point". Holds the renderer-side `EmitterDesc` (preset / rate /
/// shape) plus a polymorphic `getRTCPosition(origin)` for the spawn center.
///
/// Each frame the demo does:
///   1. `vec3 c = emitter_entity->getRTCPosition(origin)`
///   2. `emitter_entity->getDesc().center = c`(or via setter)
///   3. `particle_system.setEmitter(emitter_entity->getDesc())`
///   4. `particle_system.update(dt)` + `buildInstanceBuffer(...)`
///
/// Splits the world-position concern (Geo/Local) from the renderer concern
/// (only knows RTC vec3) — same dual-mode pattern as PolylineEntity /
/// BillboardEntity.
class EmitterEntity {
 public:
  virtual ~EmitterEntity() = default;

  EmitterEntity(const EmitterEntity&)            = delete;
  EmitterEntity& operator=(const EmitterEntity&) = delete;
  EmitterEntity(EmitterEntity&&) noexcept            = default;
  EmitterEntity& operator=(EmitterEntity&&) noexcept = default;

  void setDesc(const EmitterDesc& d) { desc_ = d; }
  const EmitterDesc& getDesc() const { return desc_; }
  EmitterDesc&       getDesc()       { return desc_; }

  /// Lower the world position to RTC float32 relative to `origin`.
  /// (Same contract as PolylineEntity / BillboardEntity.)
  virtual glm::vec3 getRTCPosition(const RTCOrigin& origin) const = 0;

 protected:
  EmitterEntity() = default;
  EmitterDesc desc_;
};

// =============================================================================
// GeoEmitter — geodetic position. Use for outdoor fire / smoke / leak source
// placed on the earth.
// =============================================================================

class GeoEmitter : public EmitterEntity {
 public:
  GeoEmitter() = default;

  void setPosition(const Geodetic& g) { position_ = g; }
  const Geodetic& getPosition() const { return position_; }

  glm::vec3 getRTCPosition(const RTCOrigin& origin) const override;

  /// Same as `getRTCPosition` with explicit ellipsoid.
  glm::vec3 getRTCPosition(const RTCOrigin& origin,
                          const Ellipsoid& ellipsoid) const;

 private:
  Geodetic position_{0.0, 0.0, 0.0};
};

// =============================================================================
// LocalEmitter — flat-world position in meters (dvec3).
// =============================================================================

class LocalEmitter : public EmitterEntity {
 public:
  LocalEmitter() = default;

  void setPosition(const glm::dvec3& p) { position_ = p; }
  const glm::dvec3& getPosition() const { return position_; }

  glm::vec3 getRTCPosition(const RTCOrigin& origin) const override;

 private:
  glm::dvec3 position_{0.0, 0.0, 0.0};
};

}  // namespace hongtu::scene
