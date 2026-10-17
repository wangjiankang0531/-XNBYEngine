#include <gtest/gtest.h>
#include <hongtu/scene/crowd/crowd_simulator.hpp>
#include <hongtu/scene/agent.hpp>

using hongtu::scene::LocalAgent;
using hongtu::scene::crowd::CrowdParams;
using hongtu::scene::crowd::CrowdSimulator;
using hongtu::scene::crowd::ObstacleAABB;

TEST(CrowdSimulatorTest, EmptyUpdateDoesNotCrash) {
  CrowdSimulator s;
  s.update(0.016f);
  EXPECT_EQ(s.agentCount(), 0u);
}

TEST(CrowdSimulatorTest, TwoNearbyAgentsRepel) {
  CrowdSimulator s;
  LocalAgent a, b;
  a.setPosition({0.0, 0.0, 0.0});
  b.setPosition({0.3, 0.0, 0.0});   // < default separation_radius_m=1.0
  // both agents want to reach (10, 0, 0) so without repulsion they'd stack
  a.setTarget({10, 0, 0}); b.setTarget({10, 0, 0});
  s.addAgent(&a);
  s.addAgent(&b);

  // Advance 1 second in 60 ticks; verify agents end up at different x positions
  for (int i = 0; i < 60; ++i) s.update(1.0f / 60.0f);

  // The agents must have moved apart in some axis. They should not be at
  // the same position after the goal-seek pulled them along +X.
  double dx = std::abs(a.getPosition().x - b.getPosition().x);
  double dy = std::abs(a.getPosition().y - b.getPosition().y);
  double dz = std::abs(a.getPosition().z - b.getPosition().z);
  EXPECT_GT(dx + dy + dz, 0.05);
}

TEST(CrowdSimulatorTest, AgentReachesGoal) {
  CrowdSimulator s;
  LocalAgent a;
  a.setPosition({0, 0, 0});
  a.setTarget({10, 0, 0});
  s.addAgent(&a);

  // 10m at 1.4m/s should take ~7s; 600 ticks at 60Hz = 10s should be enough.
  for (int i = 0; i < 600; ++i) s.update(1.0f / 60.0f);
  // Within 1 m of goal.
  EXPECT_LT(std::abs(a.getPosition().x - 10.0), 1.0);
}

TEST(CrowdSimulatorTest, ObstacleDeflectsAgent) {
  // Without obstacle: agent reaches goal in ~7s. With obstacle blocking
  // the direct path, agent is slowed (held back outside box surface) and
  // can't reach the goal in the same time.
  CrowdSimulator s;
  LocalAgent a;
  a.setPosition({-5.0, 0.0, 0.0});
  a.setTarget({5.0, 0.0, 0.0});
  s.addAgent(&a);
  s.addObstacle({{-1.0, -2.0, -1.0}, {1.0, 2.0, 1.0}});   // wider box

  // 8s simulation — long enough that without obstacle agent reaches goal.
  for (int i = 0; i < 480; ++i) s.update(1.0f / 60.0f);
  // With obstacle blocking, agent should still be well short of goal
  // (stuck near or inside the obstacle's repulse zone).
  EXPECT_LT(a.getPosition().x, 4.0);
}

TEST(CrowdSimulatorTest, VelocityClampedToMaxSpeed) {
  CrowdSimulator s;
  LocalAgent a;
  a.setPosition({0, 0, 0});
  a.setTarget({100, 0, 0});      // far goal → big steering force
  a.setMaxSpeedMps(2.0f);
  s.addAgent(&a);

  for (int i = 0; i < 30; ++i) s.update(1.0f / 60.0f);
  double speed = glm::length(a.getVelocity());
  EXPECT_LE(speed, 2.0 + 0.01);
}

TEST(CrowdSimulatorTest, ClearAgentsAndObstacles) {
  CrowdSimulator s;
  LocalAgent a, b;
  s.addAgent(&a); s.addAgent(&b);
  s.addObstacle({{-1, -1, -1}, {1, 1, 1}});
  EXPECT_EQ(s.agentCount(), 2u);
  EXPECT_EQ(s.obstacleCount(), 1u);
  s.clearAgents();
  s.clearObstacles();
  EXPECT_EQ(s.agentCount(), 0u);
  EXPECT_EQ(s.obstacleCount(), 0u);
}
