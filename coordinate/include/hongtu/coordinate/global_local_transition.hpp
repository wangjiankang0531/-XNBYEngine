#pragma once

#include <hongtu/coordinate/enu_frame.hpp>
#include <hongtu/coordinate/scale_switch.hpp>

namespace hongtu::coordinate {

/// Configuration for a continuous camera flight from a WGS84 globe to a
/// tangent-plane local scene.  The local frame is anchored at `anchor`; the
/// camera is always evaluated in double precision before it is lowered to the
/// float ENU coordinates consumed by the renderer.
struct GlobalLocalTransitionConfig {
  Geodetic anchor = Geodetic::fromDegrees(0.0, 0.0, 0.0);
  f64 global_altitude_m = 12000000.0;
  f64 local_altitude_m = 900.0;
  glm::dvec2 global_horizontal_ratio = {-0.03, -0.12};
  glm::dvec2 local_eye_xy_m = {0.0, -1100.0};
  glm::dvec3 global_target_enu_m = {0.0, 0.0, 0.0};
  glm::dvec3 local_target_enu_m = {0.0, 720.0, 20.0};
  f64 global_fov_deg = 50.0;
  f64 local_fov_deg = 60.0;
  f64 regional_focus_fov_deg = 36.0;
  f64 regional_focus_blend_start = 0.45;
  f64 regional_focus_blend_peak = 0.85;
  f64 regional_focus_blend_end = 0.98;
  f64 local_blend_start_altitude_m = 120000.0;
  f64 local_blend_end_altitude_m = 12000.0;
  f64 local_far_clip_m = 20000.0;
  f64 horizon_far_margin = 1.12;
};

struct GlobalLocalTransitionSample {
  f64 progress = 0.0;
  f64 eased_progress = 0.0;
  f64 view_altitude_m = 0.0;
  glm::dvec3 camera_enu_m{0.0};
  glm::dvec3 target_enu_m{0.0};
  glm::dvec3 camera_ecef_m{0.0};
  Geodetic camera_geodetic{};
  ScaleLevel scale_level = ScaleLevel::Global;
  f64 local_blend = 0.0;
  f64 globe_blend = 1.0;
  f64 vertical_fov_deg = 50.0;
  f64 near_clip_m = 1.0;
  f64 far_clip_m = 1000.0;
  f64 horizon_distance_m = 0.0;
};

/// Stateless, deterministic transition used by both interactive flights and
/// fixed regression captures.  Altitude is interpolated logarithmically so a
/// single normalized progress value traverses planetary, regional and park
/// scales without spending almost the whole animation in space.
class GlobalLocalTransition {
 public:
  explicit GlobalLocalTransition(GlobalLocalTransitionConfig config);

  const GlobalLocalTransitionConfig& config() const { return config_; }
  const EnuFrame& frame() const { return frame_; }

  GlobalLocalTransitionSample sample(f64 progress) const;

  /// Compute the same adaptive clip range used by `sample()` for a manually
  /// moved camera.  The far plane follows the visible ellipsoid horizon at
  /// global scales and converges to a local-scene range below city scale.
  glm::dvec2 clipPlanesForAltitude(f64 view_altitude_m) const;

 private:
  f64 localBlendForAltitude(f64 view_altitude_m) const;

  GlobalLocalTransitionConfig config_;
  EnuFrame frame_;
  ScaleSwitchManager scale_classifier_;
};

}  // namespace hongtu::coordinate
