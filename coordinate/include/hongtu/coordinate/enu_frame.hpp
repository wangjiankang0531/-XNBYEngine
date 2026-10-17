#pragma once

#include <hongtu/coordinate/geodetic.hpp>
#include <glm/glm.hpp>

namespace hongtu::coordinate {

/// 锚点处的 ENU(East-North-Up)正交基 + 锚点 ECEF。
/// 把"锚点 Geodetic + 本地米偏移"复合成 ECEF 世界坐标,反之亦然。
/// 所有量在 double(dvec3)精度下计算,避免 6.4e6 量级 ECEF 的 float 精度损失。
struct EnuFrame {
  glm::dvec3 origin_ecef{0.0};  ///< 锚点 ECEF
  glm::dvec3 east{1.0, 0.0, 0.0};
  glm::dvec3 north{0.0, 1.0, 0.0};
  glm::dvec3 up{0.0, 0.0, 1.0};

  /// 本地 ENU 米 → ECEF。  world = origin + east*x + north*y + up*z
  glm::dvec3 localToEcef(const glm::dvec3& local_m) const;
  /// ECEF → 本地 ENU 米(相对锚点)。逆变换,基正交故用点积。
  glm::dvec3 ecefToLocal(const glm::dvec3& ecef) const;
};

/// 构造锚点处的 ENU 基(WGS84)。
EnuFrame enuFrameAt(const Geodetic& anchor);

}  // namespace hongtu::coordinate
