// Pure unit tests for AnimationClip channel sampling + bindPose / sample.
// No Vulkan — keyframe math is host-side.

#include <gtest/gtest.h>
#include <hongtu/renderer/animation/animation_clip.hpp>

#include <glm/gtc/quaternion.hpp>

using hongtu::renderer::animation::AnimationChannel;
using hongtu::renderer::animation::AnimationClip;
using hongtu::renderer::animation::AnimationSampler;
using hongtu::renderer::animation::InterpolationKind;
using hongtu::renderer::animation::Joint;
using hongtu::renderer::animation::Skeleton;
using hongtu::renderer::animation::SkinPose;

namespace {
constexpr float kEps = 1e-3f;
}

TEST(AnimationChannelTest, EmptyReturnsDefault) {
  AnimationChannel<glm::vec3> ch;
  auto v = ch.sample(0.5f);
  EXPECT_FLOAT_EQ(v.x, 0.0f);
}

TEST(AnimationChannelTest, StepHoldsPreviousKey) {
  AnimationChannel<glm::vec3> ch;
  ch.times = {0.0f, 1.0f, 2.0f};
  ch.values = {{0,0,0}, {10,0,0}, {20,0,0}};
  ch.interp = InterpolationKind::Step;
  EXPECT_NEAR(ch.sample(0.99f).x, 0.0f, kEps);
  EXPECT_NEAR(ch.sample(1.0f).x, 10.0f, kEps);
  EXPECT_NEAR(ch.sample(1.5f).x, 10.0f, kEps);
}

TEST(AnimationChannelTest, LinearLerpsVec3) {
  AnimationChannel<glm::vec3> ch;
  ch.times = {0.0f, 1.0f};
  ch.values = {{0,0,0}, {10,20,30}};
  ch.interp = InterpolationKind::Linear;
  auto v = ch.sample(0.5f);
  EXPECT_NEAR(v.x, 5.0f,  kEps);
  EXPECT_NEAR(v.y, 10.0f, kEps);
  EXPECT_NEAR(v.z, 15.0f, kEps);
}

TEST(AnimationChannelTest, LinearSlerpsQuat) {
  AnimationChannel<glm::quat> ch;
  ch.times = {0.0f, 1.0f};
  ch.values = {
      glm::angleAxis(0.0f, glm::vec3(0,0,1)),
      glm::angleAxis(glm::radians(90.0f), glm::vec3(0,0,1)),
  };
  ch.interp = InterpolationKind::Linear;
  auto q = ch.sample(0.5f);
  // 45° quaternion around +Z: w = cos(22.5°) ≈ 0.924
  EXPECT_NEAR(q.w, std::cos(glm::radians(22.5f)), kEps);
}

TEST(AnimationChannelTest, ClampsOutOfRange) {
  AnimationChannel<glm::vec3> ch;
  ch.times = {1.0f, 2.0f};
  ch.values = {{5,0,0}, {10,0,0}};
  EXPECT_FLOAT_EQ(ch.sample(0.0f).x,  5.0f);    // before first
  EXPECT_FLOAT_EQ(ch.sample(10.0f).x, 10.0f);   // after last
}

TEST(AnimationChannelTest, CubicSplineSinglyValuedAtKeys) {
  // CubicSpline layout: per key (in_tangent, value, out_tangent).
  AnimationChannel<glm::vec3> ch;
  ch.times = {0.0f, 1.0f};
  ch.values = {
      {0,0,0}, {0,0,0}, {0,0,0},        // key 0:in=0, value=0, out=0
      {0,0,0}, {10,0,0}, {0,0,0},       // key 1:in=0, value=10, out=0
  };
  ch.interp = InterpolationKind::CubicSpline;
  // At key boundaries, sample should return the keyframe value.
  EXPECT_NEAR(ch.sample(0.0f).x,  0.0f, kEps);
  EXPECT_NEAR(ch.sample(1.0f).x, 10.0f, kEps);
  // Midpoint: with zero tangents, Hermite reduces to (3t²-2t³) lerp
  // = smoothstep(0.5) = 0.5, so 5.0.
  EXPECT_NEAR(ch.sample(0.5f).x, 5.0f, kEps);
}

TEST(AnimationSamplerTest, BindPoseFillsSkinPose) {
  Skeleton s;
  Joint j; j.local_translation = {1, 2, 3};
  j.local_rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 1, 0));
  j.local_scale = {2, 2, 2};
  s.addJoint(j);
  SkinPose pose;
  AnimationSampler::bindPose(s, pose);
  EXPECT_NEAR(pose.translations[0].x, 1.0f, kEps);
  EXPECT_NEAR(pose.scales[0].x,       2.0f, kEps);
}

TEST(AnimationSamplerTest, SampleOverridesJointsWithChannels) {
  Skeleton s;
  Joint j0; j0.local_translation = {0, 0, 0}; s.addJoint(j0);
  Joint j1; j1.parent = 0; j1.local_translation = {0, 1, 0}; s.addJoint(j1);

  AnimationClip clip;
  AnimationChannel<glm::vec3> ch;
  ch.joint_index = 1;
  ch.times = {0.0f, 1.0f};
  ch.values = {{0, 1, 0}, {0, 2, 0}};
  clip.translation_channels.push_back(ch);
  clip.duration_seconds = 1.0f;

  SkinPose pose;
  AnimationSampler::bindPose(s, pose);
  AnimationSampler::sample(s, clip, 0.5f, pose);
  EXPECT_NEAR(pose.translations[1].y, 1.5f, kEps);    // mid-way 1→2
  EXPECT_NEAR(pose.translations[0].y, 0.0f, kEps);    // joint 0 not in channel → bind pose preserved
}

TEST(AnimationSamplerTest, SampleQuatChannelNormalizes) {
  Skeleton s;
  Joint j; s.addJoint(j);

  AnimationClip clip;
  AnimationChannel<glm::quat> ch;
  ch.joint_index = 0;
  ch.times = {0.0f, 1.0f};
  ch.values = {
      glm::angleAxis(0.0f, glm::vec3(0,0,1)),
      glm::angleAxis(glm::radians(180.0f), glm::vec3(0,0,1)),
  };
  clip.rotation_channels.push_back(ch);

  SkinPose pose;
  AnimationSampler::bindPose(s, pose);
  AnimationSampler::sample(s, clip, 0.5f, pose);
  // 90° quat is (cos45, 0, 0, sin45), magnitude should be 1.
  glm::quat q = pose.rotations[0];
  EXPECT_NEAR(std::sqrt(q.w*q.w + q.x*q.x + q.y*q.y + q.z*q.z), 1.0f, kEps);
}
