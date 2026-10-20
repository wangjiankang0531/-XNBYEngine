// renderer/include/hongtu/renderer/motion/follow_camera.hpp
#pragma once

// 相机跟随数学(lane ⑫,spec docs/superpowers/specs/2026-10-19-motion-smooth-follow-design.md §3 裁定 8–10 / §4.3)。
//
// 输入每帧的目标(场景系位置 + 目标前向的罗盘方位),输出 SDK 语义的轨道位姿:注视点(场景系)、距离、视线罗盘方位
// (0 = 朝北看、顺时针)、视线俯仰(负 = 俯视)。消费方各自换算到 local 轨道相机(crs::sdkHeadingPitchToRoam)或 geo
// GlobeCamera::lookAt(sdkHeadingToGlobeHeading)。两种模式:
//   - chase:期望方位 = 目标前向 + heading 偏置(0 = 正后方顺着目标朝向看);
//   - fixed:期望方位 = heading(绝对罗盘方位),只平移。
// 注视点与方位按 α = 1 − exp(−dt/smooth_s) 指数逼近(方位走最短弧);smooth_s = 0 刚性。用户旋转 / 缩放直接改偏移与当前
// 方位(不经阻尼,手感跟手)。纯数学、无引擎依赖、不加锁。
#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

namespace hongtu::renderer::motion {

  enum class FollowMode {
    kChase,
    kFixed
  };

  struct FollowSpec {
    FollowMode mode = FollowMode::kChase;
    double distance_m = 30.0;
    double heading_deg = 0.0; ///< chase:相对目标前向的偏置;fixed:视线罗盘方位
    double pitch_deg = -25.0; ///< 视线俯仰(负 = 俯视)
    double smooth_s = 0.35;   ///< 阻尼时间常数(秒),0 = 刚性
    double height_m = 0.0;    ///< 注视点在目标原点上方的高度
  };

  struct FollowLimits {
    double min_pitch_deg = -89.0;
    double max_pitch_deg = -5.0; ///< geo GlobeCamera 俯仰护栏 −5°,两端一致
    double min_distance_m = 2.0;
    double max_distance_m = 20000.0;
    double max_smooth_s = 5.0;
    double deg_per_px = 0.28647889756541161; ///< 0.005 rad / 像素(同 local 轨道相机 onRotate)
    double zoom_base = 0.75;                 ///< 每「格」(浏览器 deltaY 100)距离 ×0.75(同 local 轨道相机 onZoom)
  };

  /// 目标:场景系位置 + 前向罗盘方位(没有前向时 has_forward = false,chase 保持上一次的前向)。
  struct FollowTarget {
    glm::dvec3 pos{0.0};
    double forward_compass_deg = 0.0;
    bool has_forward = false;
  };

  /// SDK 语义的轨道位姿。
  struct FollowPose {
    glm::dvec3 target{0.0};
    double distance_m = 30.0;
    double heading_deg = 0.0; ///< 视线罗盘方位 [0, 360)
    double pitch_deg = -25.0;
  };

  inline double wrap360(double deg) {
    double r = std::fmod(deg, 360.0);
    if (r < 0.0)
      r += 360.0;
    return r;
  }

  inline FollowSpec clampFollowSpec(FollowSpec s, const FollowLimits& L = FollowLimits{}) {
    s.distance_m = std::clamp(s.distance_m, L.min_distance_m, L.max_distance_m);
    s.pitch_deg = std::clamp(s.pitch_deg, L.min_pitch_deg, L.max_pitch_deg);
    s.smooth_s = std::clamp(s.smooth_s, 0.0, L.max_smooth_s);
    return s;
  }

  /// local 场景系(x = 东、y = 北、z = 上):视线方向 = (cos p · sin h, cos p · cos h, sin p),眼 = 注视点 − 方向 · 距离。
  inline glm::dvec3 followViewDirLocal(double heading_deg, double pitch_deg) {
    const double h = glm::radians(heading_deg), p = glm::radians(pitch_deg);
    return {std::cos(p) * std::sin(h), std::cos(p) * std::cos(h), std::sin(p)};
  }
  inline glm::dvec3 followEyeLocal(const FollowPose& pose) {
    return pose.target - followViewDirLocal(pose.heading_deg, pose.pitch_deg) * pose.distance_m;
  }

  /// local 场景系 yaw(度,自 +X=东 逆时针)→ 罗盘方位(0 = 北、顺时针)。
  inline double localYawToCompassDeg(double yaw_deg) {
    return wrap360(90.0 - yaw_deg);
  }

  class FollowCamera {
  public:
    /// 开始跟随:首帧直接到位(不阻尼)。
    void start(const FollowSpec& spec, const FollowTarget& t, const FollowLimits& lim = FollowLimits{}) {
      lim_ = lim;
      spec_ = clampFollowSpec(spec, lim_);
      last_forward_ = t.has_forward ? t.forward_compass_deg : 0.0;
      cam_target_ = t.pos + glm::dvec3(0.0, 0.0, spec_.height_m);
      heading_ = wrap360(desiredHeading());
      started_ = true;
    }
    bool started() const {
      return started_;
    }
    const FollowSpec& spec() const {
      return spec_;
    }

    /// 用户旋转(像素):方位 += dx·k、俯仰 −= dy·k(夹取);偏移与当前方位同时改(不经阻尼)。
    void orbit(double dx_px, double dy_px) {
      const double dh = dx_px * lim_.deg_per_px;
      spec_.heading_deg = wrap360(spec_.heading_deg + dh);
      heading_ = wrap360(heading_ + dh);
      spec_.pitch_deg = std::clamp(spec_.pitch_deg - dy_px * lim_.deg_per_px, lim_.min_pitch_deg, lim_.max_pitch_deg);
    }
    /// 用户缩放(浏览器 wheel deltaY,一格约 ±100;正 = 拉远):距离 × zoom_base^(−d/100)(夹 ±3 格 / 次)。
    void zoom(double wheel_delta_y) {
      const double w = std::clamp(-wheel_delta_y * 0.01, -3.0, 3.0);
      spec_.distance_m =
          std::clamp(spec_.distance_m * std::pow(lim_.zoom_base, w), lim_.min_distance_m, lim_.max_distance_m);
    }

    /// 推进一帧。
    FollowPose step(const FollowTarget& t, double dt) {
      if (t.has_forward)
        last_forward_ = t.forward_compass_deg;
      const glm::dvec3 want_target = t.pos + glm::dvec3(0.0, 0.0, spec_.height_m);
      const double a = alpha(dt);
      cam_target_ += (want_target - cam_target_) * a;
      const double want_h = desiredHeading();
      heading_ = wrap360(heading_ + std::remainder(want_h - heading_, 360.0) * a);
      return pose();
    }
    FollowPose pose() const {
      return FollowPose{cam_target_, spec_.distance_m, heading_, spec_.pitch_deg};
    }

  private:
    double desiredHeading() const {
      return spec_.mode == FollowMode::kChase ? last_forward_ + spec_.heading_deg : spec_.heading_deg;
    }
    double alpha(double dt) const {
      if (!(spec_.smooth_s > 1e-9))
        return 1.0;
      if (!(dt > 0.0))
        return 0.0;
      return 1.0 - std::exp(-dt / spec_.smooth_s);
    }
    FollowSpec spec_{};
    FollowLimits lim_{};
    glm::dvec3 cam_target_{0.0};
    double heading_ = 0.0;
    double last_forward_ = 0.0;
    bool started_ = false;
  };

} // namespace hongtu::renderer::motion
