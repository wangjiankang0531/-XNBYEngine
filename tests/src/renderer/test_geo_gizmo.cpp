#include <gtest/gtest.h>

#include <hongtu/coordinate/enu_frame.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/renderer/globe/globe_camera.hpp>
#include <hongtu/renderer/picking/ray_caster.hpp>

#include <glm/glm.hpp>

using namespace hongtu::renderer;
using namespace hongtu::coordinate;

namespace {
constexpr double kW = 1280, kH = 720;

// GlobeCamera 不可拷贝,故 Setup 也不可拷贝 —— 用引用 out 参数原地构造。
struct Setup {
  GlobeCamera cam;
  glm::dmat4  inv_vp{1.0};
  glm::dvec3  origin{0.0};
  EnuFrame    frame;
};

// 在锚点上空架相机俯视,填充拖拽所需的矩阵 + ENU 基。
void makeSetup(Setup& s, double lat_deg, double lon_deg) {
  s.frame  = enuFrameAt(Geodetic::fromDegrees(lat_deg, lon_deg, 0.0));
  s.origin = s.frame.origin_ecef;
  s.cam.setScreenSize(static_cast<float>(kW), static_cast<float>(kH));
  // Oblique view so all three axes (east/north/up) project to well-defined
  // screen directions. A top-down view foreshortens the up axis to a point.
  s.cam.lookAt(Geodetic::fromDegrees(lat_deg, lon_deg, 200.0), 200.0, 0.0, -55.0);
  s.cam.update(0.016f);
  glm::dmat4 view = s.cam.getViewMatrix();
  glm::dmat4 proj(s.cam.getProjectionMatrix(static_cast<float>(kW / kH)));
  proj[1][1] *= -1.0;  // 抵消 Vulkan Y-flip(screenToWorldRay 假设 GL NDC)
  s.inv_vp = glm::inverse(proj * view);
}

// 世界点 → 屏幕像素。必须与 screenToWorldRay 的 NDC→像素约定**严格互逆**:
// screenToWorldRay 用 ndc_y = 1 - 2*sy/h(像素 y 向下,NDC y 向上),故反过来
// sy = (1 - ndc_y) * 0.5 * h。早先用 (ndc_y*0.5+0.5)*h 把竖直方向写反了 ——
// 水平的 east 轴不受影响,但 north/up 投影方向被镜像,拖拽符号就反了。
glm::dvec2 toScreen(const glm::dmat4& inv_vp, const glm::dvec3& wp) {
  glm::dmat4 vp = glm::inverse(inv_vp);
  glm::dvec4 clip = vp * glm::dvec4(wp, 1.0);
  glm::dvec3 ndc = glm::dvec3(clip) / clip.w;
  return {(ndc.x * 0.5 + 0.5) * kW, (1.0 - ndc.y) * 0.5 * kH};
}

// 给定轴,构造拖拽平面法线(axis × view × axis)。
glm::dvec3 planeNormalFor(const Setup& s, const glm::dvec3& axis) {
  glm::dvec3 view_dir = glm::normalize(s.origin - s.cam.getEyeWorld());
  glm::dvec3 n = glm::cross(glm::cross(axis, view_dir), axis);
  if (glm::length(n) < 1e-6) n = glm::cross(axis, glm::dvec3(0, 0, 1));
  return glm::normalize(n);
}
}  // namespace

// GIZMO-AXIS-SIGN: 沿屏幕投影 +axis 方向移动光标 ⇒ 该轴有符号距离增大。
TEST(GeoGizmo, DragAlongAxisIncreasesSignedDistance) {
  struct Setup s;
  makeSetup(s, 33.5, 104.6);
  const glm::dvec3 axes[3] = {s.frame.east, s.frame.north, s.frame.up};
  for (int i = 0; i < 3; ++i) {
    glm::dvec3 axis = axes[i];
    glm::dvec3 n = planeNormalFor(s, axis);
    glm::dvec2 s0 = toScreen(s.inv_vp, s.origin);
    glm::dvec2 s1 = toScreen(s.inv_vp, s.origin + axis * 20.0);
    glm::dvec2 dir = glm::normalize(s1 - s0);
    double t_start =
        projectDragToAxis(s.inv_vp, s0.x, s0.y, kW, kH, s.origin, axis, n);
    glm::dvec2 moved = s0 + dir * 40.0;  // 沿 +axis 屏幕方向移 40px
    double t_now =
        projectDragToAxis(s.inv_vp, moved.x, moved.y, kW, kH, s.origin, axis, n);
    EXPECT_GT(t_now - t_start, 0.0) << "axis " << i << " sign reversed";
  }
}

// GIZMO-ROUNDTRIP + NO-CROSSTALK: 沿轴位移分解回 ENU,只动该轴分量。
TEST(GeoGizmo, DragDecomposesToSingleLocalAxis) {
  struct Setup s;
  makeSetup(s, 33.5, 104.6);
  const glm::dvec3 axes[3] = {s.frame.east, s.frame.north, s.frame.up};
  for (int i = 0; i < 3; ++i) {
    glm::dvec3 axis = axes[i];
    glm::dvec3 n = planeNormalFor(s, axis);
    glm::dvec2 s0 = toScreen(s.inv_vp, s.origin);
    glm::dvec2 s1 = toScreen(s.inv_vp, s.origin + axis * 20.0);
    glm::dvec2 dir = glm::normalize(s1 - s0);
    double t0 = projectDragToAxis(s.inv_vp, s0.x, s0.y, kW, kH, s.origin, axis, n);
    glm::dvec2 moved = s0 + dir * 30.0;
    double t1 =
        projectDragToAxis(s.inv_vp, moved.x, moved.y, kW, kH, s.origin, axis, n);
    glm::dvec3 world_delta = (t1 - t0) * glm::normalize(axis);
    glm::dvec3 local_delta(glm::dot(world_delta, s.frame.east),
                           glm::dot(world_delta, s.frame.north),
                           glm::dot(world_delta, s.frame.up));
    for (int j = 0; j < 3; ++j) {
      if (j == i)
        EXPECT_GT(std::abs(local_delta[j]), 1.0) << "axis " << i;
      else
        EXPECT_LT(std::abs(local_delta[j]), 1e-3) << "crosstalk " << i << "->" << j;
    }
  }
}
