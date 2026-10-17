#pragma once

#include <glm/glm.hpp>

namespace hongtu::renderer {

/// Shared orbit-camera control core, lifted verbatim from the editor's
/// `hongtu::editor::CameraController` Local-mode orbit logic so that the
/// editor viewport AND the cloud render server drive the camera with the
/// exact same mouse-drag feel and sensitivities.
///
/// Orbit state: a `target_` pivot plus spherical `yaw_`/`pitch_` and a
/// `distance_` radius. The camera eye sits on the orbit sphere behind the
/// pivot; world up is +Z.
///
/// Input methods take **pixel deltas** (rotate/pan) or a **wheel delta**
/// (zoom); state is applied immediately (no `update()`).
class OrbitCameraController {
 public:
  void  setTarget(const glm::vec3& t) { target_ = t; }
  void  setDistance(float d);
  void  setYawPitch(float yaw_rad, float pitch_rad);
  float getYaw() const { return yaw_; }
  float getPitch() const { return pitch_; }
  float getDistance() const { return distance_; }

  /// Mouse rotation (drag): dx/dy in pixels.
  void onRotate(float dx_pixels, float dy_pixels);
  /// Mouse pan (drag): dx/dy in pixels.
  void onPan(float dx_pixels, float dy_pixels);
  /// Wheel zoom (positive = zoom in, eye approaches pivot).
  void onZoom(float wheel_delta);

  /// Reset to default view (distance=8, yaw=-45°, pitch=-25°, target=origin).
  void resetView();

  glm::mat4 getViewMatrix() const;
  glm::mat4 getProjMatrix(float aspect, float fov_deg = 45.0f, float znear = 0.1f,
                          float zfar = 200000.0f) const;
  glm::vec3 getEye() const;
  glm::vec3 getTarget() const { return target_; }

 private:
  glm::vec3 target_{0.0f};
  float     yaw_      = -0.785f;  // -45°
  float     pitch_    = -0.436f;  // -25°
  float     distance_ = 8.0f;
};

}  // namespace hongtu::renderer
