#pragma once

#include <hongtu/coordinate/dispersion/gaussian_plume.hpp>
#include <hongtu/coordinate/ellipsoid.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <glm/glm.hpp>

#include <memory>

namespace hongtu::scene {

using hongtu::coordinate::Ellipsoid;
using hongtu::coordinate::Geodetic;
using hongtu::coordinate::RTCOrigin;
using hongtu::coordinate::WGS84;
using hongtu::coordinate::dispersion::DensityGrid;

/// Abstract volumetric entity — a 3D density field anchored to a world
/// point. Renderer side(`VolumetricPass`)consumes a `DensityGrid` (3D
/// texture) + RTC bbox extents to ray-march through it.
///
/// Same dual-mode pattern as PolylineEntity / BillboardEntity /
/// EmitterEntity. Each frame:
///   1. `vec3 c = volumetric->getRTCCenter(origin)` — center in RTC
///   2. `vec3 ext = volumetric->getExtentMeters()` — half-extent (meters)
///   3. Build AABB: `[c - ext, c + ext]`
///   4. Pass to `VolumetricPass::record(...)` with the density grid
///
/// **Density grid ownership**: held by the entity via `std::shared_ptr`
/// so multiple entities can share the same grid texture (cheap when the
/// same plume is referenced from multiple visualizations).
class VolumetricEntity {
 public:
  virtual ~VolumetricEntity() = default;

  VolumetricEntity(const VolumetricEntity&)            = delete;
  VolumetricEntity& operator=(const VolumetricEntity&) = delete;
  VolumetricEntity(VolumetricEntity&&) noexcept            = default;
  VolumetricEntity& operator=(VolumetricEntity&&) noexcept = default;

  void setExtentMeters(const glm::vec3& ext) { extent_m_ = ext; }
  void setDensityGrid(std::shared_ptr<DensityGrid> g) { grid_ = std::move(g); }
  void setColor(const glm::vec3& c)         { color_ = c; }
  void setDensityScale(float s)             { density_scale_ = s; }

  const glm::vec3&            getExtentMeters() const { return extent_m_; }
  const std::shared_ptr<DensityGrid>& getDensityGrid() const { return grid_; }
  const glm::vec3&            getColor()         const { return color_; }
  float                       getDensityScale()  const { return density_scale_; }

  /// Lower the world center point to RTC float32.
  virtual glm::vec3 getRTCCenter(const RTCOrigin& origin) const = 0;

 protected:
  VolumetricEntity() = default;
  glm::vec3 extent_m_      = {5.0f, 5.0f, 2.5f};
  glm::vec3 color_         = {0.55f, 0.65f, 0.4f};   // 默认毒气黄绿
  float     density_scale_ = 1.0f;
  std::shared_ptr<DensityGrid> grid_;
};

// =============================================================================
// GeoVolumetric — 几何中心存 geodetic。Use for outdoor plumes / gas
// dispersion anchored to a lat/lon.
// =============================================================================

class GeoVolumetric : public VolumetricEntity {
 public:
  GeoVolumetric() = default;

  void setCenter(const Geodetic& g) { center_ = g; }
  const Geodetic& getCenter() const { return center_; }

  glm::vec3 getRTCCenter(const RTCOrigin& origin) const override;
  glm::vec3 getRTCCenter(const RTCOrigin& origin,
                         const Ellipsoid& ellipsoid) const;

 private:
  Geodetic center_{0.0, 0.0, 0.0};
};

// =============================================================================
// LocalVolumetric — 几何中心存 dvec3 平面坐标。
// =============================================================================

class LocalVolumetric : public VolumetricEntity {
 public:
  LocalVolumetric() = default;

  void setCenter(const glm::dvec3& p) { center_ = p; }
  const glm::dvec3& getCenter() const { return center_; }

  glm::vec3 getRTCCenter(const RTCOrigin& origin) const override;

 private:
  glm::dvec3 center_{0.0, 0.0, 0.0};
};

}  // namespace hongtu::scene
