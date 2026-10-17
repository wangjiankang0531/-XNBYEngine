#pragma once

#include <hongtu/core/common.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace hongtu::renderer {

/// Per-instance data for GPU instancing.
struct InstanceData {
  glm::mat4 model_matrix;   ///< Model transform (64 bytes)
  glm::vec4 color;           ///< Per-instance color/tint (16 bytes)
  u32 material_id;           ///< Material index (4 bytes)
  u32 flags;                 ///< Instance flags (visible, selected, etc.) (4 bytes)
  f32 padding[2];            ///< Alignment padding (8 bytes) -> total 96 bytes
};

/// Indirect draw command (matches VkDrawIndexedIndirectCommand).
struct IndirectDrawCommand {
  u32 index_count;
  u32 instance_count;
  u32 first_index;
  i32 vertex_offset;
  u32 first_instance;
};

/// AABB for frustum culling.
struct AABB {
  glm::vec3 min_point;
  glm::vec3 max_point;

  glm::vec3 center() const { return (min_point + max_point) * 0.5f; }
  glm::vec3 extents() const { return (max_point - min_point) * 0.5f; }
};

/// Frustum planes for CPU culling (6 planes: left, right, bottom, top, near, far).
struct Frustum {
  glm::vec4 planes[6]; ///< Each plane as (nx, ny, nz, d) where nx*x + ny*y + nz*z + d >= 0 is inside
};

/// Manages instance data for GPU instancing and indirect draw.
class InstanceBuffer {
 public:
  InstanceBuffer() = default;

  /// Reserve capacity for N instances.
  void reserve(u32 capacity);

  /// Clear all instances.
  void clear();

  /// Add an instance. Returns instance index.
  u32 addInstance(const InstanceData& data);

  /// Update an existing instance.
  void updateInstance(u32 index, const InstanceData& data);

  /// Remove an instance (swap with last).
  void removeInstance(u32 index);

  /// Get raw instance data for GPU upload.
  const InstanceData* getData() const { return instances_.data(); }

  /// Get instance count.
  u32 getCount() const { return static_cast<u32>(instances_.size()); }

  /// Get size in bytes.
  size_t getSizeBytes() const { return instances_.size() * sizeof(InstanceData); }

  /// Get a specific instance.
  const InstanceData& getInstance(u32 index) const { return instances_[index]; }
  InstanceData& getInstance(u32 index) { return instances_[index]; }

  /// Generate indirect draw commands for a mesh.
  /// @param index_count    Number of indices in the mesh.
  /// @param first_index    Offset into the index buffer.
  /// @param vertex_offset  Offset into the vertex buffer.
  IndirectDrawCommand generateDrawCommand(u32 index_count, u32 first_index = 0,
                                          i32 vertex_offset = 0) const;

  /// CPU frustum culling: filter instances by frustum and return visible indices.
  /// @param bounds     Per-instance bounding boxes (must match instance count).
  /// @param frustum    View frustum.
  /// @return Indices of visible instances.
  std::vector<u32> frustumCull(const std::vector<AABB>& bounds,
                               const Frustum& frustum) const;

  /// Build a frustum from a view-projection matrix.
  static Frustum extractFrustum(const glm::mat4& view_projection);

  /// Test if an AABB is inside or intersects a frustum.
  static bool isAABBVisible(const AABB& aabb, const Frustum& frustum);

 private:
  std::vector<InstanceData> instances_;
};

} // namespace hongtu::renderer
