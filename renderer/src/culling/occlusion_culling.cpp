#include <hongtu/renderer/culling/occlusion_culling.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace hongtu::renderer {

// ============================================================================
// HiZDepthBuffer
// ============================================================================

void HiZDepthBuffer::build(const float* depth_buffer, u32 width, u32 height, u32 mip_count) {
  mips_.clear();

  if (!depth_buffer || width == 0 || height == 0) return;

  // Mip 0: copy from input depth buffer
  MipLevel mip0;
  mip0.width = width;
  mip0.height = height;
  mip0.data.assign(depth_buffer, depth_buffer + static_cast<size_t>(width) * height);
  mips_.push_back(std::move(mip0));

  // Build subsequent mip levels using max reduction (conservative occlusion)
  for (u32 level = 1; level < mip_count; ++level) {
    const MipLevel& prev = mips_[level - 1];
    u32 prev_w = prev.width;
    u32 prev_h = prev.height;

    u32 cur_w = std::max(prev_w / 2, 1u);
    u32 cur_h = std::max(prev_h / 2, 1u);

    MipLevel mip;
    mip.width = cur_w;
    mip.height = cur_h;
    mip.data.resize(static_cast<size_t>(cur_w) * cur_h);

    for (u32 y = 0; y < cur_h; ++y) {
      for (u32 x = 0; x < cur_w; ++x) {
        u32 sx = x * 2;
        u32 sy = y * 2;

        // Gather 2x2 block from previous mip, take max (farthest depth)
        float max_depth = prev.data[sy * prev_w + sx];

        if (sx + 1 < prev_w) {
          max_depth = std::max(max_depth, prev.data[sy * prev_w + sx + 1]);
        }
        if (sy + 1 < prev_h) {
          max_depth = std::max(max_depth, prev.data[(sy + 1) * prev_w + sx]);
        }
        if (sx + 1 < prev_w && sy + 1 < prev_h) {
          max_depth = std::max(max_depth, prev.data[(sy + 1) * prev_w + sx + 1]);
        }

        mip.data[y * cur_w + x] = max_depth;
      }
    }

    mips_.push_back(std::move(mip));

    if (cur_w == 1 && cur_h == 1) break;
  }
}

bool HiZDepthBuffer::isOccluded(const glm::vec2& min_xy, const glm::vec2& max_xy,
                                 float min_depth) const {
  if (mips_.empty()) return false;

  // Compute the screen-space size of the query rectangle
  float rect_w = max_xy.x - min_xy.x;
  float rect_h = max_xy.y - min_xy.y;
  float max_dim = std::max(rect_w, rect_h);

  // Select mip level where the rectangle covers roughly 2x2 texels
  u32 mip_level = 0;
  if (max_dim > 0.0f) {
    float base_dim = static_cast<float>(std::max(mips_[0].width, mips_[0].height));
    float levels = std::log2(max_dim / 2.0f);
    if (levels > 0.0f) {
      mip_level = static_cast<u32>(std::floor(levels));
    }
  }
  mip_level = std::min(mip_level, static_cast<u32>(mips_.size()) - 1);

  const MipLevel& mip = mips_[mip_level];
  if (mip.width == 0 || mip.height == 0) return false;

  // Scale screen coords to mip level
  float scale_x = static_cast<float>(mip.width) / static_cast<float>(mips_[0].width);
  float scale_y = static_cast<float>(mip.height) / static_cast<float>(mips_[0].height);

  i32 x0 = std::max(0, static_cast<i32>(std::floor(min_xy.x * scale_x)));
  i32 y0 = std::max(0, static_cast<i32>(std::floor(min_xy.y * scale_y)));
  i32 x1 = std::min(static_cast<i32>(mip.width) - 1,
                     static_cast<i32>(std::ceil(max_xy.x * scale_x)));
  i32 y1 = std::min(static_cast<i32>(mip.height) - 1,
                     static_cast<i32>(std::ceil(max_xy.y * scale_y)));

  // Find the maximum depth in the Hi-Z region
  float hi_z_depth = 0.0f;
  for (i32 y = y0; y <= y1; ++y) {
    for (i32 x = x0; x <= x1; ++x) {
      hi_z_depth = std::max(hi_z_depth, mip.data[y * mip.width + x]);
    }
  }

  // Object is occluded if its closest depth is farther than the Hi-Z depth
  // (assuming depth in [0,1] where 0=near, 1=far, and reverse-Z would flip this)
  return min_depth > hi_z_depth;
}

const std::vector<float>& HiZDepthBuffer::getMipData(u32 level) const {
  return mips_[level].data;
}

void HiZDepthBuffer::getMipSize(u32 level, u32& w, u32& h) const {
  if (level < mips_.size()) {
    w = mips_[level].width;
    h = mips_[level].height;
  } else {
    w = 0;
    h = 0;
  }
}

// ============================================================================
// OcclusionCullingSystem
// ============================================================================

OcclusionCullingSystem::OcclusionCullingSystem(const OcclusionCullingConfig& config)
    : config_(config) {}

bool OcclusionCullingSystem::projectAABB(const AABB& aabb, const glm::mat4& vp,
                                          glm::vec2& min_ss, glm::vec2& max_ss,
                                          float& min_depth, u32 screen_w,
                                          u32 screen_h) const {
  // Project all 8 corners of the AABB
  glm::vec3 corners[8] = {
      {aabb.min_point.x, aabb.min_point.y, aabb.min_point.z},
      {aabb.max_point.x, aabb.min_point.y, aabb.min_point.z},
      {aabb.min_point.x, aabb.max_point.y, aabb.min_point.z},
      {aabb.max_point.x, aabb.max_point.y, aabb.min_point.z},
      {aabb.min_point.x, aabb.min_point.y, aabb.max_point.z},
      {aabb.max_point.x, aabb.min_point.y, aabb.max_point.z},
      {aabb.min_point.x, aabb.max_point.y, aabb.max_point.z},
      {aabb.max_point.x, aabb.max_point.y, aabb.max_point.z},
  };

  min_ss = glm::vec2(std::numeric_limits<float>::max());
  max_ss = glm::vec2(std::numeric_limits<float>::lowest());
  min_depth = std::numeric_limits<float>::max();

  bool any_in_front = false;

  for (int i = 0; i < 8; ++i) {
    glm::vec4 clip = vp * glm::vec4(corners[i], 1.0f);

    // Skip points behind the near plane
    if (clip.w <= 0.0f) {
      // If any corner is behind camera, conservatively mark as visible
      // (partially behind camera means it could be very large on screen)
      min_ss = glm::vec2(0.0f);
      max_ss = glm::vec2(static_cast<float>(screen_w), static_cast<float>(screen_h));
      min_depth = 0.0f;
      return true;
    }

    any_in_front = true;

    // Perspective divide -> NDC [-1, 1]
    float inv_w = 1.0f / clip.w;
    float ndc_x = clip.x * inv_w;
    float ndc_y = clip.y * inv_w;
    float ndc_z = clip.z * inv_w;

    // NDC to screen coords [0, width/height]
    float sx = (ndc_x * 0.5f + 0.5f) * static_cast<float>(screen_w);
    float sy = (ndc_y * 0.5f + 0.5f) * static_cast<float>(screen_h);
    // Depth in [0, 1]
    float depth = ndc_z * 0.5f + 0.5f;

    min_ss.x = std::min(min_ss.x, sx);
    min_ss.y = std::min(min_ss.y, sy);
    max_ss.x = std::max(max_ss.x, sx);
    max_ss.y = std::max(max_ss.y, sy);
    min_depth = std::min(min_depth, depth);
  }

  if (!any_in_front) return false;

  // Clamp to screen bounds
  min_ss.x = std::max(min_ss.x, 0.0f);
  min_ss.y = std::max(min_ss.y, 0.0f);
  max_ss.x = std::min(max_ss.x, static_cast<float>(screen_w));
  max_ss.y = std::min(max_ss.y, static_cast<float>(screen_h));

  return true;
}

CullingResult OcclusionCullingSystem::frustumCull(const std::vector<CullObject>& objects,
                                                   const Frustum& frustum) const {
  CullingResult result;
  result.total_objects = static_cast<u32>(objects.size());

  for (u32 i = 0; i < static_cast<u32>(objects.size()); ++i) {
    if (InstanceBuffer::isAABBVisible(objects[i].bounds, frustum)) {
      result.visible_objects.push_back(i);
    } else {
      result.culled_by_frustum++;
    }
  }

  if (result.total_objects > 0) {
    result.cull_efficiency =
        static_cast<f32>(result.culled_by_frustum) / static_cast<f32>(result.total_objects) * 100.0f;
  }

  return result;
}

CullingResult OcclusionCullingSystem::occlusionCull(const std::vector<CullObject>& objects,
                                                     const Frustum& frustum,
                                                     const glm::mat4& view_proj,
                                                     const float* depth_buffer,
                                                     u32 depth_w, u32 depth_h) {
  CullingResult result;
  result.total_objects = static_cast<u32>(objects.size());

  // Build Hi-Z pyramid from previous frame's depth
  if (depth_buffer && !config_.freeze_culling) {
    hi_z_buffer_.build(depth_buffer, depth_w, depth_h, config_.hi_z_mip_count);
  }

  for (u32 i = 0; i < static_cast<u32>(objects.size()); ++i) {
    const CullObject& obj = objects[i];

    // Stage 1: Frustum culling
    if (!InstanceBuffer::isAABBVisible(obj.bounds, frustum)) {
      result.culled_by_frustum++;
      continue;
    }

    // Stage 2: Hi-Z occlusion culling
    if (depth_buffer && hi_z_buffer_.getMipCount() > 0 && !config_.freeze_culling) {
      glm::vec2 min_ss, max_ss;
      float obj_min_depth;

      bool projected =
          projectAABB(obj.bounds, view_proj, min_ss, max_ss, obj_min_depth, depth_w, depth_h);

      if (projected) {
        if (hi_z_buffer_.isOccluded(min_ss, max_ss, obj_min_depth - config_.occlusion_threshold)) {
          result.culled_by_occlusion++;
          continue;
        }
      }
    }

    result.visible_objects.push_back(i);
  }

  if (result.total_objects > 0) {
    u32 total_culled = result.culled_by_frustum + result.culled_by_occlusion;
    result.cull_efficiency =
        static_cast<f32>(total_culled) / static_cast<f32>(result.total_objects) * 100.0f;
  }

  return result;
}

std::vector<IndirectDrawCommand> OcclusionCullingSystem::generateDrawCommands(
    const CullingResult& result, u32 index_count_per_object, u32 first_index) const {
  std::vector<IndirectDrawCommand> commands;
  commands.reserve(result.visible_objects.size());

  for (u32 obj_idx : result.visible_objects) {
    IndirectDrawCommand cmd{};
    cmd.index_count = index_count_per_object;
    cmd.instance_count = 1;
    cmd.first_index = first_index;
    cmd.vertex_offset = 0;
    cmd.first_instance = obj_idx;
    commands.push_back(cmd);
  }

  return commands;
}

}  // namespace hongtu::renderer
