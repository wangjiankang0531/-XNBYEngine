#include <hongtu/coordinate/dispersion/gaussian_plume.hpp>

#include <algorithm>
#include <cmath>

namespace hongtu::coordinate::dispersion {

// =============================================================================
// Pasquill 系数 — Briggs / Pasquill formulas for rural σy, σz (meters).
// 来源:Wang 1981 / EPA AERMOD,常用范围 100..10000 m。形式:
//   σy(x) = a · x · (1 + b · x)^c
//   σz(x) = d · x · (1 + e · x)^f
// 系数表见 EPA SCRAM,这里取常用 rural 系数。
// =============================================================================

glm::dvec2 pasquillSigmas(PasquillStability stab, double x) {
  if (x <= 0.0) return {0.0, 0.0};

  double sy_a = 0.0, sy_b = 0.0, sy_c = 0.0;
  double sz_a = 0.0, sz_b = 0.0, sz_c = 0.0;

  switch (stab) {
    case PasquillStability::A:
      sy_a = 0.22;  sy_b = 0.0001; sy_c = -0.5;
      sz_a = 0.20;  sz_b = 0.0;    sz_c = 1.0;
      break;
    case PasquillStability::B:
      sy_a = 0.16;  sy_b = 0.0001; sy_c = -0.5;
      sz_a = 0.12;  sz_b = 0.0;    sz_c = 1.0;
      break;
    case PasquillStability::C:
      sy_a = 0.11;  sy_b = 0.0001; sy_c = -0.5;
      sz_a = 0.08;  sz_b = 0.0002; sz_c = -0.5;
      break;
    case PasquillStability::D:
      sy_a = 0.08;  sy_b = 0.0001; sy_c = -0.5;
      sz_a = 0.06;  sz_b = 0.0015; sz_c = -0.5;
      break;
    case PasquillStability::E:
      sy_a = 0.06;  sy_b = 0.0001; sy_c = -0.5;
      sz_a = 0.03;  sz_b = 0.0003; sz_c = -1.0;
      break;
    case PasquillStability::F:
      sy_a = 0.04;  sy_b = 0.0001; sy_c = -0.5;
      sz_a = 0.016; sz_b = 0.0003; sz_c = -1.0;
      break;
  }

  // σy = a · x · (1 + b · x)^c
  const double sigma_y = sy_a * x * std::pow(1.0 + sy_b * x, sy_c);
  const double sigma_z = sz_a * x * std::pow(1.0 + sz_b * x, sz_c);
  return {sigma_y, sigma_z};
}

// =============================================================================
// sampleGaussianPlume — 标准点源高斯羽流公式,无地面反射(简化):
//
//   C(x, y, z) = Q / (2π · u · σy · σz)
//              · exp(-y² / (2σy²))
//              · exp(-(z - H)² / (2σz²))
//
// 其中 x = 下风距离,y = 侧风偏移,z = 高度,H = 源高度,u = 风速。
// =============================================================================

double sampleGaussianPlume(const glm::dvec3& p, const GaussianPlumeConfig& cfg) {
  // 风速 + 风向单位向量
  const double u_mag = glm::length(cfg.wind);
  if (u_mag < 1e-6) return 0.0;
  const glm::dvec3 wind_hat = cfg.wind / u_mag;

  // 把 p 相对源点投影到下风(x) / 侧风(y) / 垂直(z) 三个轴。
  const glm::dvec3 rel = p - cfg.source;
  const double x_down = glm::dot(rel, wind_hat);
  if (x_down <= 0.0) return 0.0;   // 上风方向无浓度

  // 侧风:y 分量 = 在水平面上 perpendicular to wind 的分量
  glm::dvec3 wind_horiz = wind_hat;
  wind_horiz.z = 0.0;
  if (glm::length(wind_horiz) > 1e-6) wind_horiz = glm::normalize(wind_horiz);
  glm::dvec3 lat = glm::normalize(glm::cross(glm::dvec3(0, 0, 1), wind_horiz));
  const double y_side = glm::dot(rel, lat);
  const double z_up   = p.z - cfg.source.z;   // 垂直偏移(源高度参考)

  const glm::dvec2 s = pasquillSigmas(cfg.stability, x_down);
  if (s.x < 1e-6 || s.y < 1e-6) return 0.0;

  // 高斯衰减
  const double prefactor = cfg.emission_rate
                         / (2.0 * 3.14159265358979 * u_mag * s.x * s.y);
  const double ey = std::exp(-(y_side * y_side) / (2.0 * s.x * s.x));
  const double ez = std::exp(-(z_up   * z_up)   / (2.0 * s.y * s.y));
  return prefactor * ey * ez;
}

DensityGrid buildGaussianPlumeGrid(const GaussianPlumeConfig& cfg,
                                    const glm::dvec3& aabb_min,
                                    const glm::dvec3& aabb_max,
                                    uint32_t w, uint32_t h, uint32_t d) {
  DensityGrid g;
  g.width    = w;
  g.height   = h;
  g.depth    = d;
  g.aabb_min = aabb_min;
  g.aabb_max = aabb_max;
  g.values.assign(static_cast<size_t>(w) * h * d, 0.0f);

  double peak = 0.0;
  for (uint32_t k = 0; k < d; ++k) {
    for (uint32_t j = 0; j < h; ++j) {
      for (uint32_t i = 0; i < w; ++i) {
        glm::dvec3 c = g.cellCenter(i, j, k);
        double val = sampleGaussianPlume(c, cfg);
        g.values[g.indexOf(i, j, k)] = static_cast<float>(val);
        peak = std::max(peak, val);
      }
    }
  }
  if (peak > 0.0) {
    const float inv_peak = static_cast<float>(1.0 / peak);
    for (auto& v : g.values) v *= inv_peak;
  }
  return g;
}

DensityGrid buildRoomSmokeGrid(const glm::dvec3& source,
                                const glm::dvec3& aabb_min,
                                const glm::dvec3& aabb_max,
                                uint32_t w, uint32_t h, uint32_t d,
                                uint32_t iterations,
                                float injection_rate) {
  DensityGrid g;
  g.width    = w;
  g.height   = h;
  g.depth    = d;
  g.aabb_min = aabb_min;
  g.aabb_max = aabb_max;
  const size_t N = static_cast<size_t>(w) * h * d;
  g.values.assign(N, 0.0f);

  // Inject at source cell.
  glm::dvec3 ext = aabb_max - aabb_min;
  glm::dvec3 rel = source - aabb_min;
  if (ext.x <= 0 || ext.y <= 0 || ext.z <= 0) return g;
  uint32_t si = static_cast<uint32_t>(
      std::clamp(rel.x / ext.x * static_cast<double>(w), 0.0,
                 static_cast<double>(w - 1)));
  uint32_t sj = static_cast<uint32_t>(
      std::clamp(rel.y / ext.y * static_cast<double>(h), 0.0,
                 static_cast<double>(h - 1)));
  uint32_t sk = static_cast<uint32_t>(
      std::clamp(rel.z / ext.z * static_cast<double>(d), 0.0,
                 static_cast<double>(d - 1)));
  g.values[g.indexOf(si, sj, sk)] = injection_rate;

  // Iterated 6-neighbor average + small decay. Like a single-iter blur,
  // not a real fluid solver — gives a soft static smoke blob.
  std::vector<float> tmp(N, 0.0f);
  for (uint32_t step = 0; step < iterations; ++step) {
    std::copy(g.values.begin(), g.values.end(), tmp.begin());
    for (uint32_t k = 0; k < d; ++k) {
      for (uint32_t j = 0; j < h; ++j) {
        for (uint32_t i = 0; i < w; ++i) {
          float self = tmp[g.indexOf(i, j, k)];
          float sum = self;
          int   n   = 1;
          auto acc = [&](int di, int dj, int dk) {
            int ii = static_cast<int>(i) + di;
            int jj = static_cast<int>(j) + dj;
            int kk = static_cast<int>(k) + dk;
            if (ii < 0 || ii >= static_cast<int>(w) ||
                jj < 0 || jj >= static_cast<int>(h) ||
                kk < 0 || kk >= static_cast<int>(d)) return;
            sum += tmp[g.indexOf(static_cast<uint32_t>(ii),
                                  static_cast<uint32_t>(jj),
                                  static_cast<uint32_t>(kk))];
            ++n;
          };
          acc(-1, 0, 0); acc(1, 0, 0);
          acc(0, -1, 0); acc(0, 1, 0);
          acc(0, 0, -1); acc(0, 0, 1);
          // 0.985 decay per iter — soft falloff over many steps
          g.values[g.indexOf(i, j, k)] = (sum / static_cast<float>(n)) * 0.985f;
        }
      }
    }
    // Re-inject at source so steady-state plume forms.
    g.values[g.indexOf(si, sj, sk)] = std::max(
        g.values[g.indexOf(si, sj, sk)], injection_rate);
  }

  // Normalize peak to 1.
  float peak = 0.0f;
  for (auto v : g.values) peak = std::max(peak, v);
  if (peak > 0.0f) {
    const float inv = 1.0f / peak;
    for (auto& v : g.values) v *= inv;
  }
  return g;
}

}  // namespace hongtu::coordinate::dispersion
