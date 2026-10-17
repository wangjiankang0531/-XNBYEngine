#pragma once

#include <hongtu/coordinate/rtc_transform.hpp>
#include <glm/glm.hpp>

#include <optional>

namespace hongtu::renderer {

struct PickHit;  // ray_caster.hpp

/// Abstract base for all interactive cameras in the engine.
///
/// Subclasses provide three world-space (f64) primitives — `getEyeWorld`,
/// `getTargetWorld`, `getUpWorld` — and inherit shared view/projection matrix
/// construction. This is the API foundation that lets globe-mode and
/// local-mode code share downstream rendering pipelines: both modes produce a
/// `dmat4` view and a `mat4` projection / RTC-relative view via the same
/// methods.
///
/// State held here is the projection parameters every camera needs (FoV,
/// near/far). Subclasses that need a different projection (e.g. GlobeCamera's
/// horizon-aware near/far) override `getProjectionMatrix`.
class CameraBase {
 public:
  virtual ~CameraBase() = default;

  CameraBase(const CameraBase&)            = delete;
  CameraBase& operator=(const CameraBase&) = delete;

  // ---------------------------------------------------------------------------
  // Pure-virtual world-space (f64) primitives — subclasses define these.
  // ---------------------------------------------------------------------------

  /// Camera position in world space (e.g. ECEF for globe, plain meters for local).
  virtual glm::dvec3 getEyeWorld()    const = 0;

  /// Look-at target in world space.
  virtual glm::dvec3 getTargetWorld() const = 0;

  /// Up vector. Returned in f64 to preserve precision; downstream callers
  /// cast to vec3 if needed.
  virtual glm::dvec3 getUpWorld()     const = 0;

  // ---------------------------------------------------------------------------
  // Projection state — shared.
  // ---------------------------------------------------------------------------

  void setFovYDegrees(f32 deg)   { fovy_deg_ = deg; }
  void setNearFar(f32 n, f32 f)  { near_ = n; far_ = f; }
  f32  getFovYDegrees() const    { return fovy_deg_; }
  f32  getNear()        const    { return near_; }
  f32  getFar()         const    { return far_; }

  // ---------------------------------------------------------------------------
  // Derived matrices — composed from the three pure-virtuals above.
  // ---------------------------------------------------------------------------

  /// World-space view matrix (f64 lookAt). Identical math for every subclass.
  glm::dmat4 getViewMatrix() const;

  /// Default perspective projection from FoV/near/far. GlobeCamera overrides
  /// this for horizon-aware near/far that adapts to altitude over the globe.
  virtual glm::mat4 getProjectionMatrix(f32 aspect) const;

  /// RTC view matrix: camera & target are converted to the origin's local
  /// frame before lookAt — recovers float32 precision at large world coords.
  glm::mat4 getRTCViewMatrix(const coordinate::PrecisionOrigin& origin) const;

  // ---------------------------------------------------------------------------
  // Picking — subclasses pick against a coordinate-specific "ground" primitive
  // ---------------------------------------------------------------------------
  //
  // GlobeCamera   → ECEF sphere (WGS84 mean radius) intersection.
  // LocalCamera   → ground plane z = 0 intersection.
  //
  // Both override this to return the world-space hit point. Demos / tools
  // call `cam.pickWorld(sx, sy, w, h)` without knowing which mode they're in.

  /// Default implementation: no ground primitive known — returns nullopt.
  /// Concrete cameras override.
  virtual std::optional<PickHit>
      pickWorld(double sx, double sy,
                double screen_w, double screen_h) const;

 protected:
  CameraBase() = default;

  f32 fovy_deg_{60.0f};
  f32 near_{0.5f};
  f32 far_{1.0e7f};
};

}  // namespace hongtu::renderer
