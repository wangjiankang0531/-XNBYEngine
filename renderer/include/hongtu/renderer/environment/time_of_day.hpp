#pragma once

#include <glm/glm.hpp>

namespace hongtu::renderer {

/// 一组随时刻变化的光照参数。风格化(非物理)昼夜:太阳走东升西落弧线,
/// 颜色/强度/天空按内置关键点曲线插值。Z-up 世界,sun_dir 指向太阳。
struct TimeOfDayParams {
  glm::vec3 sun_dir;        // 单位向量;夜间在地平线下(z<0)
  glm::vec3 sun_color;      // 线性 RGB
  float     sun_intensity;  // 夜间为 0(由关键点表保证,非几何钳制——别加 if(z<0))
  glm::vec3 sky_color;
  float     ibl_intensity;  // 跟随天空亮度(调用方再叠 ibl_enabled 开关)
  float     skylight_bleed; // 绝对 HDR 地板(归一化语义,见 shading_glsl)
  float     exposure;
  float     night_factor;   // 0 = 白天 … 1 = 深夜;驱动窗灯/路灯
};

/// hours 任意实数,内部 fmod 到 [0,24)。
TimeOfDayParams evalTimeOfDay(float hours);

/// 真实太阳方位(简化太阳历,误差 ~1°,演示级):本地太阳时 hours、
/// 纬度(度)、年积日(默认夏至 172)→ ENU(东,北,天)单位向量,指向太阳。
/// geo 模式用:调用方再用锚点 EnuFrame 转 ECEF。颜色/强度仍走
/// evalTimeOfDay 的曲线(曲线时钟 = 本地太阳时,几何与明暗一致)。
glm::vec3 evalSunDirectionEnu(float hours, float latitude_deg,
                              float day_of_year = 172.0f);

}  // namespace hongtu::renderer
