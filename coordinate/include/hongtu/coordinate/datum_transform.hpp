#pragma once

#include <hongtu/coordinate/ellipsoid.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace hongtu::coordinate {

// ============================================================================
// Bursa-Wolf 7-Parameter Datum Transform
// ============================================================================

/// Seven parameters for Bursa-Wolf model: 3 translations + 3 rotations + 1 scale.
/// Converts ECEF coordinates from source datum to target datum.
/// X' = T + (1+s) * R * X
/// where T = [tx, ty, tz], s = scale (ppm), R = rotation matrix from [rx, ry, rz] (arcsec)
struct BursaWolfParams {
  f64 tx, ty, tz;    ///< Translation in meters
  f64 rx, ry, rz;    ///< Rotation in arc-seconds
  f64 scale;         ///< Scale factor in parts per million (ppm)
};

/// Apply Bursa-Wolf 7-parameter transform (forward: source -> target).
glm::dvec3 bursaWolfForward(const glm::dvec3& ecef, const BursaWolfParams& params);

/// Apply Bursa-Wolf 7-parameter transform (inverse: target -> source).
glm::dvec3 bursaWolfInverse(const glm::dvec3& ecef, const BursaWolfParams& params);

/// Batch forward transform.
void batchBursaWolfForward(const glm::dvec3* input, glm::dvec3* output, size_t count,
                           const BursaWolfParams& params);

// ============================================================================
// 4-Parameter Conformal (Helmert 2D) Transform
// ============================================================================

/// Four parameters for 2D conformal (Helmert) transform.
/// x' = tx + scale * (cos(rot) * x - sin(rot) * y)
/// y' = ty + scale * (sin(rot) * x + cos(rot) * y)
struct Helmert2DParams {
  f64 tx, ty;    ///< Translation in meters
  f64 rotation;  ///< Rotation angle in radians
  f64 scale;     ///< Scale factor (1.0 = no scaling)
};

/// Apply 2D Helmert transform (forward).
glm::dvec2 helmert2DForward(const glm::dvec2& point, const Helmert2DParams& params);

/// Apply 2D Helmert transform (inverse).
glm::dvec2 helmert2DInverse(const glm::dvec2& point, const Helmert2DParams& params);

// ============================================================================
// Least-Squares Parameter Estimation
// ============================================================================

/// Control point pair for parameter estimation.
struct ControlPointPair {
  glm::dvec3 source; ///< Source coordinate (ECEF)
  glm::dvec3 target; ///< Target coordinate (ECEF)
};

/// Control point pair for 2D parameter estimation.
struct ControlPointPair2D {
  glm::dvec2 source;
  glm::dvec2 target;
};

/// Estimate Bursa-Wolf 7 parameters from control point pairs (minimum 3 pairs).
/// Returns true on success, false if insufficient or degenerate points.
bool estimateBursaWolfParams(const std::vector<ControlPointPair>& points,
                             BursaWolfParams& params,
                             std::vector<f64>* residuals = nullptr);

/// Estimate 2D Helmert 4 parameters from control point pairs (minimum 2 pairs).
bool estimateHelmert2DParams(const std::vector<ControlPointPair2D>& points,
                             Helmert2DParams& params,
                             std::vector<f64>* residuals = nullptr);

} // namespace hongtu::coordinate
