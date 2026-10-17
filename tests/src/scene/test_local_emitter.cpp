// LocalEmitter tests — RTC float32 conversion + EmitterDesc passthrough.

#include <gtest/gtest.h>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/scene/emitter_entity.hpp>

using hongtu::coordinate::PrecisionOrigin;
using hongtu::renderer::EmitterPresets::makeFire;
using hongtu::scene::LocalEmitter;

namespace { constexpr float kEps = 1e-5f; }

TEST(LocalEmitterTest, RTCAtZeroOrigin) {
  PrecisionOrigin o; o.setOrigin({0, 0, 0});
  LocalEmitter e;
  e.setPosition({5.0, -2.0, 3.0});
  glm::vec3 rtc = e.getRTCPosition(o);
  EXPECT_NEAR(rtc.x,  5.0f, kEps);
  EXPECT_NEAR(rtc.y, -2.0f, kEps);
  EXPECT_NEAR(rtc.z,  3.0f, kEps);
}

TEST(LocalEmitterTest, RTCShiftsByOrigin) {
  PrecisionOrigin o; o.setOrigin({1.0e6, 2.0e6, 3.0e6});
  LocalEmitter e;
  e.setPosition({1.0e6 + 7.0, 2.0e6 - 4.0, 3.0e6 + 1.0});
  glm::vec3 rtc = e.getRTCPosition(o);
  EXPECT_NEAR(rtc.x,  7.0f, 0.01f);
  EXPECT_NEAR(rtc.y, -4.0f, 0.01f);
  EXPECT_NEAR(rtc.z,  1.0f, 0.01f);
}

TEST(LocalEmitterTest, EmitterDescPassthrough) {
  LocalEmitter e;
  auto fire = makeFire({0, 0, 0}, 80.0f);
  e.setDesc(fire);
  EXPECT_FLOAT_EQ(e.getDesc().emit_rate, 80.0f);
  // Mutable accessor — caller can tweak any field
  e.getDesc().enabled = false;
  EXPECT_FALSE(e.getDesc().enabled);
}
