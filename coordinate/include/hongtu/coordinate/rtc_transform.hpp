#pragma once

#include <hongtu/coordinate/geodetic.hpp>
#include <glm/glm.hpp>

namespace hongtu::coordinate {

/// RTC (Relative-to-Center) transform: solves GPU float32 precision issues
/// at large distances from origin by computing positions relative to a nearby
/// center point on the CPU (float64) and passing float32 offsets to the GPU.
struct RTCTransform {
  glm::dvec3 center_ecef;        ///< Center point in ECEF (float64)
  Geodetic center_geodetic;       ///< Center in geodetic coordinates
  glm::mat4 rtc_matrix;           ///< float32 matrix for GPU (relative to center)
};

/// Generic high-precision origin manager. Maintains a world-space (float64)
/// reference origin and converts to/from float32 offsets suitable for GPU
/// rendering. The math is coordinate-agnostic: works for ECEF (digital earth),
/// flat-world meters (CAD/local scenes), or any other f64 world frame.
///
/// Earth-flavoured aliases (`setOriginECEF`, `ecefToRTC`, ...) and the
/// `RTCOrigin` typedef are retained so legacy globe code keeps compiling.
class PrecisionOrigin {
 public:
  PrecisionOrigin() = default;

  // ---------------------------------------------------------------------------
  // Generic API — coordinate-system agnostic
  // ---------------------------------------------------------------------------

  /// Set the world-space origin. Does NOT update `origin_geo_` — callers in
  /// earth contexts should use `setOriginECEF/Geodetic` instead so the
  /// geodetic mirror is refreshed for logging/diagnostics.
  void setOrigin(const glm::dvec3& world);

  /// Get the world-space origin.
  const glm::dvec3& getOrigin() const { return origin_ecef_; }

  /// Convert a world-space (f64) position to a local (f32) offset relative
  /// to the origin. Key operation that recovers float32 precision near the
  /// origin even when the world coords are huge (e.g. ECEF ≈ 6.4e6 m).
  glm::vec3 worldToLocal(const glm::dvec3& world) const;

  /// Convert a local (f32) offset back to a world-space (f64) position.
  glm::dvec3 localToWorld(const glm::vec3& local) const;

  /// Compute a view matrix in local (RTC) space — the camera and target are
  /// in world (f64) coordinates; the result is a float32 lookAt suitable for
  /// rendering meshes that have been pre-converted via worldToLocal.
  glm::mat4 computeViewMatrix(const glm::dvec3& eye_world,
                              const glm::dvec3& target_world,
                              const glm::vec3& up = {0, 0, 1}) const;

  // ---------------------------------------------------------------------------
  // Earth-flavoured aliases — backward compatibility, identical semantics.
  // ---------------------------------------------------------------------------

  /// Set the RTC origin from ECEF coordinates (also refreshes the geodetic mirror).
  void setOriginECEF(const glm::dvec3& ecef, const Ellipsoid& ellipsoid = WGS84);

  /// Set the RTC origin from geodetic coordinates.
  void setOriginGeodetic(const Geodetic& geo, const Ellipsoid& ellipsoid = WGS84);

  /// Get origin in ECEF (alias for `getOrigin`).
  const glm::dvec3& getOriginECEF() const { return origin_ecef_; }

  /// Get origin in geodetic. Stays stale if `setOrigin` (generic) was the last
  /// setter — generic setter has no ellipsoid context.
  const Geodetic& getOriginGeodetic() const { return origin_geo_; }

  /// ECEF → RTC float32 (alias for `worldToLocal`).
  glm::vec3 ecefToRTC(const glm::dvec3& ecef) const { return worldToLocal(ecef); }

  /// RTC float32 → ECEF (alias for `localToWorld`).
  glm::dvec3 rtcToECEF(const glm::vec3& rtc) const { return localToWorld(rtc); }

  /// Compute view matrix in RTC space (alias for `computeViewMatrix`).
  glm::mat4 computeRTCViewMatrix(const glm::dvec3& camera_ecef,
                                  const glm::dvec3& target_ecef,
                                  const glm::vec3& up = {0, 0, 1}) const {
    return computeViewMatrix(camera_ecef, target_ecef, up);
  }

  // ---------------------------------------------------------------------------
  // Shared utilities — coordinate-agnostic, kept under earth-flavoured names
  // because the names happen to make sense in both worlds.
  // ---------------------------------------------------------------------------

  /// Batch convert ECEF (or any world-f64) positions to local (f32) offsets.
  void batchECEFToRTC(const glm::dvec3* ecef_in, glm::vec3* rtc_out, size_t count) const;

  /// Compute precision error (ULP) at a given distance from origin.
  /// Returns the smallest representable position offset in meters.
  static f32 precisionAtDistance(f64 distance_from_origin);

  /// Check if a point is within the "safe zone" (precision < threshold).
  bool isInSafeZone(const glm::dvec3& world, f32 precision_threshold = 0.001f) const;

  /// Suggest whether the origin should be re-centered (camera moved too far).
  bool shouldRecenter(const glm::dvec3& camera_world, f64 max_distance = 500000.0) const;

 private:
  glm::dvec3 origin_ecef_{0, 0, 0};
  Geodetic origin_geo_{0, 0, 0};
};

/// Legacy alias — existing globe code uses this name and keeps working unchanged.
using RTCOrigin = PrecisionOrigin;

} // namespace hongtu::coordinate
