// Loads the real rigged human (CesiumMan.glb) from disk and validates the
// GltfLoader skinned path end-to-end:
//   * skin + animation extracted
//   * mesh JOINTS_0 indices remapped into the Skeleton's topological order
//     (the remapSkinnedMeshJoints fix) — all components < jointCount
//   * forward kinematics + skin matrices are finite
//   * posed vertices stay finite + bounded across the walk clip (a broken
//     remap flings vertices to garbage coordinates — this catches it)
//
// CesiumMan is the canonical Khronos rigged-human sample; it drives the
// editor's third-person roam avatar. Skips gracefully if the asset is absent.

#include <gtest/gtest.h>

#include <hongtu/asset/GltfLoader.h>
#include <hongtu/renderer/animation/skeleton.hpp>
#include <hongtu/renderer/animation/animation_clip.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

using hongtu::asset::GltfLoader;
namespace anim = hongtu::renderer::animation;

namespace {

std::string cesiumManPath() {
  return std::string(HT_ENGINE_SOURCE_DIR) + "/assets/characters/CesiumMan.glb";
}

bool finite(const glm::mat4& m) {
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      if (!std::isfinite(m[c][r])) return false;
  return true;
}

}  // namespace

TEST(GltfSkinnedCharacter, LoadsRiggedHuman) {
  if (!std::filesystem::exists(cesiumManPath())) {
    GTEST_SKIP() << "CesiumMan.glb not present (optional asset)";
  }
  GltfLoader loader;
  auto model = loader.load(cesiumManPath());
  ASSERT_TRUE(model.has_value());
  ASSERT_FALSE(model->skins.empty());
  ASSERT_FALSE(model->animations.empty());

  const size_t joint_count = model->skins[0].skeleton.jointCount();
  EXPECT_GT(joint_count, 2u);   // a real human has many joints (CesiumMan: 19)

  // The walk clip has a positive duration.
  EXPECT_GT(model->animations[0].duration_seconds, 0.0f);

  // At least one skinned mesh with valid, IN-RANGE joint indices. This is the
  // core assertion for the remapSkinnedMeshJoints fix: every JOINTS_0 component
  // must index a real joint after topological remap.
  bool found_skinned = false;
  for (const auto& md : model->meshes) {
    if (md.skin_id < 0 || md.joint_indices.empty()) continue;
    found_skinned = true;
    ASSERT_EQ(md.joint_indices.size(), md.positions.size());
    for (const glm::uvec4& ji : md.joint_indices) {
      EXPECT_LT(ji.x, joint_count);
      EXPECT_LT(ji.y, joint_count);
      EXPECT_LT(ji.z, joint_count);
      EXPECT_LT(ji.w, joint_count);
    }
  }
  EXPECT_TRUE(found_skinned);
}

TEST(GltfSkinnedCharacter, PosedVerticesFiniteAndBounded) {
  if (!std::filesystem::exists(cesiumManPath())) {
    GTEST_SKIP() << "CesiumMan.glb not present (optional asset)";
  }
  GltfLoader loader;
  auto model = loader.load(cesiumManPath());
  ASSERT_TRUE(model.has_value());
  ASSERT_FALSE(model->skins.empty());
  ASSERT_FALSE(model->animations.empty());

  const anim::Skeleton& skel = model->skins[0].skeleton;
  const anim::AnimationClip& clip = model->animations[0];

  // Find the first skinned mesh.
  const hongtu::asset::MeshData* mesh = nullptr;
  for (const auto& md : model->meshes) {
    if (md.skin_id >= 0 && !md.joint_indices.empty()) { mesh = &md; break; }
  }
  ASSERT_NE(mesh, nullptr);

  // Bind-pose reconstruction: at the bind pose, skin matrices must reduce to
  // (at most) the armature rotation, so the skinned figure has the SAME
  // dimensions as the raw mesh (per-axis, after sorting to ignore the armature
  // axis permutation). A broken inverse-bind read scatters joints and blows
  // the bind-pose bounding box into a ~2 m blob in every axis — this catches
  // that regression (the readAccessor<vec4>-on-MAT4 bug).
  {
    glm::vec3 raw_lo(1e9f), raw_hi(-1e9f);
    for (const glm::vec3& p : mesh->positions) { raw_lo = glm::min(raw_lo, p); raw_hi = glm::max(raw_hi, p); }
    anim::SkinPose pose;
    anim::AnimationSampler::bindPose(skel, pose);
    std::vector<glm::mat4> skin;
    skel.skinMatrices(pose, skin);
    glm::vec3 sk_lo(1e9f), sk_hi(-1e9f);
    for (size_t i = 0; i < mesh->positions.size(); ++i) {
      const glm::uvec4& ji = mesh->joint_indices[i];
      const glm::vec4&  w  = mesh->joint_weights[i];
      glm::mat4 m = w.x * skin[ji.x] + w.y * skin[ji.y] + w.z * skin[ji.z] + w.w * skin[ji.w];
      glm::vec3 p = glm::vec3(m * glm::vec4(mesh->positions[i], 1.0f));
      sk_lo = glm::min(sk_lo, p); sk_hi = glm::max(sk_hi, p);
    }
    std::array<float, 3> raw_d = {raw_hi.x - raw_lo.x, raw_hi.y - raw_lo.y, raw_hi.z - raw_lo.z};
    std::array<float, 3> sk_d  = {sk_hi.x - sk_lo.x,  sk_hi.y - sk_lo.y,  sk_hi.z - sk_lo.z};
    std::sort(raw_d.begin(), raw_d.end());
    std::sort(sk_d.begin(),  sk_d.end());
    for (int k = 0; k < 3; ++k)
      EXPECT_NEAR(sk_d[k], raw_d[k], 0.05f) << "bind-pose dim " << k
          << " drifted from raw mesh — inverse-bind / armature reconstruction broken";
  }

  const float dur = clip.duration_seconds;
  for (float t : {0.0f, dur * 0.5f, dur}) {
    anim::SkinPose pose;
    anim::AnimationSampler::bindPose(skel, pose);
    anim::AnimationSampler::sample(skel, clip, t, pose);

    std::vector<glm::mat4> skin;
    skel.skinMatrices(pose, skin);
    ASSERT_EQ(skin.size(), skel.jointCount());
    for (const auto& m : skin) ASSERT_TRUE(finite(m)) << "non-finite skin matrix @t=" << t;

    // Linear-blend skin each vertex; assert finite + bounded. CesiumMan is
    // ~1.5 m tall in model space, so any posed vertex must stay well within a
    // generous 10 m ball — a broken joint remap blows this up immediately.
    for (size_t i = 0; i < mesh->positions.size(); ++i) {
      const glm::uvec4& ji = mesh->joint_indices[i];
      const glm::vec4&  w  = mesh->joint_weights[i];
      glm::mat4 m = w.x * skin[ji.x] + w.y * skin[ji.y]
                  + w.z * skin[ji.z] + w.w * skin[ji.w];
      glm::vec4 p = m * glm::vec4(mesh->positions[i], 1.0f);
      ASSERT_TRUE(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z))
          << "non-finite posed vertex " << i << " @t=" << t;
      EXPECT_LT(glm::length(glm::vec3(p)), 10.0f)
          << "posed vertex " << i << " out of bounds @t=" << t;
    }
  }
}
