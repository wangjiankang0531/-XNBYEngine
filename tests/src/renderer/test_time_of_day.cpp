// 昼夜曲线求值器 —— 纯函数,锁死关键时刻的物理直觉与 24h 连续性。
#include <hongtu/renderer/environment/time_of_day.hpp>

#include <gtest/gtest.h>

#include <cmath>

using hongtu::renderer::evalTimeOfDay;

TEST(TimeOfDay, NoonSunIsHighestAndBright) {
  auto noon = evalTimeOfDay(12.0f);
  EXPECT_GT(noon.sun_dir.z, evalTimeOfDay(9.0f).sun_dir.z);
  EXPECT_GT(noon.sun_dir.z, evalTimeOfDay(15.0f).sun_dir.z);
  EXPECT_GT(noon.sun_dir.z, 0.8f);            // 峰值高度角 ~65°
  EXPECT_GT(noon.sun_intensity, 5.0f);
  EXPECT_FLOAT_EQ(noon.night_factor, 0.0f);
}

TEST(TimeOfDay, MidnightIsDarkWithLightsOn) {
  for (float h : {0.0f, 2.0f, 23.0f}) {
    auto p = evalTimeOfDay(h);
    EXPECT_FLOAT_EQ(p.sun_intensity, 0.0f) << "h=" << h;
    EXPECT_FLOAT_EQ(p.night_factor, 1.0f) << "h=" << h;
    EXPECT_LT(p.sky_color.r, 0.1f) << "h=" << h;
  }
}

TEST(TimeOfDay, MorningHasNoNightLights) {
  EXPECT_FLOAT_EQ(evalTimeOfDay(10.0f).night_factor, 0.0f);
}

TEST(TimeOfDay, SunBelowHorizonHasZeroIntensity) {
  EXPECT_FLOAT_EQ(evalTimeOfDay(5.5f).sun_intensity, 0.0f);
  EXPECT_FLOAT_EQ(evalTimeOfDay(19.0f).sun_intensity, 0.0f);
}

TEST(TimeOfDay, SunDirIsUnitAndSweepsEastToWest) {
  for (float h = 0.0f; h < 24.0f; h += 0.25f) {
    EXPECT_NEAR(glm::length(evalTimeOfDay(h).sun_dir), 1.0f, 1e-4f);
  }
  // 8 点偏东(x>0),16 点偏西(x<0)— 方位 90°→270° 经南。
  EXPECT_GT(evalTimeOfDay(8.0f).sun_dir.x, 0.1f);
  EXPECT_LT(evalTimeOfDay(16.0f).sun_dir.x, -0.1f);
}

TEST(TimeOfDay, ContinuousOver24hIncludingWrap) {
  auto prev = evalTimeOfDay(0.0f);
  for (float h = 0.1f; h <= 24.05f; h += 0.1f) {   // 24.05 验证跨日回绕
    auto cur = evalTimeOfDay(h);
    EXPECT_LT(std::abs(cur.sun_intensity - prev.sun_intensity), 0.35f) << h;
    EXPECT_LT(glm::length(cur.sky_color - prev.sky_color), 0.15f) << h;
    EXPECT_LT(std::abs(cur.night_factor - prev.night_factor), 0.15f) << h;
    prev = cur;
  }
}

TEST(TimeOfDay, NegativeAndOverflowHoursWrap) {
  EXPECT_NEAR(evalTimeOfDay(-2.0f).night_factor,
              evalTimeOfDay(22.0f).night_factor, 1e-4f);
  EXPECT_NEAR(evalTimeOfDay(36.0f).sun_intensity,
              evalTimeOfDay(12.0f).sun_intensity, 1e-3f);
}

// ---- 真实太阳历(geo 模式)----------------------------------------------

using hongtu::renderer::evalSunDirectionEnu;

TEST(SunEphemeris, EquatorEquinoxNoonOverhead) {
  // 春分(N≈80)赤道正午:太阳近天顶。
  auto s = evalSunDirectionEnu(12.0f, 0.0f, 80.0f);
  EXPECT_GT(s.z, 0.95f);
  EXPECT_NEAR(glm::length(s), 1.0f, 1e-4f);
}

TEST(SunEphemeris, NorthernSummerNoonIsSouthOfZenith) {
  // 北纬 40° 夏至正午:太阳在南天(朝向太阳的向量北分量为负),仰角高。
  auto s = evalSunDirectionEnu(12.0f, 40.0f, 172.0f);
  EXPECT_LT(s.y, 0.0f);
  EXPECT_GT(s.z, 0.85f);   // 仰角 ~73°
}

TEST(SunEphemeris, MorningEastEveningWest) {
  auto m = evalSunDirectionEnu(8.0f, 40.0f, 172.0f);
  auto e = evalSunDirectionEnu(16.0f, 40.0f, 172.0f);
  EXPECT_GT(m.x, 0.2f);    // 上午偏东
  EXPECT_LT(e.x, -0.2f);   // 下午偏西
}

TEST(SunEphemeris, MidnightBelowHorizon) {
  EXPECT_LT(evalSunDirectionEnu(0.0f, 40.0f, 172.0f).z, -0.2f);
  EXPECT_LT(evalSunDirectionEnu(23.5f, 0.0f, 80.0f).z, -0.5f);
}

TEST(SunEphemeris, UnitLengthOver24h) {
  for (float h = 0.0f; h < 24.0f; h += 0.5f) {
    EXPECT_NEAR(glm::length(evalSunDirectionEnu(h, 40.0f, 172.0f)), 1.0f, 1e-4f);
  }
}
