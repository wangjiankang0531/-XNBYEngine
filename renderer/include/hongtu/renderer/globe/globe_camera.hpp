#pragma once

#include <glm/glm.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/renderer/camera/camera_base.hpp>

#include <optional>

namespace hongtu::renderer {

  /// Globe-specific orbit camera.
  ///
  /// Interaction model (Google Earth style):
  ///   - Left drag  : geodetic drag — the globe point under the cursor stays under it.
  ///   - Right drag : orbit — changes heading and pitch around the target.
  ///   - Scroll     : smooth zoom.
  ///   - Release    : inertia — globe coasts and decelerates after a fast drag.
  ///
  /// All input methods take absolute screen coordinates. Call setScreenSize()
  /// whenever the viewport changes.
  class GlobeCamera : public CameraBase {
  public:
    // Expose CameraBase's RTC-view overload alongside the zero-arg one below.
    using CameraBase::getRTCViewMatrix;

    GlobeCamera();

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------

    /// Must be called before any input methods. Updates projection for unprojection.
    void setScreenSize(f32 width, f32 height);

    /// Immediately set camera to look at a geodetic target.
    void lookAt(const coordinate::Geodetic& target, f64 distance_meters, f64 heading_deg = 0.0, f64 pitch_deg = -90.0);

    /// Smoothly animate to a new target.
    void flyTo(const coordinate::Geodetic& target, f64 distance_meters, f64 duration_seconds = 3.0);

    bool isFlying() const;

    /// Enable or disable interactive mouse controls.
    void setInteractive(bool enable);

    /// Override the orbit-distance floor (default = 200 m). Useful for city /
    /// architectural demos that need to push in closer than the standard
    /// "stay above buildings" safety margin. Passing a value larger than the
    /// far limit gets clamped down. The setter immediately re-clamps the
    /// current distance to keep it valid.
    void setMinDistance(f64 meters);

    /// Override the projection used by globe rendering. This is intentionally
    /// separate from the orbit pose so a global-to-local transition can share
    /// exactly the same FOV and depth range with a second renderer drawing in a
    /// local ENU frame. Values are sanitized to a valid perspective frustum.
    void setProjectionOverride(f64 vertical_fov_deg, f64 near_meters, f64 far_meters);

    /// Return to the altitude-derived globe projection policy.
    void clearProjectionOverride();

    bool hasProjectionOverride() const {
      return projection_override_enabled_;
    }

    /// Pin screen-up to a caller-provided ECEF direction. Global/local composite
    /// renderers use the ENU anchor's Up basis here so their view matrices have
    /// identical roll even when the camera is thousands of kilometres away.
    void setViewUpOverride(const glm::dvec3& up_ecef);
    void clearViewUpOverride();

    // -------------------------------------------------------------------------
    // Input (call from the application event loop)
    // -------------------------------------------------------------------------

    /// Left mouse button pressed at screen position (sx, sy).
    /// Picks the globe point under the cursor; stops any ongoing inertia.
    void onMouseDown(f32 sx, f32 sy);

    /// Mouse moved while left button is held.
    /// Rotates the globe so the originally picked point stays under the cursor.
    void onMouseMove(f32 sx, f32 sy);

    /// Left mouse button released.
    /// Transfers the last drag velocity into a decaying inertia spin.
    void onMouseUp();

    /// Immediately cancel any coasting inertia. Call when another interaction
    /// (e.g. an editor gizmo drag) takes over so the globe doesn't keep
    /// drifting underneath it.
    void stopInertia();

    /// Right mouse button drag — orbits heading and pitch around the target.
    void onRightDrag(f32 dx, f32 dy);

    /// Scroll wheel — zooms smoothly.
    /// @param delta  Positive = zoom in (closer), negative = zoom out.
    void onMouseScroll(f32 delta);

    // -------------------------------------------------------------------------
    // Update / matrices
    // -------------------------------------------------------------------------

    void update(f32 dt);

    // getViewMatrix() inherited from CameraBase — same math via the three
    // pure-virtual overrides (getEyeWorld / getTargetWorld / getUpWorld).
    glm::mat4 getProjectionMatrix(f32 aspect_ratio) const override;

    // CameraBase overrides — world-space (ECEF) primitives.
    glm::dvec3 getEyeWorld() const override;
    glm::dvec3 getTargetWorld() const override;
    glm::dvec3 getUpWorld() const override;

    glm::dvec3 getPositionECEF() const;
    coordinate::Geodetic getPositionGeodetic() const;
    f64 getAltitude() const;

    // Read-only orbit state. NOTE: heading_/pitch_/target_ are only fresh
    // immediately after lookAt()/flyTo(). After right-drag (FPS look) or
    // wheel (FPS dolly) they no longer reflect the live view — use
    // captureOrbitPose() if you need the current camera's equivalent
    // orbit parameters.
    const coordinate::Geodetic& getTargetGeodetic() const {
      return target_;
    }
    f64 getHeadingRad() const {
      return heading_;
    }
    f64 getPitchRad() const {
      return pitch_;
    }
    f64 getDistance() const {
      return distance_;
    }

    /// Snapshot of the camera's current view expressed as orbit parameters
    /// suitable for round-tripping through lookAt(). Used by the editor's
    /// 获取当前视角 button so the saved heading/pitch reflect what the
    /// user is actually looking at (after right-drag tilts and wheel
    /// dollies), not the stale values from the last lookAt() call.
    struct OrbitPose {
      coordinate::Geodetic target; // surface point the camera is aimed at (alt=0)
      f64 distance_m;              // straight-line eye→target distance
      f64 heading_deg;             // azimuth of (eye - target) in target's tangent frame
      f64 pitch_deg;               // elevation of (eye - target); -90° = eye directly above
    };
    OrbitPose captureOrbitPose() const;

    /// EXACT view snapshot — the full internal pose state that determines the
    /// rendered view (eye + aim target + heading + FPS look angles + distance).
    /// Unlike captureOrbitPose()→lookAt(), restorePoseState() reproduces the
    /// view bit-for-bit with NO orbit decomposition, NO pitch/distance clamp and
    /// NO FPS-look reset — so 获取当前视角 → 定位到默认视角 round-trips exactly,
    /// even after wheel dollies / right-drag tilts. Used by the editor home view.
    struct PoseState {
      glm::dvec3 eye_ecef{0.0};
      coordinate::Geodetic target{}; // aim target (drives forward direction)
      f64 heading_rad{0.0};
      f64 view_yaw_rad{0.0};
      f64 view_pitch_rad{0.0};
      f64 distance{1.0e7};
    };
    PoseState capturePoseState() const;
    void restorePoseState(const PoseState& s);

    /// Pick against the WGS84 mean-radius sphere (ECEF). Unified picking API
    /// shared with `LocalCamera::pickWorld`.
    std::optional<PickHit> pickWorld(double sx, double sy, double screen_w, double screen_h) const override;

    coordinate::RTCOrigin& getRTCOrigin();
    const coordinate::RTCOrigin& getRTCOrigin() const;
    /// Zero-arg overload: convenience wrapper using the camera's own RTC origin.
    /// Forwards to CameraBase::getRTCViewMatrix(rtc_origin_).
    glm::mat4 getRTCViewMatrix() const;

  private:
    glm::dvec3 computeCameraECEF() const;
    void updateRTCOrigin();

    /// Stable lookAt up vector: north-at-target rotated by heading_.
    /// Always horizontal → never parallel to the downward view direction.
    glm::dvec3 headingUp() const;

    /// Unproject screen position (sx, sy) to a unit ray direction in ECEF using
    /// the given inverse-view matrix. Caller chooses whether to use the current
    /// view or a cached one — during left-drag we use the view captured at
    /// mouse-down so successive picks come from a stable camera frame.
    glm::dvec3 screenToRayDir(f32 sx, f32 sy, const glm::dmat4& inv_view) const;

    /// Ray–sphere intersection against a sphere of the given radius.
    /// Falls back to the tangent point on that same sphere if the ray misses,
    /// keeping drags stable when the cursor moves off the limb.
    /// `cam_ecef` is the ray origin and `inv_view` defines the ray direction —
    /// pass cached values during drag so the picking frame stays fixed.
    std::optional<glm::dvec3> pickGlobe(
        f32 sx, f32 sy, f64 sphere_radius, const glm::dvec3& cam_ecef, const glm::dmat4& inv_view) const;

    /// Composite FPS view basis at the eye (forward + up), built by taking
    /// "look at orbit target" and rotating by view_yaw_rad_ around local up
    /// then view_pitch_rad_ around the resulting right axis. Returns
    /// orthogonal fwd + up. Handles the orbit_pitch=±90° degenerate case
    /// via heading_. Only invoked when at least one view rotation is
    /// non-zero — the zero-extras path returns from the legacy branch.
    struct ViewBasis_ {
      glm::dvec3 fwd, up;
    };
    ViewBasis_ computeViewBasis_() const;

    /// Apply a small rotation to target_: rotate ECEF by (axis, angle).
    void rotateTarget(const glm::dvec3& axis, f64 angle);

    // -------------------------------------------------------------------------
    // Orbit parameters
    // -------------------------------------------------------------------------
    coordinate::Geodetic target_{};
    // Independent eye position in ECEF. Initially derived from orbit
    // params by lookAt/flyTo via computeCameraECEF(), then maintained as
    // the source of truth: right-drag leaves it alone (FPS look), wheel
    // translates it together with target_ by the same vector (FPS dolly),
    // left-drag pan rotates it together with target_ around earth.
    // Without this independence the wheel had to rely on orbit math to
    // re-derive eye after each target move, which produced a small
    // tangent-frame-mismatch error per scroll and drifted the screen-
    // center world point as the user zoomed.
    glm::dvec3 eye_ecef_{0.0};
    f64 heading_{0.0};  // radians, 0 = north, clockwise (informational after first lookAt)
    f64 pitch_{0.0};    // radians, -π/2 = straight down (informational after first lookAt)
    f64 distance_{1e7}; // metres from target
    // FPS-style view rotation offsets applied on top of the orbit-determined
    // "look-at-target" direction. The eye position is NOT affected — only
    // the look direction rotates. Right-mouse drag dx/dy drive these so a
    // purely vertical drag produces purely vertical look rotation and no
    // lateral camera movement (previously dx mutated heading_ which orbited
    // the camera around target, looking like the scene was panning).
    // Both reset to 0 on lookAt/flyTo so a fresh teleport starts looking
    // straight at the new target.
    f64 view_yaw_rad_{0.0};
    f64 view_pitch_rad_{0.0};
    f64 target_distance_{1e7}; // smooth-zoom destination
    f64 min_distance_{200.0};  // orbit floor — overridable via setMinDistance()

    bool projection_override_enabled_{false};
    f64 projection_vertical_fov_deg_{60.0};
    f64 projection_near_m_{0.5};
    f64 projection_far_m_{1.0e7};
    bool view_up_override_enabled_{false};
    glm::dvec3 view_up_override_ecef_{0.0, 0.0, 1.0};

    // -------------------------------------------------------------------------
    // Fly-to animation
    // -------------------------------------------------------------------------
    bool flying_{false};
    f64 fly_elapsed_{0.0};
    f64 fly_duration_{3.0};
    coordinate::Geodetic fly_from_target_{};
    f64 fly_from_heading_{0.0};
    f64 fly_from_pitch_{0.0};
    f64 fly_from_distance_{0.0};
    coordinate::Geodetic fly_to_target_{};
    f64 fly_to_heading_{0.0};
    f64 fly_to_pitch_{0.0};
    f64 fly_to_distance_{0.0};

    // -------------------------------------------------------------------------
    // Interaction state
    // -------------------------------------------------------------------------
    bool interactive_{true};
    f32 screen_width_{1280.0f};
    f32 screen_height_{720.0f};

    // Geodetic drag — absolute mode (DeepEye style)
    bool has_pick_{false};
    glm::dvec3 initial_pick_{0.0};        // ECEF hit at mouse-down; NEVER updated during drag
    glm::dvec3 initial_target_ecef_{0.0}; // target ECEF at mouse-down; rotation applied to this
    glm::dvec3 initial_eye_ecef_{0.0};    // eye ECEF at mouse-down; rotated alongside target
    // Screen position at mouse-down — used by the close-range tangent-
    // plane pan path to compute cursor delta in pixels (the absolute-
    // pick rotation path doesn't need this because it works on the
    // freshly-picked B vs cached A).
    f32 initial_cursor_sx_{0.0f};
    f32 initial_cursor_sy_{0.0f};
    f64 pick_sphere_radius_{6378137.0}; // glm::length(initial_pick_); constant during drag
    glm::dvec3 prev_hit_{0.0};          // previous onMouseMove hit (for per-event inertia delta)
    // Camera origin and inverse-view captured at mouse-down. Used for every
    // pickGlobe call during the drag so the picking frame stays fixed even as
    // target_ rotates each event — otherwise the per-event view matrix shifts
    // and B = pickGlobe(cursor) collapses toward A, producing jitter instead of
    // a cumulative drag.
    glm::dvec3 drag_cam_ecef_{0.0};
    glm::dmat4 drag_inv_view_{1.0};

    // Per-frame rotation recorded so onMouseUp() can seed inertia
    glm::dvec3 frame_axis_{0.0};
    f64 frame_angle_{0.0};
    f64 frame_dt_{1.0 / 60.0}; // estimated frame time

    // Inertia after mouse release
    glm::dvec3 inertia_axis_{0.0};
    f64 inertia_speed_{0.0}; // radians/second
    bool inertia_active_{false};

    // -------------------------------------------------------------------------
    // RTC
    // -------------------------------------------------------------------------
    coordinate::RTCOrigin rtc_origin_;
  };

} // namespace hongtu::renderer
