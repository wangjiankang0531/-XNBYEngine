#pragma once

#include <hongtu/renderer/camera/camera_base.hpp>
#include <glm/glm.hpp>

namespace hongtu::renderer {

/// Local-mode camera for flat / non-earth worlds.
///
/// Two interaction styles share the same class:
///   - `Orbit`   : camera revolves around a fixed target at a given distance.
///                 Mouse drag rotates around the target; scroll zooms.
///   - `FreeFly` : camera owns its position and yaw/pitch; WASD-style motion
///                 moves forward/right/up in the camera's local frame.
///
/// All world-space coordinates are plain f64 meters (no ellipsoid, no ECEF).
/// The camera does not own a PrecisionOrigin — pass one explicitly to
/// `getRTCViewMatrix` when float32 precision recovery is needed. For small
/// worlds (< ~10 km from origin), float32 is already adequate and the RTC
/// step can be skipped.
class LocalCamera : public CameraBase {
 public:
  enum class Mode { FreeFly, Orbit };

  LocalCamera();

  // ---------------------------------------------------------------------------
  // Configuration
  // ---------------------------------------------------------------------------

  void setScreenSize(f32 width, f32 height);
  void setMode(Mode m) { mode_ = m; }
  Mode getMode() const { return mode_; }

  // ---------------------------------------------------------------------------
  // FreeFly placement
  // ---------------------------------------------------------------------------

  void setPosition(const glm::dvec3& p) { position_ = p; }
  void setYawPitch(f64 yaw_rad, f64 pitch_rad);
  glm::dvec3 getPosition() const { return position_; }
  f64        getYaw()      const { return yaw_; }
  f64        getPitch()    const { return pitch_; }

  /// Translate in the camera's local frame.
  void moveForward(f64 meters);
  void moveRight(f64 meters);
  void moveUp(f64 meters);

  // ---------------------------------------------------------------------------
  // Orbit placement
  // ---------------------------------------------------------------------------

  /// Set orbit target + distance, with optional yaw/pitch.
  /// pitch_rad < 0 looks down at the target; pitch_rad = -π/2 is straight down.
  void lookAtTarget(const glm::dvec3& target, f64 distance,
                    f64 yaw_rad = 0.0, f64 pitch_rad = -0.5);

  glm::dvec3 getOrbitTarget() const { return orbit_target_; }
  f64        getDistance()    const { return distance_; }

  // ---------------------------------------------------------------------------
  // Input handlers
  // ---------------------------------------------------------------------------

  /// Begin a drag (records the cursor position).
  void onMouseDown(f32 sx, f32 sy);

  /// Continue a drag — in Orbit mode rotates around the target,
  /// in FreeFly mode adjusts yaw/pitch.
  void onMouseMove(f32 sx, f32 sy);

  /// End the drag.
  void onMouseUp();

  /// Right-button drag adjusts yaw/pitch in either mode.
  void onRightDrag(f32 dx, f32 dy);

  /// Mouse wheel: zoom (orbit) or move forward (free-fly).
  void onMouseScroll(f32 delta);

  void update(f32 dt);

  // ---------------------------------------------------------------------------
  // CameraBase overrides
  // ---------------------------------------------------------------------------

  glm::dvec3 getEyeWorld()    const override;
  glm::dvec3 getTargetWorld() const override;
  glm::dvec3 getUpWorld()     const override;

  /// Pick against the ground plane (z = 0). Returns the world-space point
  /// where the screen ray hits the plane, or nullopt if the ray misses
  /// (looking up).
  std::optional<PickHit>
      pickWorld(double sx, double sy,
                double screen_w, double screen_h) const override;

 private:
  /// Forward vector derived from yaw and pitch in world space.
  glm::dvec3 forward() const;

  Mode       mode_{Mode::Orbit};

  // FreeFly state
  glm::dvec3 position_{0.0, 0.0, 10.0};
  f64        yaw_{0.0};       // radians around +Z (up)
  f64        pitch_{-0.5};    // radians; negative looks down

  // Orbit state
  glm::dvec3 orbit_target_{0.0, 0.0, 0.0};
  f64        distance_{10.0};

  // Shared
  glm::dvec3 up_{0.0, 0.0, 1.0};
  f32        screen_w_{1280.0f}, screen_h_{720.0f};

  // Drag tracking — screen-delta based, no globe-pick.
  bool       dragging_{false};
  glm::vec2  last_mouse_{0.0f, 0.0f};
};

}  // namespace hongtu::renderer
