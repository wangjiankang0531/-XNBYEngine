// LocalBillboard tests — dvec3 → RTC float32 conversion + setter behaviour.

#include <gtest/gtest.h>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/scene/billboard_entity.hpp>

using hongtu::coordinate::PrecisionOrigin;
using hongtu::scene::LocalBillboard;

namespace { constexpr float kEps = 1e-5f; }

TEST(LocalBillboardTest, Defaults) {
  LocalBillboard b;
  EXPECT_EQ(b.getColor(),      glm::vec4(1, 1, 1, 1));
  EXPECT_EQ(b.getAtlasUV(),    glm::vec4(0, 0, 1, 1));
  EXPECT_EQ(b.getSizeMeters(), glm::vec2(1, 1));
}

TEST(LocalBillboardTest, RTCAtZeroOrigin) {
  PrecisionOrigin origin; origin.setOrigin({0.0, 0.0, 0.0});
  LocalBillboard b;
  b.setPosition({7.0, -3.0, 2.5});
  glm::vec3 rtc = b.getRTCPosition(origin);
  EXPECT_NEAR(rtc.x,  7.0f, kEps);
  EXPECT_NEAR(rtc.y, -3.0f, kEps);
  EXPECT_NEAR(rtc.z,  2.5f, kEps);
}

TEST(LocalBillboardTest, RTCShiftsByOrigin) {
  PrecisionOrigin origin; origin.setOrigin({1.0e6, 2.0e6, 3.0e6});
  LocalBillboard b;
  b.setPosition({1.0e6 + 5.0, 2.0e6 - 4.0, 3.0e6 + 1.0});
  glm::vec3 rtc = b.getRTCPosition(origin);
  EXPECT_NEAR(rtc.x,  5.0f, 0.01f);
  EXPECT_NEAR(rtc.y, -4.0f, 0.01f);
  EXPECT_NEAR(rtc.z,  1.0f, 0.01f);
}

TEST(LocalBillboardTest, SettersPropagate) {
  LocalBillboard b;
  b.setAtlasUV({0.1f, 0.2f, 0.3f, 0.4f});
  b.setSizeMeters({2.0f, 3.0f});
  b.setColor({0.5f, 0.5f, 0.5f, 0.5f});
  EXPECT_FLOAT_EQ(b.getAtlasUV().x, 0.1f);
  EXPECT_FLOAT_EQ(b.getSizeMeters().y, 3.0f);
  EXPECT_FLOAT_EQ(b.getColor().a, 0.5f);
}
