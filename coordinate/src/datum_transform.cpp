#include <hongtu/coordinate/datum_transform.hpp>
#include <cmath>

namespace hongtu::coordinate {

static constexpr f64 ARCSEC_TO_RAD = 3.14159265358979323846 / (180.0 * 3600.0);
static constexpr f64 PPM = 1e-6;

// ============================================================================
// Bursa-Wolf 7-Parameter Transform
// ============================================================================

glm::dvec3 bursaWolfForward(const glm::dvec3& ecef, const BursaWolfParams& p) {
  const f64 s = 1.0 + p.scale * PPM;
  const f64 rx = p.rx * ARCSEC_TO_RAD;
  const f64 ry = p.ry * ARCSEC_TO_RAD;
  const f64 rz = p.rz * ARCSEC_TO_RAD;

  // Linearized rotation matrix (small angle approximation)
  // R = I + [[0, -rz, ry], [rz, 0, -rx], [-ry, rx, 0]]
  return {
      p.tx + s * (ecef.x - rz * ecef.y + ry * ecef.z),
      p.ty + s * (rz * ecef.x + ecef.y - rx * ecef.z),
      p.tz + s * (-ry * ecef.x + rx * ecef.y + ecef.z)};
}

glm::dvec3 bursaWolfInverse(const glm::dvec3& ecef, const BursaWolfParams& p) {
  // Inverse: negate parameters
  BursaWolfParams inv = {-p.tx, -p.ty, -p.tz, -p.rx, -p.ry, -p.rz, -p.scale};
  return bursaWolfForward(ecef, inv);
}

void batchBursaWolfForward(const glm::dvec3* input, glm::dvec3* output, size_t count,
                           const BursaWolfParams& params) {
  for (size_t i = 0; i < count; ++i) {
    output[i] = bursaWolfForward(input[i], params);
  }
}

// ============================================================================
// 2D Helmert (4-Parameter Conformal) Transform
// ============================================================================

glm::dvec2 helmert2DForward(const glm::dvec2& point, const Helmert2DParams& p) {
  const f64 cos_r = std::cos(p.rotation);
  const f64 sin_r = std::sin(p.rotation);
  return {
      p.tx + p.scale * (cos_r * point.x - sin_r * point.y),
      p.ty + p.scale * (sin_r * point.x + cos_r * point.y)};
}

glm::dvec2 helmert2DInverse(const glm::dvec2& point, const Helmert2DParams& p) {
  const f64 cos_r = std::cos(p.rotation);
  const f64 sin_r = std::sin(p.rotation);
  const f64 dx = point.x - p.tx;
  const f64 dy = point.y - p.ty;
  const f64 inv_scale = 1.0 / p.scale;
  return {
      inv_scale * (cos_r * dx + sin_r * dy),
      inv_scale * (-sin_r * dx + cos_r * dy)};
}

// ============================================================================
// Least-Squares: Bursa-Wolf Parameter Estimation
// Simplified linearized solution (standard geodetic approach)
// ============================================================================

bool estimateBursaWolfParams(const std::vector<ControlPointPair>& points,
                             BursaWolfParams& params,
                             std::vector<f64>* residuals) {
  const size_t n = points.size();
  if (n < 3) return false;

  // Observation equations: target - source = [tx, ty, tz] + [0,-rz,ry; rz,0,-rx; -ry,rx,0]*source + s*source
  // Linearized: 7 unknowns, 3n observations
  // Normal equation: A'A * x = A'b
  // A is 3n x 7, x = [tx, ty, tz, rx, ry, rz, s]

  // Build normal equations directly (A'A and A'b)
  double ATA[7][7] = {};
  double ATb[7] = {};

  for (size_t i = 0; i < n; ++i) {
    const auto& s = points[i].source;
    const auto& t = points[i].target;
    const double dx = t.x - s.x;
    const double dy = t.y - s.y;
    const double dz = t.z - s.z;

    // Row 1: dx = tx + 0 + rz*(-sy) + ry*sz + s*sx
    // A_row1 = [1, 0, 0, 0, sz, -sy, sx]
    double A1[7] = {1, 0, 0, 0, s.z, -s.y, s.x};
    // Row 2: dy = ty + rz*sx + 0 + rx*(-sz) + s*sy
    // A_row2 = [0, 1, 0, -sz, 0, sx, sy]
    double A2[7] = {0, 1, 0, -s.z, 0, s.x, s.y};
    // Row 3: dz = tz + ry*(-sx) + rx*sy + 0 + s*sz
    // A_row3 = [0, 0, 1, sy, -sx, 0, sz]
    double A3[7] = {0, 0, 1, s.y, -s.x, 0, s.z};

    double b[3] = {dx, dy, dz};
    double* rows[3] = {A1, A2, A3};

    for (int r = 0; r < 3; ++r) {
      for (int j = 0; j < 7; ++j) {
        ATb[j] += rows[r][j] * b[r];
        for (int k = 0; k < 7; ++k) {
          ATA[j][k] += rows[r][j] * rows[r][k];
        }
      }
    }
  }

  // Solve 7x7 system via Gauss elimination with partial pivoting
  double aug[7][8];
  for (int i = 0; i < 7; ++i) {
    for (int j = 0; j < 7; ++j) aug[i][j] = ATA[i][j];
    aug[i][7] = ATb[i];
  }

  for (int col = 0; col < 7; ++col) {
    // Partial pivoting
    int max_row = col;
    double max_val = std::abs(aug[col][col]);
    for (int row = col + 1; row < 7; ++row) {
      if (std::abs(aug[row][col]) > max_val) {
        max_val = std::abs(aug[row][col]);
        max_row = row;
      }
    }
    if (max_val < 1e-20) return false; // Singular

    if (max_row != col) {
      for (int j = 0; j <= 7; ++j) std::swap(aug[col][j], aug[max_row][j]);
    }

    for (int row = col + 1; row < 7; ++row) {
      double factor = aug[row][col] / aug[col][col];
      for (int j = col; j <= 7; ++j) {
        aug[row][j] -= factor * aug[col][j];
      }
    }
  }

  // Back substitution
  double x[7];
  for (int i = 6; i >= 0; --i) {
    x[i] = aug[i][7];
    for (int j = i + 1; j < 7; ++j) {
      x[i] -= aug[i][j] * x[j];
    }
    x[i] /= aug[i][i];
  }

  // Convert from linearized to standard units
  params.tx = x[0];
  params.ty = x[1];
  params.tz = x[2];
  params.rx = x[3] / ARCSEC_TO_RAD; // Convert back to arcseconds
  params.ry = x[4] / ARCSEC_TO_RAD;
  params.rz = x[5] / ARCSEC_TO_RAD;
  params.scale = x[6] / PPM;        // Convert back to ppm

  // Compute residuals if requested
  if (residuals) {
    residuals->resize(n);
    for (size_t i = 0; i < n; ++i) {
      auto transformed = bursaWolfForward(points[i].source, params);
      (*residuals)[i] = glm::length(transformed - points[i].target);
    }
  }

  return true;
}

// ============================================================================
// Least-Squares: 2D Helmert Parameter Estimation
// ============================================================================

bool estimateHelmert2DParams(const std::vector<ControlPointPair2D>& points,
                             Helmert2DParams& params,
                             std::vector<f64>* residuals) {
  const size_t n = points.size();
  if (n < 2) return false;

  // Model: x' = a*xs - b*ys + tx,  y' = b*xs + a*ys + ty
  // Unknowns: [a, b, tx, ty]
  // Build A'A (4x4) and A'b (4x1) from observation equations
  double ATA[4][4] = {};
  double ATb[4] = {};

  for (size_t i = 0; i < n; ++i) {
    const auto& s = points[i].source;
    const auto& t = points[i].target;

    // Row for x': [xs, -ys, 1, 0] -> target x
    // Row for y': [ys,  xs, 0, 1] -> target y
    double A1[4] = {s.x, -s.y, 1.0, 0.0};
    double A2[4] = {s.y, s.x, 0.0, 1.0};
    double b1 = t.x;
    double b2 = t.y;

    for (int j = 0; j < 4; ++j) {
      ATb[j] += A1[j] * b1 + A2[j] * b2;
      for (int k = 0; k < 4; ++k) {
        ATA[j][k] += A1[j] * A1[k] + A2[j] * A2[k];
      }
    }
  }

  // Solve 4x4 via Gauss elimination with partial pivoting
  double aug[4][5];
  for (int i = 0; i < 4; ++i) {
    for (int j = 0; j < 4; ++j) aug[i][j] = ATA[i][j];
    aug[i][4] = ATb[i];
  }

  for (int col = 0; col < 4; ++col) {
    int max_row = col;
    double max_val = std::abs(aug[col][col]);
    for (int row = col + 1; row < 4; ++row) {
      if (std::abs(aug[row][col]) > max_val) {
        max_val = std::abs(aug[row][col]);
        max_row = row;
      }
    }
    if (max_val < 1e-20) return false;
    if (max_row != col) {
      for (int j = 0; j <= 4; ++j) std::swap(aug[col][j], aug[max_row][j]);
    }
    for (int row = col + 1; row < 4; ++row) {
      double factor = aug[row][col] / aug[col][col];
      for (int j = col; j <= 4; ++j) aug[row][j] -= factor * aug[col][j];
    }
  }

  double x[4];
  for (int i = 3; i >= 0; --i) {
    x[i] = aug[i][4];
    for (int j = i + 1; j < 4; ++j) x[i] -= aug[i][j] * x[j];
    x[i] /= aug[i][i];
  }

  double a = x[0], b = x[1];
  params.tx = x[2];
  params.ty = x[3];
  params.scale = std::sqrt(a * a + b * b);
  params.rotation = std::atan2(b, a);

  if (residuals) {
    residuals->resize(n);
    for (size_t i = 0; i < n; ++i) {
      auto transformed = helmert2DForward(points[i].source, params);
      (*residuals)[i] = glm::length(transformed - points[i].target);
    }
  }

  return true;
}

} // namespace hongtu::coordinate
