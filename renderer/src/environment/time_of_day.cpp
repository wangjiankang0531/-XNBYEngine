#include <hongtu/renderer/environment/time_of_day.hpp>

#include <algorithm>
#include <cmath>

namespace hongtu::renderer {
namespace {

constexpr float kPi = 3.14159265358979f;

struct Key {
  float     hours;
  glm::vec3 sun_color;
  float     sun_intensity;
  glm::vec3 sky_color;
  float     ibl_intensity;
  float     skylight_bleed;
  float     exposure;
  float     night_factor;
};

// 关键时刻表。首(0h)尾(24h)取值相同 → 跨日连续。
// sun_intensity 与净 IBL 标定见 feedback_pbr_env_calibration:太阳主导。
const Key kKeys[] = {
  // h      sun_color              int   sky_color                ibl   bleed exp  night
  // 关键点设计原则:
  //  1. 强度在地平线附近的关键点取 0,让曲线自然归零,避免几何截断产生跳变。
  //  2. 每 0.1h 步长相邻差值 < 0.35;跨度短的段需降低强度极差。
  //  3. h=5.5 / h=19.0 处被 [0→0] 区间覆盖,FLOAT_EQ(0) 精确成立。
  { 0.0f, {0.30f, 0.45f, 0.80f}, 0.0f, {0.020f, 0.030f, 0.070f}, 0.10f, 0.03f, 1.0f, 1.0f},
  { 5.5f, {0.30f, 0.45f, 0.80f}, 0.0f, {0.020f, 0.030f, 0.070f}, 0.10f, 0.03f, 1.0f, 1.0f},
  { 7.5f, {1.00f, 0.78f, 0.55f}, 3.5f, {0.58f, 0.66f, 0.88f},    0.45f, 0.08f, 1.0f, 0.0f},
  {12.0f, {1.00f, 0.96f, 0.88f}, 6.0f, {0.55f, 0.70f, 0.92f},    0.50f, 0.10f, 1.0f, 0.0f},
  {16.5f, {1.00f, 0.85f, 0.60f}, 3.5f, {0.60f, 0.66f, 0.85f},    0.45f, 0.09f, 1.0f, 0.0f},
  // ibl 0.12:暮光环境光再高会把浅色墙面刷成无方向的平亮(观感像法线坏了)。
  {19.0f, {0.90f, 0.50f, 0.25f}, 0.0f, {0.22f, 0.18f, 0.32f},    0.12f, 0.04f, 1.0f, 0.6f},
  {20.5f, {0.30f, 0.45f, 0.80f}, 0.0f, {0.020f, 0.030f, 0.070f}, 0.10f, 0.03f, 1.0f, 1.0f},
  {24.0f, {0.30f, 0.45f, 0.80f}, 0.0f, {0.020f, 0.030f, 0.070f}, 0.10f, 0.03f, 1.0f, 1.0f},
};
constexpr int kKeyCount = static_cast<int>(sizeof(kKeys) / sizeof(kKeys[0]));

float smooth01(float t) { return t * t * (3.0f - 2.0f * t); }

}  // namespace

TimeOfDayParams evalTimeOfDay(float hours) {
  float h = std::fmod(hours, 24.0f);
  if (h < 0.0f) h += 24.0f;

  TimeOfDayParams p{};

  // ---- 太阳几何:方位 6h=90°(东)→12h=180°(南)→18h=270°(西),
  //      高度角 = sin(π·(h-6)/12) × 65°(夜间为负 → 地平线下)。----
  const float az_deg   = 90.0f + (h - 6.0f) * 15.0f;
  const float elev_deg = std::sin((h - 6.0f) / 12.0f * kPi) * 65.0f;
  const float az = az_deg * kPi / 180.0f;
  const float el = elev_deg * kPi / 180.0f;
  p.sun_dir = glm::normalize(glm::vec3(std::cos(el) * std::sin(az),
                                       std::cos(el) * std::cos(az),
                                       std::sin(el)));

  // ---- 关键点插值 ----
  int i = 0;
  while (i + 1 < kKeyCount - 1 && kKeys[i + 1].hours <= h) ++i;
  const Key& a = kKeys[i];
  const Key& b = kKeys[i + 1];
  const float span = b.hours - a.hours;
  const float t = smooth01(span > 1e-6f ? (h - a.hours) / span : 0.0f);

  p.sun_color      = glm::mix(a.sun_color, b.sun_color, t);
  p.sun_intensity  = glm::mix(a.sun_intensity, b.sun_intensity, t);
  p.sky_color      = glm::mix(a.sky_color, b.sky_color, t);
  p.ibl_intensity  = glm::mix(a.ibl_intensity, b.ibl_intensity, t);
  p.skylight_bleed = glm::mix(a.skylight_bleed, b.skylight_bleed, t);
  p.exposure       = glm::mix(a.exposure, b.exposure, t);
  p.night_factor   = glm::mix(a.night_factor, b.night_factor, t);

  // 风格化曲线:强度完全由关键点插值控制,不做几何截断。
  // h=5.5 / h=19.0 被 [0→0] 关键点区间覆盖,值精确为 0。
  return p;
}


glm::vec3 evalSunDirectionEnu(float hours, float latitude_deg, float day_of_year) {
  float h = std::fmod(hours, 24.0f);
  if (h < 0.0f) h += 24.0f;
  const float deg = kPi / 180.0f;
  // 太阳赤纬(Cooper 公式)与时角(正午为 0,下午为正)。
  const float decl = 23.44f * deg *
                     std::sin(2.0f * kPi * (284.0f + day_of_year) / 365.0f);
  const float hour_angle = (h - 12.0f) * 15.0f * deg;
  const float lat = latitude_deg * deg;
  // 高度角与方位角(方位自北顺时针)。
  const float sin_el = std::sin(lat) * std::sin(decl) +
                       std::cos(lat) * std::cos(decl) * std::cos(hour_angle);
  const float el = std::asin(std::clamp(sin_el, -1.0f, 1.0f));
  const float cos_el = std::cos(el);
  float az;
  if (cos_el < 1e-5f) {
    az = 0.0f;  // 太阳近天顶/天底,方位无意义
  } else {
    const float cos_az = (std::sin(decl) - std::sin(lat) * sin_el) /
                         (std::cos(lat) * cos_el);
    az = std::acos(std::clamp(cos_az, -1.0f, 1.0f));
    if (hour_angle > 0.0f) az = 2.0f * kPi - az;  // 下午太阳在西半天
  }
  return glm::vec3(cos_el * std::sin(az),   // 东
                   cos_el * std::cos(az),   // 北
                   sin_el);                 // 天
}

}  // namespace hongtu::renderer
