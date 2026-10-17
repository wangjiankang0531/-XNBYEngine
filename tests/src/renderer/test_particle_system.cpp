// Pure unit tests for ParticleSystem CPU update + 4 emitter presets.
// No Vulkan needed — particle math is host-side; render pipeline is a
// separate concern tested via demo screenshot regression.

#include <gtest/gtest.h>
#include <hongtu/renderer/effects/particle_system.hpp>

#include <cmath>

using hongtu::renderer::EmitterDesc;
using hongtu::renderer::EmitterShape;
using hongtu::renderer::ParticleInstance;
using hongtu::renderer::ParticleSystem;
namespace EP = hongtu::renderer::EmitterPresets;

namespace { constexpr float kEps = 1e-4f; }

// =============================================================================
// Capacity + reset
// =============================================================================

TEST(ParticleSystemTest, CapacityAllocatesSoaSlots) {
  ParticleSystem ps(256);
  EXPECT_EQ(ps.getCapacity(), 256u);
  EXPECT_EQ(ps.getAliveCount(), 0u);
  EXPECT_EQ(ps.getPositions().size(),  256u);
  EXPECT_EQ(ps.getVelocities().size(), 256u);
  EXPECT_EQ(ps.getAges().size(),       256u);
  EXPECT_EQ(ps.getLifetimes().size(),  256u);
  EXPECT_EQ(ps.getSizes().size(),      256u);
}

TEST(ParticleSystemTest, ResetClearsAliveCount) {
  ParticleSystem ps(64);
  EmitterDesc d;
  d.shape       = EmitterShape::Point;
  d.emit_rate   = 1000.0f;
  ps.setEmitter(d);
  ps.update(0.1f);
  ASSERT_GT(ps.getAliveCount(), 0u);
  ps.reset();
  EXPECT_EQ(ps.getAliveCount(), 0u);
}

// =============================================================================
// Spawn rate
// =============================================================================

TEST(ParticleSystemTest, SpawnsAtConfiguredRate) {
  // 100 particles/sec × 0.5s = 50 spawned (assuming pool big enough).
  ParticleSystem ps(1024);
  EmitterDesc d;
  d.shape           = EmitterShape::Point;
  d.emit_rate       = 100.0f;
  d.lifetime_seconds = 10.0f;   // long-lived so none die during test
  d.lifetime_jitter  = 0.0f;
  ps.setEmitter(d);
  ps.update(0.5f);
  // Allow ±1 for accumulator rounding.
  uint32_t alive = ps.getAliveCount();
  EXPECT_GE(alive, 49u);
  EXPECT_LE(alive, 51u);
}

TEST(ParticleSystemTest, CapacityCapsAlive) {
  ParticleSystem ps(10);
  EmitterDesc d;
  d.shape       = EmitterShape::Point;
  d.emit_rate   = 100000.0f;
  d.lifetime_seconds = 10.0f;
  ps.setEmitter(d);
  ps.update(1.0f);
  EXPECT_EQ(ps.getAliveCount(), 10u);
}

TEST(ParticleSystemTest, DisabledEmitterDoesNotSpawn) {
  ParticleSystem ps(64);
  EmitterDesc d;
  d.emit_rate = 1000.0f;
  d.enabled   = false;
  ps.setEmitter(d);
  ps.update(1.0f);
  EXPECT_EQ(ps.getAliveCount(), 0u);
}

TEST(ParticleSystemTest, BurstEmitsImmediately) {
  ParticleSystem ps(64);
  EmitterDesc d;
  d.shape         = EmitterShape::Point;
  d.emit_rate     = 0.0f;          // no continuous emission
  d.burst_remaining = 20.0f;       // one-shot burst
  d.lifetime_seconds = 10.0f;
  ps.setEmitter(d);
  ps.update(0.001f);               // even a tiny dt drains the burst
  EXPECT_EQ(ps.getAliveCount(), 20u);
  // Burst counter should be drained.
  EXPECT_FLOAT_EQ(ps.getEmitter().burst_remaining, 0.0f);
}

// =============================================================================
// Aging + cull
// =============================================================================

TEST(ParticleSystemTest, DeadParticlesAreCulled) {
  ParticleSystem ps(64);
  EmitterDesc d;
  d.shape           = EmitterShape::Point;
  d.emit_rate       = 0.0f;
  d.burst_remaining = 5.0f;
  d.lifetime_seconds = 0.1f;
  d.lifetime_jitter  = 0.0f;
  ps.setEmitter(d);
  ps.update(0.01f);                 // burst fires
  ASSERT_EQ(ps.getAliveCount(), 5u);

  // After lifetime, all should be culled.
  for (int i = 0; i < 12; ++i) ps.update(0.01f);
  EXPECT_EQ(ps.getAliveCount(), 0u);
}

// =============================================================================
// Spawn shape
// =============================================================================

TEST(ParticleSystemTest, PointShapeSpawnsAtCenter) {
  ParticleSystem ps(8);
  EmitterDesc d;
  d.shape       = EmitterShape::Point;
  d.center      = {10.0f, 20.0f, 30.0f};
  d.emit_rate   = 0.0f;
  d.burst_remaining = 8.0f;
  d.lifetime_seconds = 100.0f;
  d.initial_velocity = {0.0f, 0.0f, 0.0f};
  d.velocity_jitter  = 0.0f;
  ps.setEmitter(d);
  ps.update(0.001f);
  ASSERT_EQ(ps.getAliveCount(), 8u);
  for (uint32_t i = 0; i < ps.getAliveCount(); ++i) {
    EXPECT_NEAR(ps.getPositions()[i].x, 10.0f, kEps);
    EXPECT_NEAR(ps.getPositions()[i].y, 20.0f, kEps);
    EXPECT_NEAR(ps.getPositions()[i].z, 30.0f, kEps);
  }
}

TEST(ParticleSystemTest, BoxShapeSpawnsInsideBox) {
  ParticleSystem ps(128);
  EmitterDesc d;
  d.shape         = EmitterShape::Box;
  d.center        = {0, 0, 0};
  d.half_extent   = {5.0f, 5.0f, 5.0f};
  d.emit_rate     = 0.0f;
  d.burst_remaining = 128.0f;
  d.lifetime_seconds = 100.0f;
  d.initial_velocity = {0, 0, 0};
  d.velocity_jitter  = 0.0f;
  ps.setEmitter(d);
  ps.update(0.001f);
  ASSERT_EQ(ps.getAliveCount(), 128u);
  for (uint32_t i = 0; i < ps.getAliveCount(); ++i) {
    const auto& p = ps.getPositions()[i];
    EXPECT_LE(std::abs(p.x), 5.0f);
    EXPECT_LE(std::abs(p.y), 5.0f);
    EXPECT_LE(std::abs(p.z), 5.0f);
  }
}

TEST(ParticleSystemTest, SphereShapeSpawnsInsideRadius) {
  ParticleSystem ps(64);
  EmitterDesc d;
  d.shape         = EmitterShape::Sphere;
  d.center        = {0, 0, 0};
  d.sphere_radius = 3.0f;
  d.emit_rate     = 0.0f;
  d.burst_remaining = 64.0f;
  d.lifetime_seconds = 100.0f;
  d.initial_velocity = {0, 0, 0};
  d.velocity_jitter  = 0.0f;
  ps.setEmitter(d);
  ps.update(0.001f);
  ASSERT_EQ(ps.getAliveCount(), 64u);
  for (uint32_t i = 0; i < ps.getAliveCount(); ++i) {
    const auto& p = ps.getPositions()[i];
    float r = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
    EXPECT_LE(r, 3.0f + kEps);
  }
}

TEST(ParticleSystemTest, ConeShapeDirectsVelocityInsideHalfAngle) {
  ParticleSystem ps(128);
  EmitterDesc d;
  d.shape           = EmitterShape::Cone;
  d.center          = {0, 0, 0};
  d.cone_dir        = {0, 0, 1};
  d.cone_half_angle = 0.2f;          // ~11°
  d.emit_rate       = 0.0f;
  d.burst_remaining = 128.0f;
  d.lifetime_seconds = 100.0f;
  d.initial_velocity = {0, 0, 5.0f};
  d.velocity_jitter  = 0.0f;
  ps.setEmitter(d);
  ps.update(0.001f);
  ASSERT_EQ(ps.getAliveCount(), 128u);

  // Every velocity should be within cone_half_angle of +Z.
  const float cos_max = std::cos(d.cone_half_angle) - 1e-4f;
  for (uint32_t i = 0; i < ps.getAliveCount(); ++i) {
    glm::vec3 v = ps.getVelocities()[i];
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    ASSERT_GT(len, 0.0f);
    float cos_to_axis = v.z / len;
    EXPECT_GE(cos_to_axis, cos_max);
  }
}

// =============================================================================
// Advection
// =============================================================================

TEST(ParticleSystemTest, ConstantVelocityAdvectsLinearly) {
  ParticleSystem ps(8);
  EmitterDesc d;
  d.shape           = EmitterShape::Point;
  d.center          = {0, 0, 0};
  d.emit_rate       = 0.0f;
  d.burst_remaining = 1.0f;
  d.lifetime_seconds = 100.0f;
  d.initial_velocity = {1.0f, 0.0f, 0.0f};
  d.velocity_jitter  = 0.0f;
  d.drag             = 0.0f;
  d.acceleration     = {0, 0, 0};
  ps.setEmitter(d);
  ps.update(0.001f);                  // burst
  ASSERT_EQ(ps.getAliveCount(), 1u);

  // After 1.0 second, x ≈ 1.0
  for (int i = 0; i < 100; ++i) ps.update(0.01f);
  EXPECT_NEAR(ps.getPositions()[0].x, 1.0f, 0.05f);
  EXPECT_NEAR(ps.getPositions()[0].y, 0.0f, kEps);
}

TEST(ParticleSystemTest, AccelerationAffectsVelocity) {
  ParticleSystem ps(8);
  EmitterDesc d;
  d.shape           = EmitterShape::Point;
  d.emit_rate       = 0.0f;
  d.burst_remaining = 1.0f;
  d.lifetime_seconds = 100.0f;
  d.initial_velocity = {0, 0, 0};
  d.velocity_jitter  = 0.0f;
  d.drag             = 0.0f;
  d.acceleration     = {0.0f, 0.0f, -10.0f};   // gravity-ish
  ps.setEmitter(d);
  ps.update(0.001f);
  ASSERT_EQ(ps.getAliveCount(), 1u);

  // After 1s: v ≈ (0, 0, -10), z ≈ -5(0.5 * a * t²)
  for (int i = 0; i < 100; ++i) ps.update(0.01f);
  EXPECT_NEAR(ps.getVelocities()[0].z, -10.0f, 0.5f);
  EXPECT_NEAR(ps.getPositions()[0].z, -5.0f, 0.5f);
}

// =============================================================================
// Instance buffer
// =============================================================================

TEST(ParticleSystemTest, BuildInstanceBufferProducesAliveCount) {
  ParticleSystem ps(64);
  EmitterDesc d;
  d.shape           = EmitterShape::Point;
  d.emit_rate       = 0.0f;
  d.burst_remaining = 10.0f;
  d.lifetime_seconds = 100.0f;
  ps.setEmitter(d);
  ps.update(0.001f);

  std::vector<ParticleInstance> insts;
  ps.buildInstanceBuffer(insts);
  EXPECT_EQ(insts.size(), 10u);
  // Each instance carries non-zero size (presets default to ~0.4m).
  for (const auto& i : insts) EXPECT_GT(i.size, 0.0f);
}

TEST(ParticleSystemTest, ColorAlphaFadesByLifetime) {
  ParticleSystem ps(8);
  EmitterDesc d;
  d.shape           = EmitterShape::Point;
  d.emit_rate       = 0.0f;
  d.burst_remaining = 1.0f;
  d.lifetime_seconds = 1.0f;
  d.lifetime_jitter  = 0.0f;
  d.initial_velocity = {0, 0, 0};
  d.velocity_jitter  = 0.0f;
  // Default ramp: alpha 1 → 1 → 0.7 → 0.
  ps.setEmitter(d);
  ps.update(0.001f);
  ASSERT_EQ(ps.getAliveCount(), 1u);

  std::vector<ParticleInstance> early;
  ps.buildInstanceBuffer(early);
  ASSERT_EQ(early.size(), 1u);
  float a_early = early[0].color.a;

  for (int i = 0; i < 90; ++i) ps.update(0.01f);     // age → ~0.9s of 1s lifetime
  std::vector<ParticleInstance> late;
  ps.buildInstanceBuffer(late);
  ASSERT_EQ(late.size(), 1u);
  float a_late = late[0].color.a;

  // Alpha must monotonically decrease toward end of life (ramp.colors[3].a = 0).
  EXPECT_GT(a_early, a_late);
}

// =============================================================================
// EmitterPresets — sanity-check that each preset returns a workable desc.
// =============================================================================

TEST(EmitterPresetsTest, FirePresetHasUpwardBias) {
  auto d = EP::makeFire({0, 0, 0}, 100.0f);
  EXPECT_GT(d.initial_velocity.z, 0.0f);
  EXPECT_GT(d.emit_rate, 0.0f);
  // Color[0] should be bright (white-ish).
  EXPECT_GT(d.color_ramp.colors[0].r, 0.8f);
}

TEST(EmitterPresetsTest, SmokeHasLongLifetimeAndGrayRamp) {
  auto d = EP::makeSmoke({0, 0, 0}, {0.5f, 0, 0}, 40.0f);
  EXPECT_GE(d.lifetime_seconds, 3.0f);
  // Smoke 1st keyframe is gray (r ≈ g ≈ b).
  EXPECT_NEAR(d.color_ramp.colors[0].r, d.color_ramp.colors[0].g, 0.05f);
  EXPECT_NEAR(d.color_ramp.colors[0].g, d.color_ramp.colors[0].b, 0.05f);
}

TEST(EmitterPresetsTest, SparksHasShortLifetimeAndGravity) {
  auto d = EP::makeSparks({0, 0, 0}, 150.0f);
  EXPECT_LE(d.lifetime_seconds, 1.0f);
  EXPECT_LT(d.acceleration.z, -1.0f);   // gravity down
}

TEST(EmitterPresetsTest, JetHasConeShape) {
  auto d = EP::makeJet({0, 0, 0}, {1, 0, 0});
  EXPECT_EQ(d.shape, EmitterShape::Cone);
  EXPECT_NEAR(d.cone_dir.x, 1.0f, kEps);
  EXPECT_NEAR(d.cone_dir.y, 0.0f, kEps);
  EXPECT_NEAR(d.cone_dir.z, 0.0f, kEps);
}

TEST(EmitterPresetsTest, JetVelocityAlignedWithDirection) {
  auto d = EP::makeJet({0, 0, 0}, {1, 0, 0});
  // initial_velocity should be along +X (some scalar × normalized dir).
  glm::vec3 v = d.initial_velocity;
  float len = std::sqrt(glm::dot(v, v));
  ASSERT_GT(len, 0.0f);
  EXPECT_NEAR(v.x / len, 1.0f, kEps);
}

// =============================================================================
// ParticleInstance layout — GPU compute path 复用此 32B layout 作为 render
// buffer,所以 stride/offset 不能跑。这里固定下来。
// =============================================================================

TEST(ParticleInstanceLayoutTest, SizeIs32Bytes) {
  EXPECT_EQ(sizeof(ParticleInstance), 32u);
}

TEST(ParticleInstanceLayoutTest, FieldOffsets) {
  EXPECT_EQ(offsetof(ParticleInstance, world_pos), 0u);
  EXPECT_EQ(offsetof(ParticleInstance, size),      12u);
  EXPECT_EQ(offsetof(ParticleInstance, color),     16u);
}
