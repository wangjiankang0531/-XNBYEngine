#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace hongtu::renderer {

/// 编辑器侧逻辑点光源(世界或 RTC 米坐标)。
struct PointLight {
  glm::vec3 position;
  float     radius;     // 影响半径(米),超出衰减到 0
  glm::vec3 color;      // 线性 RGB
  float     intensity;  // 峰值强度(HDR)
};

constexpr int kMaxPointLights = 16;

/// set=3 binding=0 的 UBO 布局(std140)。**必须与 shader 的 Lights 块逐字节
/// 一致**:shader 用两条独立数组(pos_radius[16] / color_intensity[16]),
/// 故这里也用两条独立数组,而非"结构体数组"——后者会交错排布
/// [p0,c0,p1,c1,…] 与 shader 的 [p0…p15,c0…c15] 同尺寸但不同序,memcpy 后
/// 1 号灯起全部错位(静默损坏,不崩)。
struct PointLightBlock {
  glm::ivec4 count{0};                          // .x = 有效盏数
  glm::vec4  pos_radius[kMaxPointLights]{};      // [i].xyz 位置, .w 半径
  glm::vec4  color_intensity[kMaxPointLights]{}; // [i].rgb 颜色, .a 强度
};

/// 取离相机最近的 ≤max 盏(过滤 intensity<=0 / radius<=0)。纯函数。
std::vector<PointLight> selectNearestLights(const glm::vec3& camera_pos,
                                            const std::vector<PointLight>& all,
                                            int max);

}  // namespace hongtu::renderer
