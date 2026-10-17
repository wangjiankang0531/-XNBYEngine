#include <hongtu/coordinate/scale_switch.hpp>

#include <algorithm>
#include <cmath>

namespace hongtu::coordinate {

// ============================================================================
// Scale level utilities
// ============================================================================

const char* scaleLevelName(ScaleLevel level) {
  switch (level) {
    case ScaleLevel::Global: return "Global";
    case ScaleLevel::Continental: return "Continental";
    case ScaleLevel::National: return "National";
    case ScaleLevel::City: return "City";
    case ScaleLevel::District: return "District";
    case ScaleLevel::Building: return "Building";
    case ScaleLevel::Device: return "Device";
    default: return "Unknown";
  }
}

void scaleLevelAltitudeRange(ScaleLevel level, f64& min_alt, f64& max_alt) {
  // 7-level scale system based on view altitude
  switch (level) {
    case ScaleLevel::Global:       // > 10000 km
      min_alt = 10000000.0;
      max_alt = 1e12;
      break;
    case ScaleLevel::Continental:  // 1000 - 10000 km
      min_alt = 1000000.0;
      max_alt = 10000000.0;
      break;
    case ScaleLevel::National:     // 100 - 1000 km
      min_alt = 100000.0;
      max_alt = 1000000.0;
      break;
    case ScaleLevel::City:         // 10 - 100 km
      min_alt = 10000.0;
      max_alt = 100000.0;
      break;
    case ScaleLevel::District:     // 1 - 10 km
      min_alt = 1000.0;
      max_alt = 10000.0;
      break;
    case ScaleLevel::Building:     // 100m - 1 km
      min_alt = 100.0;
      max_alt = 1000.0;
      break;
    case ScaleLevel::Device:       // < 100m
      min_alt = 0.0;
      max_alt = 100.0;
      break;
    default:
      min_alt = 0.0;
      max_alt = 1e12;
      break;
  }
}

// ============================================================================
// ScaleSwitchManager
// ============================================================================

ScaleSwitchManager::ScaleSwitchManager(const ScaleSwitchConfig& config) : config_(config) {
  transition_.from_level = ScaleLevel::Global;
  transition_.to_level = ScaleLevel::Global;
  transition_.progress = 0.0;
  transition_.active = false;
}

ScaleLevel ScaleSwitchManager::altitudeToScaleLevel(f64 altitude) const {
  // Determine the raw scale level from altitude
  if (altitude > 10000000.0) return ScaleLevel::Global;
  if (altitude > 1000000.0) return ScaleLevel::Continental;
  if (altitude > 100000.0) return ScaleLevel::National;
  if (altitude > 10000.0) return ScaleLevel::City;
  if (altitude > 1000.0) return ScaleLevel::District;
  if (altitude > 100.0) return ScaleLevel::Building;
  return ScaleLevel::Device;
}

void ScaleSwitchManager::update(const Geodetic& camera_geodetic, f64 view_altitude,
                                 f64 delta_time) {
  last_camera_geo_ = camera_geodetic;
  last_altitude_ = view_altitude;

  // Update active transition
  if (transition_.active) {
    f64 speed = 1.0 / std::max(config_.transition_duration_sec, 0.001);
    transition_.progress += delta_time * speed;

    if (transition_.progress >= 1.0) {
      transition_.progress = 1.0;
      transition_.active = false;
      current_level_ = transition_.to_level;

      // Update RTC origin when we reach local scales
      if (static_cast<u32>(current_level_) >= static_cast<u32>(ScaleLevel::City)) {
        rtc_origin_.setOriginGeodetic(camera_geodetic);
      }
    }
  }

  if (!config_.auto_switch) return;

  // Determine target level with hysteresis
  ScaleLevel target = altitudeToScaleLevel(view_altitude);

  if (target != current_level_ && !transition_.active) {
    // Apply hysteresis: require altitude to be past the boundary by the hysteresis factor
    // to avoid oscillation at level boundaries
    f64 target_min, target_max;
    scaleLevelAltitudeRange(target, target_min, target_max);

    f64 current_min, current_max;
    scaleLevelAltitudeRange(current_level_, current_min, current_max);

    bool should_switch = false;

    if (static_cast<u32>(target) < static_cast<u32>(current_level_)) {
      // Zooming out (higher altitude -> coarser/global level, lower enum value)
      f64 threshold = current_max * (1.0 + config_.hysteresis_factor);
      should_switch = view_altitude > threshold;
    } else {
      // Zooming in (lower altitude -> finer/local level, higher enum value)
      f64 threshold = current_min * (1.0 - config_.hysteresis_factor);
      should_switch = view_altitude < threshold;
    }

    if (should_switch) {
      transition_.from_level = current_level_;
      transition_.to_level = target;
      transition_.progress = 0.0;
      transition_.active = true;
    }
  }
}

f64 ScaleSwitchManager::getLocalBlendFactor() const {
  // Compute how "local" the current view is
  // ScaleLevel::Global = 0 -> blend 0 (fully global/ECEF)
  // ScaleLevel::Device = 6 -> blend 1 (fully local/ENU)

  auto levelToBlend = [](ScaleLevel level) -> f64 {
    switch (level) {
      case ScaleLevel::Global: return 0.0;
      case ScaleLevel::Continental: return 0.0;
      case ScaleLevel::National: return 0.2;
      case ScaleLevel::City: return 0.5;
      case ScaleLevel::District: return 0.8;
      case ScaleLevel::Building: return 1.0;
      case ScaleLevel::Device: return 1.0;
      default: return 0.0;
    }
  };

  if (transition_.active) {
    // Smooth interpolation during transition using smoothstep
    f64 t = transition_.progress;
    // Smoothstep: 3t^2 - 2t^3
    f64 smooth_t = t * t * (3.0 - 2.0 * t);

    f64 from_blend = levelToBlend(transition_.from_level);
    f64 to_blend = levelToBlend(transition_.to_level);
    return from_blend + (to_blend - from_blend) * smooth_t;
  }

  return levelToBlend(current_level_);
}

ScaleSwitchManager::CoordState ScaleSwitchManager::getCoordState() const {
  CoordState state;
  state.level = current_level_;
  state.origin_ecef = rtc_origin_.getOriginECEF();
  state.local_blend = getLocalBlendFactor();
  state.use_local_frame = state.local_blend > 0.5;
  return state;
}

void ScaleSwitchManager::forceLevel(ScaleLevel level) {
  // Stop any active transition
  transition_.active = false;
  transition_.progress = 0.0;
  current_level_ = level;

  // Update RTC origin if entering local scale
  if (static_cast<u32>(level) >= static_cast<u32>(ScaleLevel::City)) {
    rtc_origin_.setOriginGeodetic(last_camera_geo_);
  }
}

}  // namespace hongtu::coordinate
