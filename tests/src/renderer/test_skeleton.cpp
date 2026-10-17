// Pure unit tests for Skeleton + forward kinematics + skin matrices.
// No Vulkan — bone math is host-side.

#include <gtest/gtest.h>
#include <hongtu/renderer/animation/skeleton.hpp>
#include <hongtu/renderer/animation/animation_clip.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

using hongtu::renderer::animation::AnimationSampler;
using hongtu::renderer::animation::Joint;
using hongtu::renderer::animation::Skeleton;
using hongtu::renderer::animation::SkinPose;

namespace {

constexpr float kEps = 1e-4f;

Skeleton makeTwoBoneSkeleton() {
  // root at origin, child 1m up along +Y, both identity-rotated.
  Skeleton s;
  Joint root;
  root.name = "root";
  root.parent = -1;
  root.local_translation = {0, 0, 0};
  root.inverse_bind = glm::mat4(1.0f);
  s.addJoint(root);
  Joint child;
  child.name = "child";
  child.parent = 0;
  child.local_translation = {0, 1, 0};
  // child world-bind = root_local × child_local = translate(0,1,0)
  // inverse_bind = inverse of that
  child.inverse_bind = glm::inverse(glm::translate(glm::mat4(1.0f), {0, 1, 0}));
  s.addJoint(child);
  return s;
}

}  // namespace

TEST(SkeletonTest, AddJointReturnsIndex) {
  Skeleton s;
  Joint j; j.name = "j0";
  EXPECT_EQ(s.addJoint(j), 0u);
  Joint j1; j1.name = "j1"; j1.parent = 0;
  EXPECT_EQ(s.addJoint(j1), 1u);
  EXPECT_EQ(s.jointCount(), 2u);
}

TEST(SkeletonTest, BindPoseFillsLocalTRS) {
  Skeleton s = makeTwoBoneSkeleton();
  SkinPose pose;
  AnimationSampler::bindPose(s, pose);
  EXPECT_EQ(pose.translations.size(), 2u);
  EXPECT_EQ(pose.translations[1].y, 1.0f);
  EXPECT_NEAR(pose.rotations[0].w, 1.0f, kEps);   // identity quat
  EXPECT_NEAR(pose.scales[0].x, 1.0f, kEps);
}

TEST(SkeletonTest, ForwardKinematicsBindPoseGivesBindWorld) {
  Skeleton s = makeTwoBoneSkeleton();
  SkinPose pose;
  AnimationSampler::bindPose(s, pose);

  std::vector<glm::mat4> world;
  s.forwardKinematics(pose, world);
  ASSERT_EQ(world.size(), 2u);
  // root world = identity translate (0,0,0)
  EXPECT_NEAR(world[0][3].x, 0.0f, kEps);
  EXPECT_NEAR(world[0][3].y, 0.0f, kEps);
  // child world = translate (0,1,0)
  EXPECT_NEAR(world[1][3].x, 0.0f, kEps);
  EXPECT_NEAR(world[1][3].y, 1.0f, kEps);
}

TEST(SkeletonTest, ParentRotationPropagatesToChildPosition) {
  // Rotate root 90° around Z → child(originally at +Y) should end up at -X.
  Skeleton s = makeTwoBoneSkeleton();
  SkinPose pose;
  AnimationSampler::bindPose(s, pose);
  pose.rotations[0] = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));

  std::vector<glm::mat4> world;
  s.forwardKinematics(pose, world);
  EXPECT_NEAR(world[1][3].x, -1.0f, kEps);
  EXPECT_NEAR(world[1][3].y,  0.0f, kEps);
}

TEST(SkeletonTest, SkinMatricesAreIdentityAtBindPose) {
  // skin = world × inverse_bind. At bind pose, world == bind_world,
  // so skin = bind_world × bind_world⁻¹ = identity.
  Skeleton s = makeTwoBoneSkeleton();
  SkinPose pose;
  AnimationSampler::bindPose(s, pose);

  std::vector<glm::mat4> skin;
  s.skinMatrices(pose, skin);
  for (size_t i = 0; i < skin.size(); ++i) {
    EXPECT_NEAR(skin[i][0][0], 1.0f, kEps) << "joint " << i;
    EXPECT_NEAR(skin[i][3][0], 0.0f, kEps);
    EXPECT_NEAR(skin[i][3][1], 0.0f, kEps);
  }
}
