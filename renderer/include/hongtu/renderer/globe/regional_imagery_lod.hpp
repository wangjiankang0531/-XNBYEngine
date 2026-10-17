#pragma once

#include <hongtu/core/common.hpp>

namespace hongtu::renderer {

  /// Altitude policy for a bounded regional-imagery branch layered between a
  /// low-resolution globe texture and local DEM/DOM. Altitudes are evaluated
  /// logarithmically so that the transition is stable across planetary scales.
  struct RegionalImageryLodConfig {
    f64 coverage_start_altitude_m = 5000000.0;
    f64 coverage_full_altitude_m = 2000000.0;
    f64 detail_start_altitude_m = 1600000.0;
    f64 detail_full_altitude_m = 400000.0;
    f64 context_start_altitude_m = 1000000.0;
    f64 context_full_altitude_m = 250000.0;
    f64 near_start_altitude_m = 200000.0;
    f64 near_full_altitude_m = 50000.0;
    i32 coarse_zoom = 3;
    i32 detail_zoom = 4;
    i32 context_zoom = 8;
    i32 near_zoom = 10;
  };

  struct RegionalImageryLodSample {
    f64 altitude_m = 0.0;
    f64 coverage_weight = 0.0;
    f64 detail_weight = 0.0;
    f64 context_weight = 0.0;
    f64 near_weight = 0.0;
    i32 effective_zoom = -1;
  };

  class RegionalImageryLod {
  public:
    explicit RegionalImageryLod(RegionalImageryLodConfig config = {});

    const RegionalImageryLodConfig& config() const {
      return config_;
    }
    RegionalImageryLodSample sample(f64 altitude_m) const;

  private:
    RegionalImageryLodConfig config_;
  };

} // namespace hongtu::renderer
