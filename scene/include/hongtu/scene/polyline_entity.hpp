#pragma once

#include <hongtu/coordinate/ellipsoid.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <glm/glm.hpp>

#include <vector>

namespace hongtu::scene {

using hongtu::coordinate::Ellipsoid;
using hongtu::coordinate::Geodetic;
using hongtu::coordinate::RTCOrigin;
using hongtu::coordinate::WGS84;

/// Abstract polyline entity — a series of world-space points that, given a
/// `PrecisionOrigin`, lowers itself into the RTC float32 space the
/// `PolylineRenderer` consumes.
///
/// Subclasses store their vertices in whichever coordinate system makes sense
/// for the scene type:
///   - `GeoPolyline` stores `Geodetic` (lat/lon/alt) — best for routes /
///     pipelines drawn on the earth surface.
///   - `LocalPolyline` stores `dvec3` meters — best for indoor / CAD scenes.
///
/// Both share the same `getRTCPoints(origin)` contract so a single
/// `PolylineRenderer::record()` call can mix both kinds of polylines in one
/// frame — the geodetic-vs-flat split lives entirely in the entity.
class PolylineEntity {
 public:
  virtual ~PolylineEntity() = default;

  PolylineEntity(const PolylineEntity&)            = delete;
  PolylineEntity& operator=(const PolylineEntity&) = delete;
  PolylineEntity(PolylineEntity&&) noexcept            = default;
  PolylineEntity& operator=(PolylineEntity&&) noexcept = default;

  // Per-polyline display attributes. Stored on the base so the demo code
  // can `auto& p : polylines` and read them uniformly. Rendering happens
  // through `PolylineRenderer::PolylineDraw` which mirrors these.
  void setColor(const glm::vec4& c) { color_ = c; }
  void setWidthMeters(float w)      { width_m_ = w; }
  void setClosed(bool b)            { closed_ = b; }
  void setArrowHead(bool b)         { arrow_head_ = b; }
  void setArrowTail(bool b)         { arrow_tail_ = b; }
  void setArrowSizeMeters(float s)  { arrow_size_m_ = s; }
  void setDashed(bool b)            { dashed_ = b; }
  void setDashSizeMeters(float s)   { dash_size_m_ = s; }
  void setDashGapRatio(float r)     { dash_gap_ratio_ = r; }

  const glm::vec4& getColor()         const { return color_; }
  float            getWidthMeters()   const { return width_m_; }
  bool             isClosed()         const { return closed_; }
  bool             hasArrowHead()     const { return arrow_head_; }
  bool             hasArrowTail()     const { return arrow_tail_; }
  float            getArrowSizeMeters() const { return arrow_size_m_; }
  bool             isDashed()         const { return dashed_; }
  float            getDashSizeMeters() const { return dash_size_m_; }
  float            getDashGapRatio()  const { return dash_gap_ratio_; }

  /// Lower the polyline vertices into RTC float32 space relative to `origin`.
  /// The base assumes `origin` is the same precision origin used by the
  /// camera and the rest of the scene. Subclasses do the geodetic-or-flat
  /// conversion to ECEF (or just subtraction for flat) before casting to
  /// float32.
  virtual std::vector<glm::vec3>
      getRTCPoints(const RTCOrigin& origin) const = 0;

 protected:
  PolylineEntity() = default;

  glm::vec4 color_{1.0f, 1.0f, 1.0f, 1.0f};
  float     width_m_ = 1.0f;
  bool      closed_  = false;
  bool      arrow_head_     = false;
  bool      arrow_tail_     = false;
  float     arrow_size_m_   = 0.0f;   // 0 = default (3 × width_m_)
  bool      dashed_         = false;
  float     dash_size_m_    = 1.0f;
  float     dash_gap_ratio_ = 0.5f;
};

// =============================================================================
// GeoPolyline — vertices stored as geodetic coordinates. Each point is
// converted to ECEF on demand and then to RTC float32 against the supplied
// origin. Use this for routes / pipelines / boundaries painted on the earth.
// =============================================================================

class GeoPolyline : public PolylineEntity {
 public:
  GeoPolyline() = default;

  /// Append a point. Lat/lon are radians; alt is meters above the ellipsoid.
  void addPoint(const Geodetic& g) { points_.push_back(g); }

  /// Replace all points in one shot.
  void setPoints(std::vector<Geodetic> pts) { points_ = std::move(pts); }

  const std::vector<Geodetic>& getPoints() const { return points_; }

  /// Convert geodetic vertices → ECEF → RTC float32 against `origin`.
  std::vector<glm::vec3> getRTCPoints(const RTCOrigin& origin) const override;

  /// Same as `getRTCPoints` but with an explicit ellipsoid (defaults to WGS84).
  std::vector<glm::vec3> getRTCPoints(const RTCOrigin& origin,
                                       const Ellipsoid& ellipsoid) const;

 private:
  std::vector<Geodetic> points_;
};

// =============================================================================
// LocalPolyline — vertices stored as flat-world meters (dvec3). Conversion
// is just `worldToLocal(origin)` per point. No ellipsoid involved.
// =============================================================================

class LocalPolyline : public PolylineEntity {
 public:
  LocalPolyline() = default;

  void addPoint(const glm::dvec3& p) { points_.push_back(p); }
  void setPoints(std::vector<glm::dvec3> pts) { points_ = std::move(pts); }

  const std::vector<glm::dvec3>& getPoints() const { return points_; }

  std::vector<glm::vec3> getRTCPoints(const RTCOrigin& origin) const override;

 private:
  std::vector<glm::dvec3> points_;
};

}  // namespace hongtu::scene
