#include <hongtu/renderer/animation/procedural_character.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace hongtu::renderer::animation {

namespace {

constexpr float kPi = 3.14159265358979f;

float smoothstep(float a, float b, float t) {
  float u = std::clamp((t - a) / (b - a), 0.0f, 1.0f);
  return u * u * (3.0f - 2.0f * u);
}

}  // namespace

CharacterAsset ProceduralCharacter::buildCapsule(float height,
                                                  float radius,
                                                  uint32_t segments,
                                                  uint32_t height_rings) {
  CharacterAsset out;

  // ---------------------------------------------------------------------------
  // Skeleton — 2 joints, child at z=0.5 * height (waist).
  // ---------------------------------------------------------------------------
  const float waist_z = height * 0.5f;

  Joint lower;
  lower.name = "lower";
  lower.parent = -1;
  lower.local_translation = {0.0f, 0.0f, 0.0f};
  lower.inverse_bind = glm::mat4(1.0f);
  out.skeleton.addJoint(lower);

  Joint upper;
  upper.name = "upper";
  upper.parent = 0;
  upper.local_translation = {0.0f, 0.0f, waist_z};
  // bind-pose world transform = translate(0, 0, waist_z)
  upper.inverse_bind = glm::inverse(glm::translate(glm::mat4(1.0f), {0, 0, waist_z}));
  out.skeleton.addJoint(upper);

  // ---------------------------------------------------------------------------
  // Walking-sway clip — rotate upper joint around X axis sinusoidally.
  // ---------------------------------------------------------------------------
  out.walking.name = "walk";
  out.walking.duration_seconds = 1.0f;       // 1 Hz cycle

  AnimationChannel<glm::quat> upper_rot;
  upper_rot.joint_index = 1;
  upper_rot.interp = InterpolationKind::Linear;
  // 5 keyframes over 1s: 0, π/2, π, 3π/2, 2π — gentle ±0.25 rad sway
  const float amp = 0.25f;
  for (int k = 0; k <= 4; ++k) {
    float t = k * 0.25f;
    float phase = t * 2.0f * kPi;
    float angle = std::sin(phase) * amp;
    upper_rot.times.push_back(t);
    upper_rot.values.push_back(glm::angleAxis(angle, glm::vec3(1, 0, 0)));
  }
  out.walking.rotation_channels.push_back(std::move(upper_rot));

  // ---------------------------------------------------------------------------
  // Cylinder mesh — rings × segments. Each ring is a horizontal circle at z.
  // Top cap and bottom cap added as 2 fan rings.
  // ---------------------------------------------------------------------------
  const float blend_lo = waist_z - 0.15f;
  const float blend_hi = waist_z + 0.15f;
  // height_rings is the number of HORIZONTAL slices(rings); minimum 2.
  const uint32_t rings = std::max(2u, height_rings);

  // Build rings of vertices.
  for (uint32_t r = 0; r < rings; ++r) {
    float v = static_cast<float>(r) / static_cast<float>(rings - 1);
    float z = v * height;
    float lower_w = 1.0f - smoothstep(blend_lo, blend_hi, z);
    float upper_w = 1.0f - lower_w;
    for (uint32_t s = 0; s < segments; ++s) {
      float a = static_cast<float>(s) / static_cast<float>(segments) * 2.0f * kPi;
      float x = std::cos(a) * radius;
      float y = std::sin(a) * radius;
      SkinnedMeshVertex vtx;
      vtx.position = {x, y, z};
      vtx.normal   = glm::normalize(glm::vec3{x, y, 0.0f});
      vtx.uv       = {static_cast<float>(s) / static_cast<float>(segments), v};
      vtx.tangent  = {0, 0, 0, 1};
      vtx.joint_indices = {0u, 1u, 0u, 0u};
      vtx.joint_weights = {lower_w, upper_w, 0.0f, 0.0f};
      out.vertices.push_back(vtx);
    }
  }

  // Index buffer — 2 triangles per quad between rings r and r+1.
  for (uint32_t r = 0; r + 1 < rings; ++r) {
    for (uint32_t s = 0; s < segments; ++s) {
      uint32_t s1 = (s + 1) % segments;
      uint32_t i00 = r       * segments + s;
      uint32_t i01 = r       * segments + s1;
      uint32_t i10 = (r + 1) * segments + s;
      uint32_t i11 = (r + 1) * segments + s1;
      out.indices.push_back(i00); out.indices.push_back(i10); out.indices.push_back(i11);
      out.indices.push_back(i00); out.indices.push_back(i11); out.indices.push_back(i01);
    }
  }

  // Caps — simple fans. Top:vertices on top ring + a synthetic center vertex.
  uint32_t top_center_idx = static_cast<uint32_t>(out.vertices.size());
  {
    SkinnedMeshVertex v;
    v.position = {0, 0, height};
    v.normal   = {0, 0, 1};
    v.uv       = {0.5f, 0.5f};
    v.tangent  = {1, 0, 0, 1};
    v.joint_indices = {1u, 0u, 0u, 0u};
    v.joint_weights = {1.0f, 0.0f, 0.0f, 0.0f};
    out.vertices.push_back(v);
  }
  uint32_t top_ring_start = (rings - 1) * segments;
  for (uint32_t s = 0; s < segments; ++s) {
    uint32_t s1 = (s + 1) % segments;
    out.indices.push_back(top_center_idx);
    out.indices.push_back(top_ring_start + s1);
    out.indices.push_back(top_ring_start + s);
  }

  // Bottom — vertices on bottom ring + synthetic center.
  uint32_t bottom_center_idx = static_cast<uint32_t>(out.vertices.size());
  {
    SkinnedMeshVertex v;
    v.position = {0, 0, 0};
    v.normal   = {0, 0, -1};
    v.uv       = {0.5f, 0.5f};
    v.tangent  = {1, 0, 0, 1};
    v.joint_indices = {0u, 0u, 0u, 0u};
    v.joint_weights = {1.0f, 0.0f, 0.0f, 0.0f};
    out.vertices.push_back(v);
  }
  for (uint32_t s = 0; s < segments; ++s) {
    uint32_t s1 = (s + 1) % segments;
    out.indices.push_back(bottom_center_idx);
    out.indices.push_back(s);
    out.indices.push_back(s1);
  }

  return out;
}

}  // namespace hongtu::renderer::animation
