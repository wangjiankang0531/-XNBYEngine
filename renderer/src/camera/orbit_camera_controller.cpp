#include "hongtu/renderer/camera/orbit_camera_controller.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace hongtu::renderer {

namespace { constexpr float kHalfPi = 1.5707f; }

void OrbitCameraController::setDistance(float d) {
  // Upper bound 100km accommodates large geo / city-scale models (e.g.
  // a 3km tileset needs to pull back to ~10km).
  distance_ = std::clamp(d, 0.1f, 100000.0f);
}

void OrbitCameraController::setYawPitch(float yaw_rad, float pitch_rad) {
  yaw_   = yaw_rad;
  pitch_ = std::clamp(pitch_rad, -kHalfPi + 0.01f, kHalfPi - 0.01f);
}

void OrbitCameraController::onRotate(float dx, float dy) {
  yaw_   -= dx * 0.005f;
  pitch_ -= dy * 0.005f;
  pitch_ = std::clamp(pitch_, -kHalfPi + 0.01f, kHalfPi - 0.01f);
}

void OrbitCameraController::onPan(float dx, float dy) {
  // Horizontal pan along the view-derived ground-plane right axis;
  // vertical pan along WORLD up (0,0,1). Using world up — instead of
  // the view-aligned up — keeps dy mapped to altitude at any pitch, so
  // looking straight down doesn't trap the camera near ground.
  glm::vec3 eye   = getEye();
  glm::vec3 fwd   = glm::normalize(target_ - eye);
  glm::vec3 right = glm::cross(fwd, glm::vec3(0, 0, 1));
  // When looking straight up/down, fwd is parallel to world-up and `right`
  // collapses — fall back to a view-aligned right so horizontal pan still
  // works at the poles.
  if (glm::length(right) < 1e-4f) right = glm::vec3(1, 0, 0);
  right = glm::normalize(right);
  float scale = distance_ * 0.001f;
  target_ -= right             * (dx * scale);
  target_ += glm::vec3(0,0,1) * (dy * scale);
}

void OrbitCameraController::onZoom(float wheel_delta) {
  // 滚轮 = 缩放(眼睛沿视线朝枢轴几何靠近/远离)。这条路径【根治】之前两次
  // 投诉(贴近模型一格穿过去 / 向前滚一下就全蓝):
  //   * 收缩的是「到枢轴的半径」distance_,眼睛只会渐近逼近枢轴、永远不会越过
  //     它冲出场景 —— 任意大的 delta、任意大的 distance 都不可能一格飞穿。
  //   * 几何收缩天然减速:越近绝对步长越小,贴模型自动慢下来。
  // macOS 精密/惯性滚动可能放大单次 delta,钳到 [-3,3] 防绝对暴量(此路径
  // 本就安全,钳只是手感上限)。
  wheel_delta = std::clamp(wheel_delta, -3.0f, 3.0f);
  const float factor = std::pow(0.75f, wheel_delta);   // 进每格 −25% / 退 +33%
  const float want   = distance_ * factor;
  if (want >= 1.0f) {
    distance_ = std::min(want, 100000.0f);
    return;
  }
  // 已贴到枢轴(1 m 下限):继续前滚 → 枢轴沿视线小步前移,受控飞进/穿过
  // 场景(每格 ~2 m,城市尺度也不暴冲)。旧实现到这一步会卡死推不动。
  distance_ = 1.0f;
  if (wheel_delta > 0.0f) {
    glm::vec3 fwd = glm::normalize(target_ - getEye());
    target_ += fwd * (wheel_delta * 2.0f);
  }
}

void OrbitCameraController::resetView() {
  target_   = glm::vec3(0.0f);
  yaw_      = -0.785f;
  pitch_    = -0.436f;
  distance_ = 8.0f;
}

glm::vec3 OrbitCameraController::getEye() const {
  float cp = std::cos(pitch_);
  glm::vec3 dir(cp * std::cos(yaw_), cp * std::sin(yaw_), std::sin(pitch_));
  return target_ - dir * distance_;
}

glm::mat4 OrbitCameraController::getViewMatrix() const {
  return glm::lookAt(getEye(), target_, glm::vec3(0, 0, 1));
}

glm::mat4 OrbitCameraController::getProjMatrix(float aspect, float fov_deg,
                                               float znear, float zfar) const {
  glm::mat4 p = glm::perspective(glm::radians(fov_deg), aspect, znear, zfar);
  p[1][1] *= -1.0f;     // Vulkan Y-flip
  return p;
}

}  // namespace hongtu::renderer
