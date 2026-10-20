// renderer/include/hongtu/renderer/motion/geo_motion.hpp
#pragma once

// geo 下的运动插值与方位换算(lane ⑫,spec §3 裁定 5 / 10、§4.2)。
//
// 端点存场景系米(工程锚点 ENU 绕 up 转 heading,与 SDK pos / lonlat 落位同一语义)。插值:两端经 scene_base 转 ECEF,
// 弦线插值后换大地坐标、高度按两端椭球高线性插值,再转回场景系 —— 长段(几十 km)走贴椭球的弧,不穿地;小步与线性差
// 在亚厘米。贴地端点(lonlat 缺 alt):该端的高度 = 插值点经纬处地形高(逐点贴地);两端都贴地 = 插值点贴地,一端贴地 =
// 两种高度按插值比例混合(段首尾连续)。
#include <glm/glm.hpp>
#include <hongtu/coordinate/enu_frame.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/renderer/motion/follow_camera.hpp>
#include <hongtu/renderer/motion/pose_smoother.hpp>

#include <cmath>
#include <functional>

namespace hongtu::renderer::motion {

  /// (lat°, lon°) → 地形椭球高(米)。空 = 0(椭球面)。
  using MotionTerrainFn = std::function<double(double lat_deg, double lon_deg)>;

  inline glm::dvec3 geoLerpScene(const glm::dmat4& scene_base,
                                 const glm::dmat4& scene_base_inv,
                                 const glm::dvec3& a,
                                 const glm::dvec3& b,
                                 double s,
                                 bool a_ground,
                                 bool b_ground,
                                 const MotionTerrainFn& terrain) {
    namespace c = hongtu::coordinate;
    const glm::dvec3 ae = glm::dvec3(scene_base * glm::dvec4(a, 1.0));
    const glm::dvec3 be = glm::dvec3(scene_base * glm::dvec4(b, 1.0));
    const c::Geodetic ga = c::ecefToGeodetic(ae);
    const c::Geodetic gb = c::ecefToGeodetic(be);
    c::Geodetic g = c::ecefToGeodetic(ae + (be - ae) * s);
    double ground = 0.0;
    if (a_ground || b_ground) {
      double la = 0.0, lo = 0.0;
      g.toDegrees(la, lo);
      const double h = terrain ? terrain(la, lo) : 0.0;
      ground = std::isfinite(h) ? h : 0.0;
    }
    const double ha = a_ground ? ground : ga.alt;
    const double hb = b_ground ? ground : gb.alt;
    g.alt = ha + (hb - ha) * s;
    return glm::dvec3(scene_base_inv * glm::dvec4(c::geodeticToECEF(g), 1.0));
  }

  /// 绑定当前场景帧与地形采样的插值钩子(PoseSmoother::step 用)。
  inline MotionLerpFn geoMotionLerp(const glm::dmat4& scene_base,
                                    const glm::dmat4& scene_base_inv,
                                    MotionTerrainFn terrain_fn) {
    return [scene_base, scene_base_inv, terrain = std::move(terrain_fn)](
               const glm::dvec3& a, const glm::dvec3& b, double s, bool ag, bool bg) {
      return geoLerpScene(scene_base, scene_base_inv, a, b, s, ag, bg, terrain);
    };
  }

  /// 场景系 yaw(度,自场景 +X 逆时针)在 pos_scene 处的罗盘方位(0 = 真北、顺时针;工程 heading 自动计入)。
  inline double sceneYawToCompassDeg(const glm::dmat4& scene_base, const glm::dvec3& pos_scene, double yaw_deg) {
    namespace c = hongtu::coordinate;
    const double y = glm::radians(yaw_deg);
    const glm::dvec3 d = glm::dmat3(scene_base) * glm::dvec3(std::cos(y), std::sin(y), 0.0);
    const glm::dvec3 pe = glm::dvec3(scene_base * glm::dvec4(pos_scene, 1.0));
    const c::EnuFrame f = c::enuFrameAt(c::ecefToGeodetic(pe));
    return wrap360(glm::degrees(std::atan2(glm::dot(d, f.east), glm::dot(d, f.north))));
  }

  /// geo:跟随位姿(注视点场景系 + 罗盘方位 / 俯仰 / 距离)→ 眼点(场景系)。方位相对注视点处的真北。
  inline glm::dvec3 followEyeGeo(const FollowPose& pose,
                                 const glm::dmat4& scene_base,
                                 const glm::dmat4& scene_base_inv) {
    namespace c = hongtu::coordinate;
    const glm::dvec3 te = glm::dvec3(scene_base * glm::dvec4(pose.target, 1.0));
    const c::EnuFrame f = c::enuFrameAt(c::ecefToGeodetic(te));
    const glm::dvec3 dl = followViewDirLocal(pose.heading_deg, pose.pitch_deg);
    const glm::dvec3 d = f.east * dl.x + f.north * dl.y + f.up * dl.z;
    return glm::dvec3(scene_base_inv * glm::dvec4(te - d * pose.distance_m, 1.0));
  }

} // namespace hongtu::renderer::motion
