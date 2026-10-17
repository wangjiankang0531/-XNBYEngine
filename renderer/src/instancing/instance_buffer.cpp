#include <hongtu/renderer/instancing/instance_buffer.hpp>
#include <algorithm>
#include <cmath>

namespace hongtu::renderer {

void InstanceBuffer::reserve(u32 capacity) {
  instances_.reserve(capacity);
}

void InstanceBuffer::clear() {
  instances_.clear();
}

u32 InstanceBuffer::addInstance(const InstanceData& data) {
  u32 index = static_cast<u32>(instances_.size());
  instances_.push_back(data);
  return index;
}

void InstanceBuffer::updateInstance(u32 index, const InstanceData& data) {
  instances_[index] = data;
}

void InstanceBuffer::removeInstance(u32 index) {
  if (index < instances_.size() - 1) {
    instances_[index] = instances_.back();
  }
  instances_.pop_back();
}

IndirectDrawCommand InstanceBuffer::generateDrawCommand(u32 index_count, u32 first_index,
                                                         i32 vertex_offset) const {
  return {index_count, static_cast<u32>(instances_.size()), first_index, vertex_offset, 0};
}

Frustum InstanceBuffer::extractFrustum(const glm::mat4& vp) {
  Frustum f;
  // Left
  f.planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0],
                           vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
  // Right
  f.planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0],
                           vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
  // Bottom
  f.planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1],
                           vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
  // Top
  f.planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1],
                           vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
  // Near
  f.planes[4] = glm::vec4(vp[0][3] + vp[0][2], vp[1][3] + vp[1][2],
                           vp[2][3] + vp[2][2], vp[3][3] + vp[3][2]);
  // Far
  f.planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2],
                           vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);

  // Normalize all planes
  for (auto& plane : f.planes) {
    f32 len = std::sqrt(plane.x * plane.x + plane.y * plane.y + plane.z * plane.z);
    if (len > 0.0f) plane /= len;
  }

  return f;
}

bool InstanceBuffer::isAABBVisible(const AABB& aabb, const Frustum& frustum) {
  auto center = aabb.center();
  auto extents = aabb.extents();

  for (const auto& plane : frustum.planes) {
    // Compute effective radius for this plane
    f32 r = extents.x * std::abs(plane.x) +
            extents.y * std::abs(plane.y) +
            extents.z * std::abs(plane.z);
    f32 dist = plane.x * center.x + plane.y * center.y + plane.z * center.z + plane.w;

    if (dist < -r) return false; // Fully outside this plane
  }
  return true; // Inside or intersecting all planes
}

std::vector<u32> InstanceBuffer::frustumCull(const std::vector<AABB>& bounds,
                                              const Frustum& frustum) const {
  std::vector<u32> visible;
  u32 count = std::min(static_cast<u32>(instances_.size()), static_cast<u32>(bounds.size()));

  for (u32 i = 0; i < count; ++i) {
    if (isAABBVisible(bounds[i], frustum)) {
      visible.push_back(i);
    }
  }

  return visible;
}

} // namespace hongtu::renderer
