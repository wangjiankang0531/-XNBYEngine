#include <hongtu/renderer/camera/local_camera.hpp>
#include <hongtu/renderer/picking/ray_caster.hpp>

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

namespace hongtu::renderer {

namespace {

constexpr f64 kMinDistance   = 0.01;
constexpr f64 kMaxDistance   = 1.0e9;
constexpr f64 kMinPitch      = -glm::pi<f64>() / 2.0 + 0.01;
constexpr f64 kMaxPitch      =  glm::pi<f64>() / 2.0 - 0.01;
constexpr f64 kDragYawRate   = 0.005;      // radians per pixel
constexpr f64 kDragPitchRate = 0.005;
constexpr f64 kOrbitRotRate  = 0.005;      // orbit drag

}  // namespace

LocalCamera::LocalCamera() {
  // Sensible defaults for a small local scene.
  setNearFar(0.1f, 10000.0f);
  setFovYDegrees(60.0f);
}

void LocalCamera::setScreenSize(f32 width, f32 height) {
  screen_w_ = width;
  screen_h_ = height;
}

void LocalCamera::setYawPitch(f64 yaw_rad, f64 pitch_rad) {
  yaw_   = yaw_rad;
  pitch_ = std::clamp(pitch_rad, kMinPitch, kMaxPitch);
}

glm::dvec3 LocalCamera::forward() const {
  f64 cp = std::cos(pitch_);
  return glm::dvec3(cp * std::cos(yaw_), cp * std::sin(yaw_), std::sin(pitch_));
}

void LocalCamera::moveForward(f64 meters) {
  position_ += forward() * meters;
}

void LocalCamera::moveRight(f64 meters) {
  glm::dvec3 f = forward();
  glm::dvec3 right = glm::normalize(glm::cross(f, up_));
  position_ += right * meters;
}

void LocalCamera::moveUp(f64 meters) {
  position_ += up_ * meters;
}

void LocalCamera::lookAtTarget(const glm::dvec3& target, f64 distance,
                                f64 yaw_rad, f64 pitch_rad) {
  mode_         = Mode::Orbit;
  orbit_target_ = target;
  distance_     = std::clamp(distance, kMinDistance, kMaxDistance);
  yaw_          = yaw_rad;
  pitch_        = std::clamp(pitch_rad, kMinPitch, kMaxPitch);
}

void LocalCamera::onMouseDown(f32 sx, f32 sy) {
  dragging_   = true;
  last_mouse_ = {sx, sy};
}

void LocalCamera::onMouseMove(f32 sx, f32 sy) {
  if (!dragging_) return;
  f32 dx = sx - last_mouse_.x;
  f32 dy = sy - last_mouse_.y;
  last_mouse_ = {sx, sy};

  if (mode_ == Mode::Orbit) {
    yaw_   -= static_cast<f64>(dx) * kOrbitRotRate;
    pitch_ -= static_cast<f64>(dy) * kOrbitRotRate;
    pitch_  = std::clamp(pitch_, kMinPitch, kMaxPitch);
  } else {
    // FreeFly: drag steers the camera's facing direction.
    yaw_   += static_cast<f64>(dx) * kDragYawRate;
    pitch_ -= static_cast<f64>(dy) * kDragPitchRate;
    pitch_  = std::clamp(pitch_, kMinPitch, kMaxPitch);
  }
}

void LocalCamera::onMouseUp() {
  dragging_ = false;
}

void LocalCamera::onRightDrag(f32 dx, f32 dy) {
  yaw_   += static_cast<f64>(dx) * kDragYawRate;
  pitch_ -= static_cast<f64>(dy) * kDragPitchRate;
  pitch_  = std::clamp(pitch_, kMinPitch, kMaxPitch);
}

void LocalCamera::onMouseScroll(f32 delta) {
  if (mode_ == Mode::Orbit) {
    // Each notch shrinks or grows distance by ~10 %.
    f64 factor = std::exp(-0.1 * static_cast<f64>(delta));
    distance_  = std::clamp(distance_ * factor, kMinDistance, kMaxDistance);
  } else {
    // FreeFly: scroll moves forward by a fraction of the current scale.
    moveForward(static_cast<f64>(delta) * 0.5);
  }
}

void LocalCamera::update(f32 /*dt*/) {
  // No smoothing / inertia in v1 — left as a hook for future polish.
}

// -----------------------------------------------------------------------------
// CameraBase overrides
// -----------------------------------------------------------------------------

glm::dvec3 LocalCamera::getEyeWorld() const {
  if (mode_ == Mode::Orbit) {
    glm::dvec3 dir = forward();           // unit vector from camera to target
    return orbit_target_ - dir * distance_;
  }
  return position_;
}

glm::dvec3 LocalCamera::getTargetWorld() const {
  if (mode_ == Mode::Orbit) return orbit_target_;
  return position_ + forward();           // look 1 m ahead in free-fly
}

glm::dvec3 LocalCamera::getUpWorld() const {
  return up_;
}

std::optional<PickHit>
LocalCamera::pickWorld(double sx, double sy,
                       double screen_w, double screen_h) const {
  glm::dmat4 view = getViewMatrix();
  glm::dmat4 proj = getProjectionMatrix(
      static_cast<f32>(screen_w / std::max(1.0, screen_h)));
  WorldRay ray = screenToWorldRay(sx, sy, screen_w, screen_h,
                                   glm::inverse(proj * view));

  // Ground plane z = 0, normal +Z. Local-mode "ground" is the world XY plane.
  auto t = intersectRayPlane(ray, glm::dvec3(0, 0, 0),
                                    glm::dvec3(0, 0, 1));
  if (!t) return std::nullopt;
  PickHit hit;
  hit.t     = *t;
  hit.point = ray.origin + ray.direction * (*t);
  return hit;
}

}  // namespace hongtu::renderer
