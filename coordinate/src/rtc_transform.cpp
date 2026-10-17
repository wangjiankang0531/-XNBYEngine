#include <hongtu/coordinate/rtc_transform.hpp>

#include <cmath>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

namespace hongtu::coordinate {

// -----------------------------------------------------------------------------
// Generic API
// -----------------------------------------------------------------------------

void PrecisionOrigin::setOrigin(const glm::dvec3& world) {
  origin_ecef_ = world;
}

glm::vec3 PrecisionOrigin::worldToLocal(const glm::dvec3& world) const {
  // The key operation: subtract origin in double precision, then cast to float
  glm::dvec3 offset = world - origin_ecef_;
  return glm::vec3(static_cast<f32>(offset.x), static_cast<f32>(offset.y),
                   static_cast<f32>(offset.z));
}

glm::dvec3 PrecisionOrigin::localToWorld(const glm::vec3& local) const {
  return origin_ecef_ + glm::dvec3(static_cast<f64>(local.x), static_cast<f64>(local.y),
                                   static_cast<f64>(local.z));
}

glm::mat4 PrecisionOrigin::computeViewMatrix(const glm::dvec3& eye_world,
                                              const glm::dvec3& target_world,
                                              const glm::vec3& up) const {
  glm::vec3 eye_local    = worldToLocal(eye_world);
  glm::vec3 target_local = worldToLocal(target_world);
  return glm::lookAt(eye_local, target_local, up);
}

// -----------------------------------------------------------------------------
// Earth-flavoured aliases — refresh the geodetic mirror alongside the origin.
// -----------------------------------------------------------------------------

void PrecisionOrigin::setOriginECEF(const glm::dvec3& ecef, const Ellipsoid& ellipsoid) {
  origin_ecef_ = ecef;
  origin_geo_  = ecefToGeodetic(ecef, ellipsoid);
}

void PrecisionOrigin::setOriginGeodetic(const Geodetic& geo, const Ellipsoid& ellipsoid) {
  origin_geo_  = geo;
  origin_ecef_ = geodeticToECEF(geo, ellipsoid);
}

// -----------------------------------------------------------------------------
// Shared utilities
// -----------------------------------------------------------------------------

void PrecisionOrigin::batchECEFToRTC(const glm::dvec3* ecef_in, glm::vec3* rtc_out,
                                      size_t count) const {
  for (size_t i = 0; i < count; ++i) {
    glm::dvec3 offset = ecef_in[i] - origin_ecef_;
    rtc_out[i] = glm::vec3(static_cast<f32>(offset.x), static_cast<f32>(offset.y),
                           static_cast<f32>(offset.z));
  }
}

f32 PrecisionOrigin::precisionAtDistance(f64 distance_from_origin) {
  // Float32 has 23 mantissa bits -> ULP-based precision estimate
  // The precision (smallest representable offset) at a given magnitude is:
  // epsilon * 2^(floor(log2(magnitude)))
  // Which equals the magnitude times the machine epsilon for float32.

  if (distance_from_origin <= 0.0) return 0.0f;

  // float32 machine epsilon = 2^-23 ~ 1.19209e-07
  constexpr f64 float32_epsilon = static_cast<f64>(std::numeric_limits<f32>::epsilon());

  // The ULP at this distance
  f64 exponent = std::floor(std::log2(distance_from_origin));
  f64 ulp = std::ldexp(1.0, static_cast<int>(exponent)) * float32_epsilon;

  return static_cast<f32>(ulp);
}

bool PrecisionOrigin::isInSafeZone(const glm::dvec3& world, f32 precision_threshold) const {
  f64 distance = glm::length(world - origin_ecef_);
  f32 precision = precisionAtDistance(distance);
  return precision < precision_threshold;
}

bool PrecisionOrigin::shouldRecenter(const glm::dvec3& camera_world, f64 max_distance) const {
  f64 distance = glm::length(camera_world - origin_ecef_);
  return distance > max_distance;
}

}  // namespace hongtu::coordinate
