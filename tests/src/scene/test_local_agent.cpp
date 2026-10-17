#include <gtest/gtest.h>
#include <hongtu/scene/agent.hpp>

using hongtu::scene::LocalAgent;

namespace { constexpr double kEps = 1e-6; }

TEST(LocalAgentTest, DefaultsAtOrigin) {
  LocalAgent a;
  EXPECT_DOUBLE_EQ(a.getPosition().x, 0.0);
  EXPECT_FLOAT_EQ(a.getMaxSpeedMps(), 1.4f);
  EXPECT_FLOAT_EQ(a.getRadiusMeters(), 0.4f);
}

TEST(LocalAgentTest, SetPositionAndModelMatrix) {
  LocalAgent a;
  a.setPosition({5.0, 10.0, 0.0});
  auto m = a.getModelMatrix();
  EXPECT_DOUBLE_EQ(m[3][0], 5.0);
  EXPECT_DOUBLE_EQ(m[3][1], 10.0);
}

TEST(LocalAgentTest, IntegrateMovesPositionByVelocity) {
  LocalAgent a;
  a.setPosition({0.0, 0.0, 0.0});
  a.integrate({1.0, 2.0, 0.0}, 0.5f);
  EXPECT_NEAR(a.getPosition().x, 0.5, kEps);
  EXPECT_NEAR(a.getPosition().y, 1.0, kEps);
}

TEST(LocalAgentTest, HeadingFromVelocity) {
  LocalAgent a;
  a.setVelocity({0.0, 1.0, 0.0});
  auto h = a.getHeading();
  EXPECT_NEAR(h.y, 1.0, kEps);
}

TEST(LocalAgentTest, ModelMatrixYawsToVelocity) {
  // Velocity +Y → agent +X axis (its forward) should rotate to point along +Y.
  LocalAgent a;
  a.setPosition({0, 0, 0});
  a.setVelocity({0, 1, 0});
  auto m = a.getModelMatrix();
  // Column 0 is agent-local +X(its "forward") in world. We expect ~(0,1,0).
  EXPECT_NEAR(m[0][0], 0.0, 1e-6);
  EXPECT_NEAR(m[0][1], 1.0, 1e-6);
}

TEST(LocalAgentTest, ZeroVelocityHeadingFallback) {
  LocalAgent a;
  EXPECT_NEAR(a.getHeading().x, 1.0, kEps);
}
