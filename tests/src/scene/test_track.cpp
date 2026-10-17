#include <gtest/gtest.h>
#include <hongtu/scene/timeline/track.hpp>

#include <glm/glm.hpp>

using hongtu::scene::Interp;
using hongtu::scene::Track;

TEST(TrackTest, EmptyReturnsDefault) {
  Track<float> t;
  EXPECT_TRUE(t.empty());
  EXPECT_EQ(t.duration(), 0.0f);
  EXPECT_FLOAT_EQ(t.sample(0.0f), 0.0f);
}

TEST(TrackTest, SingleKeyReturnsThatValue) {
  Track<float> t;
  t.addKey(5.0f, 7.5f);
  EXPECT_FLOAT_EQ(t.sample(0.0f), 7.5f);   // before
  EXPECT_FLOAT_EQ(t.sample(5.0f), 7.5f);   // at
  EXPECT_FLOAT_EQ(t.sample(99.0f), 7.5f);  // after
}

TEST(TrackTest, LinearLerpsBetweenKeys) {
  Track<float> t;
  t.addKey(0.0f, 0.0f, Interp::Linear);
  t.addKey(10.0f, 100.0f, Interp::Linear);
  EXPECT_FLOAT_EQ(t.sample(0.0f),  0.0f);
  EXPECT_FLOAT_EQ(t.sample(5.0f),  50.0f);
  EXPECT_FLOAT_EQ(t.sample(10.0f), 100.0f);
}

TEST(TrackTest, StepHoldsPreviousUntilNext) {
  Track<float> t;
  t.addKey(0.0f, 1.0f, Interp::Step);
  t.addKey(5.0f, 2.0f, Interp::Step);
  EXPECT_FLOAT_EQ(t.sample(2.5f), 1.0f);   // step holds 1.0 until 5.0
  EXPECT_FLOAT_EQ(t.sample(4.999f), 1.0f);
  EXPECT_FLOAT_EQ(t.sample(5.0f), 2.0f);
}

TEST(TrackTest, EaseInOutNonLinearMid) {
  Track<float> t;
  t.addKey(0.0f, 0.0f, Interp::EaseInOut);
  t.addKey(10.0f, 10.0f, Interp::EaseInOut);
  // At midpoint, EaseInOut == Linear (smoothstep(0.5)=0.5)
  EXPECT_NEAR(t.sample(5.0f), 5.0f, 1e-3f);
  // At 1/4 way, linear=2.5, smoothstep < 2.5
  EXPECT_LT(t.sample(2.5f), 2.5f);
}

TEST(TrackTest, ClampsToFirstAndLast) {
  Track<float> t;
  t.addKey(2.0f, 5.0f, Interp::Linear);
  t.addKey(4.0f, 9.0f, Interp::Linear);
  EXPECT_FLOAT_EQ(t.sample(0.0f),  5.0f);
  EXPECT_FLOAT_EQ(t.sample(10.0f), 9.0f);
}

TEST(TrackTest, OutOfOrderInsertsKeepSortedOrder) {
  Track<float> t;
  t.addKey(5.0f, 50.0f, Interp::Linear);
  t.addKey(0.0f, 0.0f,  Interp::Linear);
  t.addKey(10.0f, 100.0f, Interp::Linear);
  EXPECT_FLOAT_EQ(t.sample(2.5f), 25.0f);   // bisection sees sorted [0,5,10]
}

TEST(TrackTest, BoolTrackAlwaysSteps) {
  Track<bool> t;
  t.addKey(0.0f, false, Interp::Linear);    // Linear ignored for bool
  t.addKey(5.0f, true,  Interp::Linear);
  EXPECT_FALSE(t.sample(2.5f));
  EXPECT_TRUE(t.sample(5.0f));
}

TEST(TrackTest, Vec3LinearLerp) {
  Track<glm::vec3> t;
  t.addKey(0.0f, glm::vec3(0,0,0), Interp::Linear);
  t.addKey(10.0f, glm::vec3(10,20,30), Interp::Linear);
  auto v = t.sample(5.0f);
  EXPECT_NEAR(v.x, 5.0f, 1e-4f);
  EXPECT_NEAR(v.y, 10.0f, 1e-4f);
  EXPECT_NEAR(v.z, 15.0f, 1e-4f);
}
