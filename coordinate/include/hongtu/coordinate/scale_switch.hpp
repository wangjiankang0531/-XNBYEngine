#pragma once

#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace hongtu::coordinate {

/// Scale level for coordinate system switching.
enum class ScaleLevel : u32 {
  Global = 0,     ///< > 10000 km view (whole earth)
  Continental,    ///< 1000-10000 km
  National,       ///< 100-1000 km
  City,           ///< 10-100 km
  District,       ///< 1-10 km
  Building,       ///< 100m-1km
  Device,         ///< < 100m (equipment level)
  Count
};

/// Get the name of a scale level.
const char* scaleLevelName(ScaleLevel level);

/// Get the typical view altitude range for a scale level (meters).
void scaleLevelAltitudeRange(ScaleLevel level, f64& min_alt, f64& max_alt);

/// Scale transition configuration.
struct ScaleSwitchConfig {
  f64 hysteresis_factor = 0.15;      ///< Overlap ratio to prevent oscillation
  f64 transition_duration_sec = 0.5; ///< Blend duration when switching
  bool auto_switch = true;           ///< Automatically switch based on altitude
};

/// State of a scale transition (for smooth blending).
struct ScaleTransition {
  ScaleLevel from_level;
  ScaleLevel to_level;
  f64 progress = 0.0;               ///< [0, 1], 0=fully from, 1=fully to
  bool active = false;
};

/// Manages automatic coordinate system switching between global and local views.
class ScaleSwitchManager {
 public:
  explicit ScaleSwitchManager(const ScaleSwitchConfig& config = {});

  /// Update based on current camera state.
  /// @param camera_geodetic  Camera position in geodetic coordinates.
  /// @param view_altitude    Effective view altitude (distance to ground, meters).
  /// @param delta_time       Time since last update in seconds.
  void update(const Geodetic& camera_geodetic, f64 view_altitude, f64 delta_time);

  /// Get current scale level.
  ScaleLevel getCurrentLevel() const { return current_level_; }

  /// Get current transition state.
  const ScaleTransition& getTransition() const { return transition_; }

  /// Is a transition currently active?
  bool isTransitioning() const { return transition_.active; }

  /// Get blend factor: 0 = fully global (ECEF), 1 = fully local (ENU).
  /// Smooth interpolation during transitions.
  f64 getLocalBlendFactor() const;

  /// Determine which scale level an altitude corresponds to.
  ScaleLevel altitudeToScaleLevel(f64 altitude) const;

  /// Compute the coordinate transformation for current state.
  /// When global: uses ECEF directly.
  /// When local: uses ENU relative to a local anchor.
  /// During transition: blends between them.
  struct CoordState {
    ScaleLevel level;
    glm::dvec3 origin_ecef;       ///< Current RTC origin
    f64 local_blend;               ///< 0=global, 1=local
    bool use_local_frame;          ///< True if should use ENU/local
  };
  CoordState getCoordState() const;

  /// Force a specific scale level (disables auto-switch until next update).
  void forceLevel(ScaleLevel level);

  /// Get config.
  const ScaleSwitchConfig& getConfig() const { return config_; }

 private:
  ScaleSwitchConfig config_;
  ScaleLevel current_level_ = ScaleLevel::Global;
  ScaleTransition transition_;
  Geodetic last_camera_geo_{};
  f64 last_altitude_ = 1e7;
  RTCOrigin rtc_origin_;
};

} // namespace hongtu::coordinate
