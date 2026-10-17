#pragma once

#include <hongtu/coordinate/ellipsoid.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <glm/glm.hpp>

#include <algorithm>

namespace hongtu::scene {

using hongtu::coordinate::Ellipsoid;
using hongtu::coordinate::Geodetic;
using hongtu::coordinate::RTCOrigin;
using hongtu::coordinate::WGS84;

/// Abstract shockwave entity — a spherical front expanding from a world
/// point. Holds the temporal state (start_time, speed, max radius, color)
/// + a polymorphic `getRTCCenter(origin)` for the explosion center.
///
/// `getCurrentRadius(now)` is pure CPU math — demo computes and passes
/// the radius to `ShockwavePass::Params::radius_m` each frame.
class ShockwaveEntity {
 public:
  virtual ~ShockwaveEntity() = default;

  ShockwaveEntity(const ShockwaveEntity&)            = delete;
  ShockwaveEntity& operator=(const ShockwaveEntity&) = delete;
  ShockwaveEntity(ShockwaveEntity&&) noexcept            = default;
  ShockwaveEntity& operator=(ShockwaveEntity&&) noexcept = default;

  void setStartTimeSec(float t)   { start_time_sec_ = t; }
  void setSpeedMps(float v)       { speed_mps_ = v; }
  void setMaxRadiusMeters(float r){ max_radius_m_ = r; }
  void setThicknessMeters(float t){ thickness_m_ = t; }
  void setColor(const glm::vec3& c){ color_ = c; }
  void setIntensity(float i)      { intensity_ = i; }
  void setChromaOffset(float c)   { chroma_offset_ = c; }

  float getStartTimeSec()   const { return start_time_sec_; }
  float getSpeedMps()       const { return speed_mps_; }
  float getMaxRadiusMeters()const { return max_radius_m_; }
  float getThicknessMeters()const { return thickness_m_; }
  const glm::vec3& getColor() const { return color_; }
  float getIntensity()      const { return intensity_; }
  float getChromaOffset()   const { return chroma_offset_; }

  /// 在时间 `now_sec` 时刻的当前半径(米)。clamped 到 max_radius。
  /// 时刻 < start_time 返回 0(波尚未触发)。
  float getCurrentRadius(float now_sec) const {
    float dt = now_sec - start_time_sec_;
    if (dt <= 0.0f) return 0.0f;
    return std::min(max_radius_m_, dt * speed_mps_);
  }

  /// 已扩散完毕(达到 max_radius_m_) 返回 true。
  bool isComplete(float now_sec) const {
    return getCurrentRadius(now_sec) >= max_radius_m_;
  }

  /// Lower the world center point to RTC float32.
  virtual glm::vec3 getRTCCenter(const RTCOrigin& origin) const = 0;

 protected:
  ShockwaveEntity() = default;
  float     start_time_sec_ = 0.0f;
  float     speed_mps_      = 10.0f;
  float     max_radius_m_   = 20.0f;
  float     thickness_m_    = 0.6f;
  glm::vec3 color_          = {1.0f, 0.85f, 0.6f};
  float     intensity_      = 2.5f;
  float     chroma_offset_  = 0.0f;
};

// =============================================================================
// GeoShockwave / LocalShockwave — 同 EmitterEntity / VolumetricEntity 模式
// =============================================================================

class GeoShockwave : public ShockwaveEntity {
 public:
  GeoShockwave() = default;
  void setCenter(const Geodetic& g) { center_ = g; }
  const Geodetic& getCenter() const { return center_; }

  glm::vec3 getRTCCenter(const RTCOrigin& origin) const override;
  glm::vec3 getRTCCenter(const RTCOrigin& origin,
                         const Ellipsoid& ellipsoid) const;

 private:
  Geodetic center_{0.0, 0.0, 0.0};
};

class LocalShockwave : public ShockwaveEntity {
 public:
  LocalShockwave() = default;
  void setCenter(const glm::dvec3& p) { center_ = p; }
  const glm::dvec3& getCenter() const { return center_; }

  glm::vec3 getRTCCenter(const RTCOrigin& origin) const override;

 private:
  glm::dvec3 center_{0.0, 0.0, 0.0};
};

}  // namespace hongtu::scene
