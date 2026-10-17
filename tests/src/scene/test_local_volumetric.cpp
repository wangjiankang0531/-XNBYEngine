// LocalVolumetric tests — dvec3 中心 → RTC float32.

#include <gtest/gtest.h>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/scene/volumetric_entity.hpp>

using hongtu::coordinate::PrecisionOrigin;
using hongtu::scene::LocalVolumetric;

namespace { constexpr float kEps = 1e-5f; }

TEST(LocalVolumetricTest, Defaults) {
  LocalVolumetric v;
  EXPECT_NEAR(v.getExtentMeters().x, 5.0f, kEps);
  EXPECT_FLOAT_EQ(v.getDensityScale(), 1.0f);
}

TEST(LocalVolumetricTest, RTCAtZeroOrigin) {
  PrecisionOrigin o; o.setOrigin({0, 0, 0});
  LocalVolumetric v;
  v.setCenter({10.0, -5.0, 2.0});
  glm::vec3 rtc = v.getRTCCenter(o);
  EXPECT_NEAR(rtc.x,  10.0f, kEps);
  EXPECT_NEAR(rtc.y,  -5.0f, kEps);
  EXPECT_NEAR(rtc.z,   2.0f, kEps);
}

TEST(LocalVolumetricTest, RTCShiftsByOrigin) {
  PrecisionOrigin o; o.setOrigin({1.0e6, 2.0e6, 3.0e6});
  LocalVolumetric v;
  v.setCenter({1.0e6 + 3.0, 2.0e6 + 2.0, 3.0e6 - 1.0});
  glm::vec3 rtc = v.getRTCCenter(o);
  EXPECT_NEAR(rtc.x,  3.0f, 0.01f);
  EXPECT_NEAR(rtc.y,  2.0f, 0.01f);
  EXPECT_NEAR(rtc.z, -1.0f, 0.01f);
}

TEST(LocalVolumetricTest, ExtentAndScaleSetters) {
  LocalVolumetric v;
  v.setExtentMeters({10, 20, 5});
  v.setDensityScale(2.5f);
  EXPECT_FLOAT_EQ(v.getExtentMeters().y, 20.0f);
  EXPECT_FLOAT_EQ(v.getDensityScale(), 2.5f);
}
