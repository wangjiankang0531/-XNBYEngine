#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/renderer/instancing/instance_buffer.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace hongtu::renderer {

/// Occlusion culling configuration.
struct OcclusionCullingConfig {
  u32 hi_z_width = 512;         ///< Hi-Z buffer width
  u32 hi_z_height = 256;        ///< Hi-Z buffer height
  u32 hi_z_mip_count = 9;       ///< Number of Hi-Z mip levels
  f32 occlusion_threshold = 0.0f; ///< Depth threshold for occlusion
  bool freeze_culling = false;   ///< Freeze culling for debugging
};

/// Per-object data for culling.
struct CullObject {
  AABB bounds;
  u32 object_id;
  bool was_visible = true;       ///< Visibility from last frame (for temporal coherence)
};

/// Result of a culling pass.
struct CullingResult {
  std::vector<u32> visible_objects;  ///< Indices of visible objects
  u32 total_objects = 0;
  u32 culled_by_frustum = 0;
  u32 culled_by_occlusion = 0;
  f32 cull_efficiency = 0.0f;       ///< Percentage culled
};

/// Hi-Z depth pyramid for CPU-side occlusion queries.
class HiZDepthBuffer {
 public:
  /// Build the Hi-Z pyramid from a depth buffer.
  void build(const float* depth_buffer, u32 width, u32 height, u32 mip_count);

  /// Test if a screen-space rectangle is occluded.
  /// @param min_xy  Min screen coords (pixels)
  /// @param max_xy  Max screen coords (pixels)
  /// @param min_depth  Minimum depth of the tested object
  bool isOccluded(const glm::vec2& min_xy, const glm::vec2& max_xy, float min_depth) const;

  /// Get mip level data.
  const std::vector<float>& getMipData(u32 level) const;

  /// Get mip dimensions.
  void getMipSize(u32 level, u32& w, u32& h) const;

  /// Get mip count.
  u32 getMipCount() const { return static_cast<u32>(mips_.size()); }

 private:
  struct MipLevel {
    std::vector<float> data;
    u32 width, height;
  };
  std::vector<MipLevel> mips_;
};

/// Combined frustum + occlusion culling system.
class OcclusionCullingSystem {
 public:
  explicit OcclusionCullingSystem(const OcclusionCullingConfig& config = {});

  /// Perform frustum culling only (no depth buffer needed).
  CullingResult frustumCull(const std::vector<CullObject>& objects,
                            const Frustum& frustum) const;

  /// Perform full frustum + Hi-Z occlusion culling.
  /// @param objects       Objects to cull.
  /// @param frustum       View frustum.
  /// @param view_proj     View-projection matrix (for screen-space projection).
  /// @param depth_buffer  Previous frame's depth buffer.
  /// @param depth_w       Depth buffer width.
  /// @param depth_h       Depth buffer height.
  CullingResult occlusionCull(const std::vector<CullObject>& objects,
                              const Frustum& frustum,
                              const glm::mat4& view_proj,
                              const float* depth_buffer,
                              u32 depth_w, u32 depth_h);

  /// Generate indirect draw commands for visible objects.
  std::vector<IndirectDrawCommand> generateDrawCommands(
      const CullingResult& result,
      u32 index_count_per_object,
      u32 first_index = 0) const;

  /// Get config.
  const OcclusionCullingConfig& getConfig() const { return config_; }

  /// Get the Hi-Z buffer (for debugging/visualization).
  const HiZDepthBuffer& getHiZBuffer() const { return hi_z_buffer_; }

 private:
  /// Project AABB to screen-space rectangle.
  bool projectAABB(const AABB& aabb, const glm::mat4& vp,
                   glm::vec2& min_ss, glm::vec2& max_ss, float& min_depth,
                   u32 screen_w, u32 screen_h) const;

  OcclusionCullingConfig config_;
  HiZDepthBuffer hi_z_buffer_;
};

} // namespace hongtu::renderer
