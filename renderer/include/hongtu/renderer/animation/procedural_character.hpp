#pragma once

#include <hongtu/renderer/animation/animation_clip.hpp>
#include <hongtu/renderer/animation/skeleton.hpp>
#include <hongtu/renderer/pbr/skinned_pbr_pipeline.hpp>

#include <cstdint>
#include <vector>

namespace hongtu::renderer::animation {

/// Bundle of (mesh + skeleton + clip) for a procedurally-generated stand-in
/// character. Used by demos that want to show skinned characters without
/// shipping a real glTF asset (evacuation_demo's 100 agents, etc).
///
/// The character is a 2-bone capsule:
///   - joint 0 "lower" at origin → covers vertices z ∈ [0, 0.85m]
///   - joint 1 "upper" at (0,0,0.85) → covers vertices z ∈ [0.85, 1.7m]
/// Weights blend smoothly across z=0.85 so bending the upper joint doesn't
/// produce a hard kink.
struct CharacterAsset {
  Skeleton                            skeleton;
  AnimationClip                       walking;   // sway clip
  std::vector<SkinnedMeshVertex>      vertices;
  std::vector<uint32_t>               indices;
};

class ProceduralCharacter {
 public:
  /// Build the asset bundle. `height` is total height in meters (default 1.7);
  /// `radius` is capsule radius (default 0.3); `segments` is the horizontal
  /// segment count for the cylinder body (default 8 — minimum for round look).
  static CharacterAsset buildCapsule(float height   = 1.7f,
                                     float radius   = 0.3f,
                                     uint32_t segments = 8,
                                     uint32_t height_rings = 4);
};

}  // namespace hongtu::renderer::animation
