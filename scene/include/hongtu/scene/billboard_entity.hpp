#pragma once

#include <hongtu/coordinate/ellipsoid.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <glm/glm.hpp>

namespace hongtu::scene {

using hongtu::coordinate::Ellipsoid;
using hongtu::coordinate::Geodetic;
using hongtu::coordinate::RTCOrigin;
using hongtu::coordinate::WGS84;

/// Abstract billboard entity — anything that "lives at one point in the
/// world" and should be drawn as a screen-aligned quad. Holds the per-icon
/// display state (atlas UV rect, size, color) and the polymorphic position
/// API the renderer uses to lower to RTC float32.
///
/// Subclasses differ only in how the world-space position is stored:
///   - `GeoBillboard`   — geodetic (lat/lon/alt) on an ellipsoid
///   - `LocalBillboard` — flat-world meters (dvec3)
class BillboardEntity {
 public:
  virtual ~BillboardEntity() = default;

  BillboardEntity(const BillboardEntity&)            = delete;
  BillboardEntity& operator=(const BillboardEntity&) = delete;
  BillboardEntity(BillboardEntity&&) noexcept            = default;
  BillboardEntity& operator=(BillboardEntity&&) noexcept = default;

  // Atlas slot — (uv_min.x, uv_min.y, uv_max.x, uv_max.y) packed.
  void setAtlasUV(const glm::vec4& uv) { atlas_uv_ = uv; }
  void setSizeMeters(const glm::vec2& s) { size_m_ = s; }
  void setColor(const glm::vec4& c) { color_ = c; }

  const glm::vec4& getAtlasUV()     const { return atlas_uv_; }
  const glm::vec2& getSizeMeters()  const { return size_m_; }
  const glm::vec4& getColor()       const { return color_; }

  /// Lower the world position to RTC float32 relative to `origin`. This is
  /// the only piece that differs between Geo/Local; the rest of the API is
  /// shared.
  virtual glm::vec3 getRTCPosition(const RTCOrigin& origin) const = 0;

 protected:
  BillboardEntity() = default;

  glm::vec4 atlas_uv_{0.0f, 0.0f, 1.0f, 1.0f};
  glm::vec2 size_m_{1.0f, 1.0f};
  glm::vec4 color_{1.0f, 1.0f, 1.0f, 1.0f};
};

// =============================================================================
// GeoBillboard — geodetic position. Use this for icons on the earth surface
// (危险源 / 消防栓 / 摄像头 / 传感器 in the chemical park demo).
// =============================================================================

class GeoBillboard : public BillboardEntity {
 public:
  GeoBillboard() = default;

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
// LocalBillboard — flat-world position in meters (dvec3).
// =============================================================================

class LocalBillboard : public BillboardEntity {
 public:
  LocalBillboard() = default;

  void setPosition(const glm::dvec3& p) { position_ = p; }
  const glm::dvec3& getPosition() const { return position_; }

  glm::vec3 getRTCPosition(const RTCOrigin& origin) const override;

 private:
  glm::dvec3 position_{0.0, 0.0, 0.0};
};

}  // namespace hongtu::scene
