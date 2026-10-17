#include <hongtu/renderer/globe/regional_imagery_lod.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace hongtu::renderer {
  namespace {

    f64 descendingLogSmoothstep(f64 altitude_m, f64 start_altitude_m, f64 full_altitude_m) {
      const f64 altitude = std::max(altitude_m, 1.0);
      const f64 start = std::log(start_altitude_m);
      const f64 full = std::log(full_altitude_m);
      const f64 linear = std::clamp((start - std::log(altitude)) / (start - full), 0.0, 1.0);
      return linear * linear * (3.0 - 2.0 * linear);
    }

  } // namespace

  RegionalImageryLod::RegionalImageryLod(RegionalImageryLodConfig config) : config_(config) {
    const bool finite_altitudes =
        std::isfinite(config_.coverage_start_altitude_m) && std::isfinite(config_.coverage_full_altitude_m) &&
        std::isfinite(config_.detail_start_altitude_m) && std::isfinite(config_.detail_full_altitude_m) &&
        std::isfinite(config_.context_start_altitude_m) && std::isfinite(config_.context_full_altitude_m) &&
        std::isfinite(config_.near_start_altitude_m) && std::isfinite(config_.near_full_altitude_m);
    if (!finite_altitudes || config_.coverage_start_altitude_m <= config_.coverage_full_altitude_m ||
        config_.detail_start_altitude_m <= config_.detail_full_altitude_m ||
        config_.context_start_altitude_m <= config_.context_full_altitude_m ||
        config_.near_start_altitude_m <= config_.near_full_altitude_m ||
        config_.coverage_full_altitude_m < config_.detail_start_altitude_m || config_.detail_full_altitude_m <= 0.0 ||
        config_.context_full_altitude_m <= 0.0 || config_.near_full_altitude_m <= 0.0 || config_.coarse_zoom < 0 ||
        config_.detail_zoom != config_.coarse_zoom + 1 || config_.context_zoom <= config_.detail_zoom ||
        config_.near_zoom <= config_.context_zoom) {
      throw std::invalid_argument("RegionalImageryLod configuration is invalid");
    }
  }

  RegionalImageryLodSample RegionalImageryLod::sample(f64 altitude_m) const {
    if (!std::isfinite(altitude_m))
      throw std::invalid_argument("RegionalImageryLod altitude must be finite");
    RegionalImageryLodSample result;
    result.altitude_m = std::max(altitude_m, 0.0);
    result.coverage_weight =
        descendingLogSmoothstep(result.altitude_m, config_.coverage_start_altitude_m, config_.coverage_full_altitude_m);
    result.detail_weight =
        descendingLogSmoothstep(result.altitude_m, config_.detail_start_altitude_m, config_.detail_full_altitude_m);
    result.context_weight =
        descendingLogSmoothstep(result.altitude_m, config_.context_start_altitude_m, config_.context_full_altitude_m);
    result.near_weight =
        descendingLogSmoothstep(result.altitude_m, config_.near_start_altitude_m, config_.near_full_altitude_m);
    if (result.coverage_weight <= 0.0)
      result.effective_zoom = -1;
    else if (result.detail_weight < 0.5)
      result.effective_zoom = config_.coarse_zoom;
    else if (result.context_weight < 0.5)
      result.effective_zoom = config_.detail_zoom;
    else if (result.near_weight < 0.5)
      result.effective_zoom = config_.context_zoom;
    else
      result.effective_zoom = config_.near_zoom;
    return result;
  }

} // namespace hongtu::renderer
