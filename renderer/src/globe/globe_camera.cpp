#include <glm/gtc/matrix_transform.hpp>
#include <hongtu/coordinate/ellipsoid.hpp>
#include <hongtu/renderer/globe/globe_camera.hpp>
#include <hongtu/renderer/picking/ray_caster.hpp> // PickHit
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace hongtu::renderer {

  namespace {

    // Default for `GlobeCamera::min_distance_` — chosen so day-to-day globe
    // orbiting stays clear of typical building heights without needing the
    // per-app override. Demos that need to push in closer (e.g. earth_demo
    // --city) can dial it down via `setMinDistance()`.
    constexpr f64 kDefaultMinDistance = 200.0; // 200 m
    // Above ~30 000 km the earth shrinks to a few hundred pixels, and
    // front-vs-back tile depth values get crushed into the same z-buffer
    // bucket → tile boundaries flicker as small black specks. Cap there so
    // the user can't reach that degenerate regime via wheel zoom.
    constexpr f64 kMaxDistance = 30'000'000.0; // 30 000 km
    // Pitch limits for a target-orbit camera.
    // kMaxPitch MUST stay slightly below horizon. At exactly 0° the
    // eye-target offset is entirely in the target's tangent plane, and
    // headingUp() returns a vector in that same plane — when heading
    // happens to align with the offset direction the two become parallel
    // and glm::lookAt produces a degenerate (NaN-ish) view matrix. The 5°
    // margin gives the offset a non-zero radial (z) component so the cross
    // product stays well-conditioned. This was the original committed
    // value; do NOT raise it to 0° without first fixing lookAt to use a
    // non-degenerate up vector near horizon.
    constexpr f64 kMinPitch = -glm::pi<f64>() / 2.0; // straight down
    constexpr f64 kMaxPitch = glm::radians(-5.0);    // 5° below horizon
    constexpr f64 kRightDragSens = 0.003;            // radians per pixel for right-drag orbit
    constexpr f64 kFovYDeg = 60.0;                   // must match getProjectionMatrix
    constexpr f64 kEarthRadius = 6378137.0;          // WGS84 equatorial semi-axis (m)
    constexpr f64 kInertiaDecay = 3.0;               // speed halves every ~0.33 s (e^-3*t)
    constexpr f64 kInertiaMinSpd = 1e-5;             // rad/s below which inertia stops
    constexpr f64 kZoomSmoothing = 5.0;              // zoom spring stiffness — lower = smoother approach

    f64 smoothstep(f64 t) {
      t = std::clamp(t, 0.0, 1.0);
      return t * t * (3.0 - 2.0 * t);
    }

    f64 lerp(f64 a, f64 b, f64 t) {
      return a + (b - a) * t;
    }

    /// Möller–Trumbore ray–sphere test.
    /// Returns t of nearest positive intersection, or -1 if none.
    f64 raySphere(const glm::dvec3& origin, const glm::dvec3& dir, f64 radius) {
      f64 b = 2.0 * glm::dot(origin, dir);
      f64 c = glm::dot(origin, origin) - radius * radius;
      f64 disc = b * b - 4.0 * c; // a=1 because dir is normalised
      if (disc < 0.0)
        return -1.0;
      f64 sq = std::sqrt(disc);
      f64 t1 = (-b - sq) * 0.5;
      f64 t2 = (-b + sq) * 0.5;
      if (t1 > 0.0)
        return t1;
      if (t2 > 0.0)
        return t2;
      return -1.0;
    }

  } // namespace

  // ============================================================================
  // Constructor
  // ============================================================================

  GlobeCamera::GlobeCamera() {
    target_ = coordinate::Geodetic{0.0, 0.0, 0.0};
    heading_ = 0.0;
    pitch_ = kMinPitch;
    distance_ = target_distance_ = 1e7;
  }

  // ============================================================================
  // Configuration
  // ============================================================================

  void GlobeCamera::setScreenSize(f32 width, f32 height) {
    screen_width_ = width;
    screen_height_ = height;
  }

  void GlobeCamera::setProjectionOverride(f64 vertical_fov_deg, f64 near_meters, f64 far_meters) {
    projection_vertical_fov_deg_ = std::clamp(vertical_fov_deg, 1.0, 179.0);
    projection_near_m_ = std::max(near_meters, 0.001);
    projection_far_m_ = std::max(far_meters, projection_near_m_ * 1.001);
    projection_override_enabled_ = true;
  }

  void GlobeCamera::clearProjectionOverride() {
    projection_override_enabled_ = false;
  }

  void GlobeCamera::setViewUpOverride(const glm::dvec3& up_ecef) {
    const f64 length = glm::length(up_ecef);
    if (length <= 1.0e-12) {
      clearViewUpOverride();
      return;
    }
    view_up_override_ecef_ = up_ecef / length;
    view_up_override_enabled_ = true;
  }

  void GlobeCamera::clearViewUpOverride() {
    view_up_override_enabled_ = false;
  }

  void GlobeCamera::lookAt(const coordinate::Geodetic& target, f64 distance_meters, f64 heading_deg, f64 pitch_deg) {
    target_ = target;
    heading_ = glm::radians(heading_deg);
    pitch_ = std::clamp(glm::radians(pitch_deg), kMinPitch, kMaxPitch);
    distance_ = std::clamp(distance_meters, min_distance_, kMaxDistance);
    target_distance_ = distance_;
    // Seed independent eye from orbit math. After this, eye_ecef_ is the
    // source of truth — wheel/drag modify it directly.
    eye_ecef_ = computeCameraECEF();
    flying_ = false;
    // Fully reset inertia state so a fresh teleport doesn't carry over residual
    // velocity from an earlier drag — without this, axis/speed left over from a
    // prior `onMouseUp()` could decay into the new orientation over the next
    // few seconds and slowly drift the camera off-target.
    inertia_active_ = false;
    inertia_speed_ = 0.0;
    inertia_axis_ = glm::dvec3(0.0);
    // Reset the FPS view tilt so a fresh teleport always lands looking AT
    // the new target; any leftover tilt from the previous session would
    // otherwise have the camera staring off into space.
    view_pitch_rad_ = 0.0;
    view_yaw_rad_ = 0.0;
    updateRTCOrigin();
  }

  void GlobeCamera::flyTo(const coordinate::Geodetic& target, f64 distance_meters, f64 duration_seconds) {
    fly_from_target_ = target_;
    fly_from_heading_ = heading_;
    fly_from_pitch_ = pitch_;
    fly_from_distance_ = distance_;
    // Same logic as lookAt — fly should arrive looking at the destination.
    view_pitch_rad_ = 0.0;
    fly_to_target_ = target;
    fly_to_heading_ = 0.0;
    fly_to_pitch_ = kMinPitch;
    fly_to_distance_ = std::clamp(distance_meters, min_distance_, kMaxDistance);
    fly_duration_ = duration_seconds;
    fly_elapsed_ = 0.0;
    flying_ = true;
    inertia_active_ = false;
  }

  bool GlobeCamera::isFlying() const {
    return flying_;
  }

  void GlobeCamera::setInteractive(bool enable) {
    interactive_ = enable;
  }

  void GlobeCamera::setMinDistance(f64 meters) {
    // Floor at 1 m to avoid degenerate lookAt math (eye == target) on aggressive
    // overrides. Cap to half the far limit to leave room for it to still be a
    // valid clamp range.
    min_distance_ = std::clamp(meters, 1.0, kMaxDistance * 0.5);
    // Re-clamp the live + smoothed targets so a freshly-lowered floor lets
    // pending zoom-in actually take effect.
    distance_ = std::clamp(distance_, min_distance_, kMaxDistance);
    target_distance_ = std::clamp(target_distance_, min_distance_, kMaxDistance);
  }

  // ============================================================================
  // Private helpers
  // ============================================================================

  glm::dvec3 GlobeCamera::screenToRayDir(f32 sx, f32 sy, const glm::dmat4& inv_view) const {
    // NDC: origin at centre, Y up, X right
    double ndcX = 2.0 * sx / screen_width_ - 1.0;
    double ndcY = 1.0 - 2.0 * sy / screen_height_; // flip Y (SDL origin = top-left)

    // View-space ray direction using the camera's FOV.
    // glm::perspective uses vertical FOV; camera looks down -Z in view space.
    double half_fov_y = glm::radians(kFovYDeg * 0.5);
    double tan_half = std::tan(half_fov_y);
    double aspect = static_cast<f64>(screen_width_) / screen_height_;

    glm::dvec3 view_dir = glm::normalize(glm::dvec3(ndcX * tan_half * aspect, // view X
                                                    ndcY * tan_half,          // view Y
                                                    -1.0                      // look in -Z
                                                    ));

    // Rotate from view space to ECEF world space using the supplied frame.
    glm::dvec4 world = inv_view * glm::dvec4(view_dir, 0.0); // direction (w=0)
    return glm::normalize(glm::dvec3(world));
  }

  std::optional<glm::dvec3> GlobeCamera::pickGlobe(
      f32 sx, f32 sy, f64 sphere_radius, const glm::dvec3& cam_ecef, const glm::dmat4& inv_view) const {
    // Pick against the WGS84 ellipsoid, not the requested `sphere_radius`.
    //
    // Background: the legacy sphere pick failed two ways. (a) Using mean
    // radius made the hit lie ~hundreds of metres below the visible
    // surface at oblique views — placement landed off-cursor. (b) Using
    // equatorial radius put the camera INSIDE the sphere at low altitude
    // on non-equator latitudes, so ray-sphere returned the far-side exit
    // point and we drew the gizmo / dragged the globe as if the cursor
    // had clicked the antipode — which is exactly the "viewport flipped"
    // bug screenshot. Picking the actual ellipsoid avoids both: the
    // surface is always RIGHT THERE regardless of latitude, and the eye
    // is always above it (since geodetic altitude > 0 means outside the
    // ellipsoid by construction).
    //
    // Method: warp ECEF along Z so the ellipsoid becomes a sphere of
    // radius `a`, intersect, then unwarp the hit.
    constexpr f64 a = 6378137.0;
    constexpr f64 b = 6356752.314245;
    constexpr f64 sz = a / b; // ≈ 1.003364
    (void)sphere_radius;      // legacy param — kept for ABI parity; ignored

    glm::dvec3 dir = screenToRayDir(sx, sy, inv_view);

    glm::dvec3 eye_s = glm::dvec3(cam_ecef.x, cam_ecef.y, cam_ecef.z * sz);
    glm::dvec3 dir_s = glm::dvec3(dir.x, dir.y, dir.z * sz);
    f64 dir_s_len = glm::length(dir_s);
    if (dir_s_len < 1e-12)
      return std::nullopt;
    glm::dvec3 dir_sn = dir_s / dir_s_len;

    f64 t = raySphere(eye_s, dir_sn, a);
    if (t > 0.0) {
      glm::dvec3 hit_s = eye_s + t * dir_sn;
      return glm::dvec3(hit_s.x, hit_s.y, hit_s.z / sz);
    }

    // Fallback: project the ray's closest-approach point onto the same
    // (squished) sphere — keeps the grab point sliding along the limb
    // rather than jumping when the cursor moves off the globe edge.
    f64 t_closest = -glm::dot(eye_s, dir_sn);
    if (t_closest > 0.0) {
      glm::dvec3 p = eye_s + t_closest * dir_sn;
      f64 d = glm::length(p);
      if (d > 1e-6) {
        glm::dvec3 hit_s = p * (a / d);
        return glm::dvec3(hit_s.x, hit_s.y, hit_s.z / sz);
      }
    }
    return std::nullopt;
  }

  std::optional<PickHit> GlobeCamera::pickWorld(double sx, double sy, double screen_w, double screen_h) const {
    // pickGlobe handles the ellipsoid math (Z-squish trick) — this just
    // wraps it for the LocalCamera-parity API. The sphere_radius arg is
    // ignored inside pickGlobe but kept for ABI; pass a sensible value.
    (void)screen_w;
    (void)screen_h;
    glm::dmat4 inv_view = glm::inverse(getViewMatrix());
    glm::dvec3 eye = getEyeWorld();
    auto p = pickGlobe(static_cast<f32>(sx), static_cast<f32>(sy), 6378137.0, eye, inv_view);
    if (!p)
      return std::nullopt;
    PickHit hit;
    hit.point = *p;
    hit.t = glm::length(*p - eye);
    return hit;
  }

  void GlobeCamera::rotateTarget(const glm::dvec3& axis, f64 angle) {
    glm::dmat4 rot = glm::rotate(glm::dmat4(1.0), angle, axis);
    glm::dvec3 target_ecef = coordinate::geodeticToECEF(target_);
    glm::dvec3 new_ecef = glm::dvec3(rot * glm::dvec4(target_ecef, 1.0));
    target_ = coordinate::ecefToGeodetic(new_ecef);
    // Eye rotates with target so the relative geometry (and inertia
    // behaviour) is preserved — without this the eye would drift relative
    // to the spinning target during inertia and the orbit would degenerate.
    eye_ecef_ = glm::dvec3(rot * glm::dvec4(eye_ecef_, 1.0));
  }

  // ============================================================================
  // Input
  // ============================================================================

  void GlobeCamera::onMouseDown(f32 sx, f32 sy) {
    if (!interactive_ || flying_)
      return;
    inertia_active_ = false;
    inertia_speed_ = 0.0;
    frame_angle_ = 0.0;

    // Freeze the picking frame at mouse-down. All onMouseMove picks during this
    // drag use this same camera origin and inverse-view, so B = pickGlobe(cursor)
    // measures the cursor's globe position from a stable viewpoint — angle(A→B)
    // then grows monotonically with the cumulative cursor displacement instead
    // of collapsing toward zero each event (the source of the drag jitter).
    drag_cam_ecef_ = eye_ecef_; // use the actual eye, not orbit-derived
    drag_inv_view_ = glm::inverse(getViewMatrix());
    initial_cursor_sx_ = sx;
    initial_cursor_sy_ = sy;

    auto hit = pickGlobe(sx, sy, kEarthRadius, drag_cam_ecef_, drag_inv_view_);
    if (hit) {
      initial_pick_ = *hit;
      pick_sphere_radius_ = glm::length(*hit);
      initial_target_ecef_ = coordinate::geodeticToECEF(target_);
      initial_eye_ecef_ = eye_ecef_;
      prev_hit_ = *hit;
      has_pick_ = true;
    } else {
      has_pick_ = false;
    }
  }

  void GlobeCamera::onMouseMove(f32 sx, f32 sy) {
    if (!interactive_ || flying_ || !has_pick_)
      return;

    // Close-range pan: at low altitude the absolute-pick rotation
    // (cursor → sphere hit → angle from initial_pick) blows up the
    // moment the view tilts, because pixels near the horizon map to
    // ground points several km away. Two cursor positions one pixel
    // apart can subtend a sizable earth-centre angle, and the camera
    // appears to fly across the map.
    //
    // Below ~2 km altitude we drop the pick entirely and translate
    // both eye and target in the camera-local tangent plane by
    // (cursor delta) × (metres per pixel at this altitude). Look
    // direction is preserved (both move by the same vector), so the
    // dragged feature stays under the cursor without any earth-centre
    // rotation.
    coordinate::Geodetic eye_geo = coordinate::ecefToGeodetic(eye_ecef_);
    // Threshold bumped from 2 km → 50 km after the user reported "0 altitude
    // still flies". The HUD shows `home_altitude_m` from the doc, not the
    // live eye altitude, so the user was likely at a few-km-actual altitude
    // (the wheel parked them just above the 50 m floor for the surface they
    // were AIMED at, but their oblique view kept eye_alt high) — outside
    // the old 2 km window, so the pick-rotation path took over and the
    // horizon picks blew up. 50 km is high enough to cover anything that
    // visually reads as "close to the ground", below which absolute pick
    // is unreliable at oblique angles anyway.
    constexpr f64 kLowAltitudeM = 50000.0;
    if (eye_geo.alt < kLowAltitudeM) {
      f64 dx = static_cast<f64>(sx - initial_cursor_sx_);
      f64 dy = static_cast<f64>(sy - initial_cursor_sy_);

      // Screen-axis basis at eye, derived from the live view direction.
      auto basis = computeViewBasis_();
      glm::dvec3 right = glm::normalize(glm::cross(basis.fwd, basis.up));

      // Metres-per-pixel at this altitude. Vertical FOV defines the
      // visible world height = 2 * h * tan(fov/2); pixel scale = that
      // / screen_height. Use altitude-above-ellipsoid so dragging in a
      // low-orbit camera doesn't drift to 6e6 m steps.
      constexpr f64 kFovYRad = glm::radians(60.0);
      f64 mpp = (screen_height_ > 0.0f)
                    ? (2.0 * std::max(eye_geo.alt, 1.0) * std::tan(kFovYRad * 0.5) / static_cast<f64>(screen_height_))
                    : 0.0;

      // Camera moves OPPOSITE to the cursor displacement so the world
      // point under cursor follows the cursor. Screen y grows downward,
      // hence the +dy * up term (cursor down → camera moves up =
      // toward the horizon, world appears to scroll downward).
      glm::dvec3 pan = -right * dx * mpp + basis.up * dy * mpp;

      eye_ecef_ = initial_eye_ecef_ + pan;
      glm::dvec3 new_target_ecef = initial_target_ecef_ + pan;
      target_ = coordinate::ecefToGeodetic(new_target_ecef);
      // Inertia isn't seeded for tangent-plane drags — it would slide
      // off into space without a globe-rotation interpretation.
      frame_angle_ = 0.0;
      updateRTCOrigin();
      return;
    }

    auto hit = pickGlobe(sx, sy, pick_sphere_radius_, drag_cam_ecef_, drag_inv_view_);
    if (!hit)
      return;

    // Absolute mode (DeepEye style): always rotate initial_target_ecef_ by the
    // total angle from initial_pick_ to the current cursor hit.  Because
    // initial_pick_ is never updated, multiple SDL events per frame produce a
    // stable, monotonic result — no oscillation.
    glm::dvec3 A = glm::normalize(initial_pick_); // fixed grab point
    glm::dvec3 B = glm::normalize(*hit);          // current cursor hit

    glm::dvec3 axis = glm::cross(B, A); // cross(end, start) matches DeepEye
    f64 axis_len = glm::length(axis);
    if (axis_len < 1e-12)
      return;
    axis /= axis_len;

    f64 angle = std::acos(std::clamp(glm::dot(A, B), -1.0, 1.0));

    glm::dmat4 rot = glm::rotate(glm::dmat4(1.0), angle, axis);
    glm::dvec3 new_ecef = glm::dvec3(rot * glm::dvec4(initial_target_ecef_, 1.0));
    target_ = coordinate::ecefToGeodetic(new_ecef);
    // Rotate eye by the SAME transform from its mouse-down position so the
    // orbit relationship is preserved during the pan — without this the
    // eye stays anchored while target spins and the camera would tilt
    // away from the dragged feature.
    eye_ecef_ = glm::dvec3(rot * glm::dvec4(initial_eye_ecef_, 1.0));

    // Record per-event delta (prev_hit → current hit) for inertia seeding.
    {
      glm::dvec3 P = glm::normalize(prev_hit_);
      glm::dvec3 Q = glm::normalize(*hit);
      glm::dvec3 da = glm::cross(Q, P);
      f64 dlen = glm::length(da);
      if (dlen > 1e-12) {
        frame_axis_ = da / dlen;
        frame_angle_ = std::acos(std::clamp(glm::dot(P, Q), -1.0, 1.0));
      }
    }
    prev_hit_ = *hit;

    updateRTCOrigin();
  }

  void GlobeCamera::onMouseUp() {
    if (!interactive_)
      return;
    has_pick_ = false;

    // Seed inertia from the last recorded rotation.
    // frame_angle_/frame_dt_ gives radians/second; decay handles the rest.
    if (frame_dt_ > 0.0 && frame_angle_ > 1e-6) {
      inertia_axis_ = frame_axis_;
      inertia_speed_ = frame_angle_ / frame_dt_;
      inertia_active_ = true;
    }
    frame_angle_ = 0.0;
  }

  void GlobeCamera::stopInertia() {
    inertia_active_ = false;
    inertia_speed_ = 0.0;
    frame_angle_ = 0.0;
  }

  void GlobeCamera::onRightDrag(f32 dx, f32 dy) {
    if (!interactive_ || flying_)
      return;
    // Cancel any residual left-drag inertia — otherwise the scene keeps
    // rotating from the previous pan while the user is trying to tilt the
    // view, and the combined motion reads as "the scene is panning while
    // I'm looking up/down".
    inertia_active_ = false;
    inertia_speed_ = 0.0;
    // Pure FPS look: BOTH axes rotate the view direction only — eye stays
    // exactly where the orbit put it, so a vertical drag produces zero
    // lateral camera motion (the old `heading_ += dx*sens` orbited the
    // camera around the target horizontally, which looked like horizontal
    // panning whenever dy drags had any incidental dx).
    view_yaw_rad_ += static_cast<f64>(dx) * kRightDragSens;
    view_pitch_rad_ += static_cast<f64>(dy) * kRightDragSens;
    // Stop ~1° short of the poles to keep glm::lookAt non-degenerate.
    constexpr f64 kViewPitchClamp = glm::radians(89.0);
    view_pitch_rad_ = std::clamp(view_pitch_rad_, -kViewPitchClamp, kViewPitchClamp);
    // Yaw wraps freely — no clamp needed.
  }

  void GlobeCamera::onMouseScroll(f32 delta) {
    if (!interactive_ || flying_)
      return;
    // Kill leftover left-drag inertia immediately so target_ can't keep
    // rotating between wheel events and corrupt the geometry.
    inertia_active_ = false;
    inertia_speed_ = 0.0;

    delta = std::clamp(delta, -5.0f, 5.0f);

    // Simple radial altitude scaling.
    //
    // PREVIOUS attempt: "hit-anchored" zoom (cast view ray, move eye
    // fractionally toward sphere hit). That had a beautiful property
    // (zoom toward what you see), but it FAILED catastrophically when
    // target_ drifted across the globe (which happened naturally because
    // earlier versions of this function pinned target_ to the hit_point
    // every wheel event). At antipodal target the (eye - hit) vector is
    // ~12 000 km, multiplying by mul=2.7 launched the eye 22 000 km
    // into space; multiplying by 0.37 cut through the sphere and the
    // override clamped to 1 m and called it done — either way the wheel
    // felt broken.
    //
    // The pure altitude-scaling below has no failure modes: regardless
    // of target_ state, eye altitude scales by exp(-0.7*delta) per
    // notch (≈ ×0.5 in, ×2 out), clamped to [1 m, 50 000 km]. Look
    // direction is preserved (eye moves along its own radial). Whatever
    // is in the centre of the frame stays centred during zoom because
    // the radial vector through eye doesn't change direction as
    // altitude changes (top-down case); for oblique views the screen
    // centre point drifts slightly but that's the standard Google Earth
    // behaviour and is consistent.
    coordinate::Geodetic eye_geo = coordinate::ecefToGeodetic(eye_ecef_);

    // Unconditionally re-anchor target_ to the eye's nadir before applying
    // the altitude change. Even fresh-from-lookAt the orbit pitch leaves a
    // small horizontal offset between eye and target (~150 km for pitch=
    // -88° at 4 000 km orbit) — invisible from high altitude (2° tilt
    // from vertical) but as the wheel takes the eye down without moving
    // target_, the offset turns the view into a horizon shot (at 7 km
    // altitude, 150 km horizontal = 87° tilt = looking sideways at the
    // earth's edge). Snapping every notch keeps the look direction
    // pointing straight down at the eye's lat/lon throughout the zoom.
    target_ = coordinate::Geodetic{eye_geo.lat, eye_geo.lon, 0.0};

    spdlog::info("[wheel] delta={:.3f} alt_before={:.0f}m", static_cast<double>(delta), eye_geo.alt);

    // Altitude-aware exponent: above ~500 m we want the snappy ~50%
    // per-notch zoom users got used to in orbit / city view. Below 500 m
    // each notch should be much finer — close to a model that's only a
    // couple metres tall, halving altitude every click means going from
    // "10 m up" to "5 m up" in one wheel tick, which feels like a
    // teleport. Floor the scaling at 15% of the orbit exponent so we
    // still progress (5–10% alt change per notch) instead of stalling.
    f64 alt_scale = std::clamp(eye_geo.alt / 500.0, 0.15, 1.0);
    f64 mul = std::exp(-0.7 * alt_scale * static_cast<f64>(delta));
    f64 new_alt = std::clamp(eye_geo.alt * mul, 1.0, kMaxDistance);

    coordinate::Geodetic new_eye_geo = eye_geo;
    new_eye_geo.alt = new_alt;
    eye_ecef_ = coordinate::geodeticToECEF(new_eye_geo);

    spdlog::info("[wheel] AFTER: eye_alt={:.0f}m  (×{:.3f}/notch)", new_alt, mul);

    // Refresh distance_ off (eye, target_) so captureOrbitPose and the
    // smoothing spring stay consistent with the new eye position.
    distance_ = glm::length(eye_ecef_ - coordinate::geodeticToECEF(target_));
    target_distance_ = distance_;
  }

  // ============================================================================
  // Update
  // ============================================================================

  void GlobeCamera::update(f32 dt) {
    frame_dt_ = std::max(static_cast<f64>(dt), 1e-4);

    if (flying_) {
      fly_elapsed_ += static_cast<f64>(dt);
      f64 t = std::clamp(fly_elapsed_ / fly_duration_, 0.0, 1.0);
      f64 s = smoothstep(t);

      target_.lat = lerp(fly_from_target_.lat, fly_to_target_.lat, s);
      target_.lon = lerp(fly_from_target_.lon, fly_to_target_.lon, s);
      target_.alt = lerp(fly_from_target_.alt, fly_to_target_.alt, s);
      heading_ = lerp(fly_from_heading_, fly_to_heading_, s);
      pitch_ = lerp(fly_from_pitch_, fly_to_pitch_, s);

      f64 max_dist = std::max(fly_from_distance_, fly_to_distance_) * 1.5;
      f64 parabola = 4.0 * t * (1.0 - t);
      f64 linear = lerp(fly_from_distance_, fly_to_distance_, s);
      distance_ = lerp(linear, max_dist, parabola);
      target_distance_ = distance_;

      if (fly_elapsed_ >= fly_duration_) {
        target_ = fly_to_target_;
        heading_ = fly_to_heading_;
        pitch_ = fly_to_pitch_;
        distance_ = target_distance_ = fly_to_distance_;
        flying_ = false;
      }
      // During fly the orbit params are interpolated; re-derive eye each
      // frame so getEyeWorld()/getViewMatrix() reflect the in-flight pose.
      eye_ecef_ = computeCameraECEF();
    } else {
      // Smooth zoom: spring toward target_distance_
      f64 alpha = 1.0 - std::exp(-kZoomSmoothing * static_cast<f64>(dt));
      distance_ += (target_distance_ - distance_) * alpha;

      // Inertia spin after drag release
      if (inertia_active_ && inertia_speed_ > kInertiaMinSpd) {
        f64 angle = inertia_speed_ * static_cast<f64>(dt);
        rotateTarget(inertia_axis_, angle);
        inertia_speed_ *= std::exp(-kInertiaDecay * static_cast<f64>(dt));
        if (inertia_speed_ <= kInertiaMinSpd) {
          inertia_active_ = false;
        }
      }
    }

    updateRTCOrigin();
  }

  // ============================================================================
  // Matrices
  // ============================================================================

  // getViewMatrix() is inherited from CameraBase; the body in the base is
  // `glm::lookAt(getEyeWorld(), getTargetWorld(), getUpWorld())` — identical
  // to the previous implementation since the three overrides below return
  // exactly the values that used to be computed inline here.

  glm::dvec3 GlobeCamera::getEyeWorld() const {
    return eye_ecef_;
  }

  namespace {
    // FPS view basis: takes the eye-frame "look at orbit target" direction
    // and rotates it by view_yaw (around local up) and view_pitch (around
    // resulting right axis). Degenerate case (eye colinear with target,
    // i.e. orbit_pitch = ±90°) falls back to heading_ for a stable right
    // axis. Returns forward + orthogonal up; caller composes lookAt.
    struct ViewBasis {
      glm::dvec3 fwd, up;
    };
  } // namespace

  GlobeCamera::ViewBasis_ GlobeCamera::computeViewBasis_() const {
    glm::dvec3 eye = eye_ecef_;
    glm::dvec3 local_up = view_up_override_enabled_ ? view_up_override_ecef_ : glm::normalize(eye);

    // Heading direction in the eye's local ENU frame — used both as the
    // forward fallback (eye==target) and as the STABLE right-axis source at the
    // top-down degeneracy.
    coordinate::Geodetic eye_geo = coordinate::ecefToGeodetic(eye);
    f64 sl = std::sin(eye_geo.lat), cl = std::cos(eye_geo.lat);
    f64 so = std::sin(eye_geo.lon), co = std::cos(eye_geo.lon);
    glm::dvec3 east(-so, co, 0.0);
    glm::dvec3 north(-sl * co, -sl * so, cl);
    glm::dvec3 horizontal_fwd = north * std::cos(heading_) + east * std::sin(heading_);

    // Base forward: from eye to orbit target (the rotation-zero look direction).
    glm::dvec3 orbit_target_ecef = coordinate::geodeticToECEF(target_);
    glm::dvec3 raw_fwd = orbit_target_ecef - eye;
    glm::dvec3 base_fwd = (glm::length(raw_fwd) > 1e-6) ? glm::normalize(raw_fwd) : horizontal_fwd;

    // Right axis = forward × up (standard lookAt) so `up = cross(right, fwd)`
    // always points skyward — deriving right purely from heading (the old code)
    // made `up` flip downward at shallow pitch (upside-down view). Near the
    // top-down degeneracy (fwd ∥ local_up) that cross goes to zero, so fall back
    // to the heading-derived axis which stays stable straight down.
    glm::dvec3 right_axis = glm::cross(base_fwd, local_up);
    if (glm::length(right_axis) < 1.0e-3) {
      right_axis = glm::cross(horizontal_fwd, local_up);
    }
    right_axis = glm::normalize(right_axis);

    // Yaw rotates fwd + right around local_up.
    if (view_yaw_rad_ != 0.0) {
      glm::dmat4 yaw_rot = glm::rotate(glm::dmat4(1.0), view_yaw_rad_, local_up);
      base_fwd = glm::dvec3(yaw_rot * glm::dvec4(base_fwd, 0.0));
      right_axis = glm::dvec3(yaw_rot * glm::dvec4(right_axis, 0.0));
    }

    // Pitch tilts fwd around (post-yaw) right_axis. Up is then
    // cross(right, fwd), guaranteed orthogonal to fwd by construction.
    if (view_pitch_rad_ != 0.0) {
      glm::dmat4 pitch_rot = glm::rotate(glm::dmat4(1.0), view_pitch_rad_, right_axis);
      base_fwd = glm::dvec3(pitch_rot * glm::dvec4(base_fwd, 0.0));
    }

    glm::dvec3 up = glm::normalize(glm::cross(right_axis, base_fwd));
    return {base_fwd, up};
  }

  glm::dvec3 GlobeCamera::getTargetWorld() const {
    // Always route through computeViewBasis_(). Returning eye + fwd*1e7
    // gives a target far along the look direction, which is what lookAt
    // matrices actually need — the magnitude is irrelevant, only the
    // direction. Doing this even at extras=0 keeps the transition into
    // right-drag rotation continuous: a single unified code path means
    // no abrupt frame-0 → frame-1 jump in either fwd or up.
    return eye_ecef_ + computeViewBasis_().fwd * 1.0e7;
  }

  glm::dvec3 GlobeCamera::getUpWorld() const {
    // Same reasoning as getTargetWorld: one path, no discontinuity. At
    // extras=0 this returns the heading-derived horizontal "up" rotated
    // to be orthogonal to (eye→target), which is within a few degrees of
    // the old legacy headingUp() and visually stable.
    return computeViewBasis_().up;
  }

  glm::mat4 GlobeCamera::getProjectionMatrix(f32 aspect_ratio) const {
    // Near/far follow physical geometry so the float32 depth buffer keeps
    // usable precision at every altitude.
    //
    // Far plane = tangent-to-horizon distance × 2 (covers atmosphere shell and
    // the limb; far side of the globe is occluded by the sphere itself so we
    // never need to see past the horizon).
    // Near plane = 5% of altitude, with floor of 0.5 m. Avoids slicing
    // close terrain while still keeping far/near ratio under ~10 000.
    f32 fov_y = static_cast<f32>(kFovYDeg);
    f32 near = 0.5f;
    f32 far = 1.0e7f;
    if (projection_override_enabled_) {
      fov_y = static_cast<f32>(projection_vertical_fov_deg_);
      near = static_cast<f32>(projection_near_m_);
      far = static_cast<f32>(projection_far_m_);
    } else {
      constexpr f64 R = 6378137.0; // WGS84 equatorial radius
      f64 alt = std::max(getAltitude(), 1.0);
      f64 horizon = std::sqrt(alt * (alt + 2.0 * R));
      near = static_cast<f32>(std::max(alt * 0.05, 0.5));
      far = static_cast<f32>(horizon * 2.0 + 200000.0); // +200 km atmosphere buffer
    }
    glm::mat4 p = glm::perspective(glm::radians(fov_y), aspect_ratio, near, far);
    // Vulkan NDC Y points down; without this the globe (and every consumer
    // sharing this projection — atmosphere, labels, vector layer, city overlay)
    // renders upside-down. Flipping rasterized triangle winding is the cull
    // pipelines' responsibility (see frontCounterClockwise=true in
    // GlobeRenderer / AtmosphereRenderer).
    p[1][1] *= -1.0f;
    return p;
  }

  glm::dvec3 GlobeCamera::getPositionECEF() const {
    return eye_ecef_;
  }

  GlobeCamera::OrbitPose GlobeCamera::captureOrbitPose() const {
    // Step 1: current forward direction. Uses the FPS basis when the user
    // has right-dragged; falls back to (target_ - eye) when they haven't.
    glm::dvec3 fwd;
    if (view_yaw_rad_ == 0.0 && view_pitch_rad_ == 0.0) {
      glm::dvec3 to_target = coordinate::geodeticToECEF(target_) - eye_ecef_;
      if (glm::length(to_target) > 1e-6) {
        fwd = glm::normalize(to_target);
      } else {
        // Eye coincides with target — pick heading-derived horizontal
        coordinate::Geodetic eye_geo = coordinate::ecefToGeodetic(eye_ecef_);
        f64 sl = std::sin(eye_geo.lat), cl = std::cos(eye_geo.lat);
        f64 so = std::sin(eye_geo.lon), co = std::cos(eye_geo.lon);
        glm::dvec3 east(-so, co, 0.0);
        glm::dvec3 north(-sl * co, -sl * so, cl);
        fwd = north * std::cos(heading_) + east * std::sin(heading_);
      }
    } else {
      fwd = computeViewBasis_().fwd;
    }

    // Step 2: where does the look ray hit the WGS84 sphere? That's the
    // surface point the camera is aimed at — becomes the round-trippable
    // "target" for lookAt(). If the ray misses (looking at sky) we project
    // a point distance_ ahead onto the sphere so we still produce a valid
    // pose.
    constexpr f64 kR = 6378137.0;
    f64 t_hit = raySphere(eye_ecef_, fwd, kR);
    glm::dvec3 target_ecef;
    if (t_hit > 0.0) {
      target_ecef = eye_ecef_ + fwd * t_hit;
    } else {
      target_ecef = glm::normalize(eye_ecef_ + fwd * distance_) * kR;
    }

    // Step 3: derive orbit (heading, pitch, distance) from the eye-target
    // relationship in target's local tangent frame. Inverse of the math
    // in computeCameraECEF() — by construction lookAt(target, distance,
    // heading, pitch) places the eye back at eye_ecef_ exactly.
    coordinate::Geodetic target_geo = coordinate::ecefToGeodetic(target_ecef);
    f64 sl = std::sin(target_geo.lat), cl = std::cos(target_geo.lat);
    f64 so = std::sin(target_geo.lon), co = std::cos(target_geo.lon);
    glm::dvec3 east(-so, co, 0.0);
    glm::dvec3 north(-sl * co, -sl * so, cl);
    glm::dvec3 up(cl * co, cl * so, sl);

    glm::dvec3 offset = eye_ecef_ - target_ecef;
    f64 oe = glm::dot(offset, east);
    f64 on = glm::dot(offset, north);
    f64 ou = glm::dot(offset, up);
    f64 dist = glm::length(offset);

    OrbitPose pose;
    pose.target = target_geo;
    pose.target.alt = 0.0; // pin to surface
    pose.distance_m = dist;
    pose.heading_deg = glm::degrees(std::atan2(oe, on));
    // computeCameraECEF stores offset_up = -sin(pitch)*distance, so
    // sin(pitch) = -offset_up / distance.
    pose.pitch_deg = (dist > 1e-6) ? glm::degrees(std::asin(std::clamp(-ou / dist, -1.0, 1.0))) : -90.0;
    return pose;
  }

  GlobeCamera::PoseState GlobeCamera::capturePoseState() const {
    PoseState s;
    s.eye_ecef = eye_ecef_;
    s.target = target_;
    s.heading_rad = heading_;
    s.view_yaw_rad = view_yaw_rad_;
    s.view_pitch_rad = view_pitch_rad_;
    s.distance = distance_;
    return s;
  }

  void GlobeCamera::restorePoseState(const PoseState& s) {
    // Verbatim restore — the rendered view (getEyeWorld/getTargetWorld/
    // getUpWorld → computeViewBasis_) is a pure function of exactly these
    // members, so this reproduces the captured view bit-for-bit. No clamping,
    // no FPS reset, no orbit recompute.
    eye_ecef_ = s.eye_ecef;
    target_ = s.target;
    heading_ = s.heading_rad;
    view_yaw_rad_ = s.view_yaw_rad;
    view_pitch_rad_ = s.view_pitch_rad;
    distance_ = s.distance;
    target_distance_ = s.distance;
    flying_ = false;
    inertia_active_ = false;
    inertia_speed_ = 0.0;
    inertia_axis_ = glm::dvec3(0.0);
    updateRTCOrigin();
  }

  coordinate::Geodetic GlobeCamera::getPositionGeodetic() const {
    return coordinate::ecefToGeodetic(eye_ecef_);
  }

  f64 GlobeCamera::getAltitude() const {
    return getPositionGeodetic().alt;
  }

  coordinate::RTCOrigin& GlobeCamera::getRTCOrigin() {
    return rtc_origin_;
  }
  const coordinate::RTCOrigin& GlobeCamera::getRTCOrigin() const {
    return rtc_origin_;
  }

  glm::mat4 GlobeCamera::getRTCViewMatrix() const {
    // Identical math to before — CameraBase::getRTCViewMatrix(origin) calls
    // origin.computeViewMatrix(getEyeWorld(), getTargetWorld(), vec3(getUpWorld())),
    // which expands to rtc_origin_.computeRTCViewMatrix(cam_ecef, target_ecef, up32).
    return CameraBase::getRTCViewMatrix(rtc_origin_);
  }

  // ============================================================================
  // Private helpers
  // ============================================================================

  glm::dvec3 GlobeCamera::headingUp() const {
    // North and east unit vectors at the target point (ECEF, horizontal plane).
    f64 sl = std::sin(target_.lat), cl = std::cos(target_.lat);
    f64 so = std::sin(target_.lon), co = std::cos(target_.lon);
    glm::dvec3 east(-so, co, 0.0);
    glm::dvec3 north(-sl * co, -sl * so, cl);

    // Heading rotates the screen-up direction around the vertical axis.
    // Result is always horizontal → never parallel to the straight-down view
    // direction, so glm::lookAt cannot degenerate regardless of pitch.
    return north * std::cos(heading_) + east * std::sin(heading_);
  }

  glm::dvec3 GlobeCamera::computeCameraECEF() const {
    glm::dvec3 target_ecef = coordinate::geodeticToECEF(target_);

    f64 sl = std::sin(target_.lat), cl = std::cos(target_.lat);
    f64 so = std::sin(target_.lon), co = std::cos(target_.lon);

    glm::dvec3 east(-so, co, 0.0);
    glm::dvec3 north(-sl * co, -sl * so, cl);
    glm::dvec3 up(cl * co, cl * so, sl);

    f64 cp = std::cos(pitch_), sp = std::sin(pitch_);
    f64 sh = std::sin(heading_), ch = std::cos(heading_);

    glm::dvec3 offset_enu(cp * sh * distance_, cp * ch * distance_, -sp * distance_);

    return target_ecef + east * offset_enu.x + north * offset_enu.y + up * offset_enu.z;
  }

  void GlobeCamera::updateRTCOrigin() {
    if (rtc_origin_.shouldRecenter(eye_ecef_)) {
      rtc_origin_.setOriginECEF(eye_ecef_);
    }
  }

} // namespace hongtu::renderer
