#include <hongtu/coordinate/global_local_transition.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace hongtu::coordinate {
namespace {

constexpr f64 kPi = 3.141592653589793238462643383279502884;

f64 clamp01(f64 value) { return std::clamp(value, 0.0, 1.0); }

f64 smoothstep01(f64 value) {
  const f64 t = clamp01(value);
  return t * t * (3.0 - 2.0 * t);
}

bool finiteGeodetic(const Geodetic& value) {
  return std::isfinite(value.lat) && std::isfinite(value.lon) && std::isfinite(value.alt);
}

}  // namespace

GlobalLocalTransition::GlobalLocalTransition(GlobalLocalTransitionConfig config)
    : config_(std::move(config)), frame_(enuFrameAt(config_.anchor)) {
  if (!finiteGeodetic(config_.anchor) || config_.anchor.lat < -0.5 * kPi || config_.anchor.lat > 0.5 * kPi ||
      config_.anchor.lon < -kPi || config_.anchor.lon > kPi) {
    throw std::invalid_argument("GlobalLocalTransition requires a finite WGS84 anchor");
  }
  if (!std::isfinite(config_.global_altitude_m) || !std::isfinite(config_.local_altitude_m) ||
      config_.global_altitude_m <= config_.local_altitude_m || config_.local_altitude_m <= 0.0) {
    throw std::invalid_argument("GlobalLocalTransition altitude range is invalid");
  }
  if (!std::isfinite(config_.local_blend_start_altitude_m) ||
      !std::isfinite(config_.local_blend_end_altitude_m) ||
      config_.local_blend_start_altitude_m <= config_.local_blend_end_altitude_m ||
      config_.local_blend_end_altitude_m <= 0.0) {
    throw std::invalid_argument("GlobalLocalTransition blend altitude range is invalid");
  }
  if (!std::isfinite(config_.regional_focus_fov_deg) || config_.regional_focus_fov_deg <= 0.0 ||
      config_.regional_focus_blend_start < 0.0 ||
      config_.regional_focus_blend_start >= config_.regional_focus_blend_peak ||
      config_.regional_focus_blend_peak >= config_.regional_focus_blend_end ||
      config_.regional_focus_blend_end > 1.0) {
    throw std::invalid_argument("GlobalLocalTransition regional focus configuration is invalid");
  }
}

f64 GlobalLocalTransition::localBlendForAltitude(f64 view_altitude_m) const {
  const f64 altitude = std::max(view_altitude_m, 1.0);
  const f64 high = std::log(config_.local_blend_start_altitude_m);
  const f64 low = std::log(config_.local_blend_end_altitude_m);
  const f64 normalized = (high - std::log(altitude)) / (high - low);
  return smoothstep01(normalized);
}

glm::dvec2 GlobalLocalTransition::clipPlanesForAltitude(f64 view_altitude_m) const {
  const f64 altitude = std::clamp(view_altitude_m, config_.local_altitude_m, config_.global_altitude_m);
  const f64 local_blend = localBlendForAltitude(altitude);
  const f64 near_clip = std::clamp(altitude * 0.001, 0.25, 12000.0);
  const f64 horizon = std::sqrt(std::max(0.0, altitude * (2.0 * WGS84.a + altitude)));
  const f64 globe_far = horizon * config_.horizon_far_margin + 50000.0;
  const f64 local_far = std::max(config_.local_far_clip_m, altitude * 2.5);
  const f64 clip_blend = smoothstep01((local_blend - 0.90) / 0.10);
  const f64 far_clip = std::max(near_clip * 4.0, globe_far + (local_far - globe_far) * clip_blend);
  return {near_clip, far_clip};
}

GlobalLocalTransitionSample GlobalLocalTransition::sample(f64 progress) const {
  GlobalLocalTransitionSample result;
  result.progress = clamp01(progress);
  result.eased_progress = smoothstep01(result.progress);

  const f64 log_global = std::log(config_.global_altitude_m);
  const f64 log_local = std::log(config_.local_altitude_m);
  if (result.progress <= 0.0) {
    result.view_altitude_m = config_.global_altitude_m;
  } else if (result.progress >= 1.0) {
    result.view_altitude_m = config_.local_altitude_m;
  } else {
    result.view_altitude_m = std::exp(log_global + (log_local - log_global) * result.eased_progress);
  }
  result.local_blend = localBlendForAltitude(result.view_altitude_m);
  result.globe_blend = 1.0 - result.local_blend;

  const glm::dvec2 global_xy = config_.global_horizontal_ratio * result.view_altitude_m;
  const f64 orbit_blend = smoothstep01((result.progress - 0.62) / 0.38);
  const glm::dvec2 camera_xy = global_xy + (config_.local_eye_xy_m - global_xy) * orbit_blend;
  result.camera_enu_m = {camera_xy.x, camera_xy.y, result.view_altitude_m};
  result.target_enu_m =
      config_.global_target_enu_m + (config_.local_target_enu_m - config_.global_target_enu_m) * result.local_blend;
  result.camera_ecef_m = frame_.localToEcef(result.camera_enu_m);
  result.camera_geodetic = ecefToGeodetic(result.camera_ecef_m);
  result.scale_level = scale_classifier_.altitudeToScaleLevel(result.view_altitude_m);
  const f64 base_fov =
      config_.global_fov_deg + (config_.local_fov_deg - config_.global_fov_deg) * result.local_blend;
  const f64 focus_in = smoothstep01((result.local_blend - config_.regional_focus_blend_start) /
                                    (config_.regional_focus_blend_peak - config_.regional_focus_blend_start));
  const f64 focus_out = 1.0 - smoothstep01((result.local_blend - config_.regional_focus_blend_peak) /
                                           (config_.regional_focus_blend_end - config_.regional_focus_blend_peak));
  result.vertical_fov_deg =
      base_fov + (config_.regional_focus_fov_deg - base_fov) * focus_in * focus_out;
  result.horizon_distance_m =
      std::sqrt(std::max(0.0, result.view_altitude_m * (2.0 * WGS84.a + result.view_altitude_m)));
  const glm::dvec2 clip = clipPlanesForAltitude(result.view_altitude_m);
  result.near_clip_m = clip.x;
  result.far_clip_m = clip.y;
  return result;
}

}  // namespace hongtu::coordinate
