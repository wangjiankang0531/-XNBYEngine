#pragma once

#include <algorithm>
#include <cmath>

namespace hongtu::renderer {
  // Screen tessellation is a quality heuristic, not an author-supplied bound on
  // geometric error. Keep hysteresis and viewport scaling explicit.
  inline float projectedNativeEdge(float edge_m, float distance_m, float fov_degrees, int viewport_height) {
    if (!std::isfinite(edge_m) || !std::isfinite(distance_m) || edge_m <= 0 || viewport_height <= 0 ||
        fov_degrees < 5 || fov_degrees > 150)
      return 0;
    return edge_m * viewport_height / (2 * std::max(distance_m, 1.0f) * std::tan(fov_degrees * 0.00872664626f));
  }

  inline int selectNativeGeometryDepth(float projected_root, float projected_edge, int resident_depth, int max_depth) {
    if (!std::isfinite(projected_root) || !std::isfinite(projected_edge) || max_depth <= 0)
      return 0;
    if (projected_root < (resident_depth > 0 ? 24.0f : 32.0f))
      return 0;
    if (resident_depth <= 0)
      return std::min(3, max_depth);
    if (projected_edge > 6.0f)
      return std::min(resident_depth + 1, max_depth);
    // Within a visible root, hold a ready level rather than re-decoding on tiny
    // oscillations. The far-view threshold releases it to its complete base.
    return resident_depth;
  }

  // Use the measured base tessellation to decide whether refinement is useful.
  // The wider 2..6 pixel dead band allows a real coarser transaction on retreat
  // without oscillating between neighboring levels whose edge lengths differ.
  inline int selectAdaptiveNativeDepth(float projected_root,
                                       float projected_base_edge,
                                       float projected_edge,
                                       int resident_depth,
                                       int max_depth,
                                       float projected_parent_edge = 0,
                                       float projected_base_texel = 0,
                                       float projected_texel = 0,
                                       float projected_parent_texel = 0) {
    // A source texel above 1.5 pixels is visibly magnified even on dense geometry.
    projected_base_edge = std::max(projected_base_edge, projected_base_texel * 4.0f);
    projected_edge = std::max(projected_edge, projected_texel * 4.0f);
    projected_parent_edge = std::max(projected_parent_edge, projected_parent_texel * 4.0f);
    if (!std::isfinite(projected_base_edge) || projected_base_edge <= 0)
      return 0;
    if (projected_base_edge < (resident_depth > 0 ? 3.0f : 6.0f))
      return 0;
    const int next = selectNativeGeometryDepth(projected_root, projected_edge, resident_depth, max_depth);
    if (next && resident_depth > 3 && projected_edge < 2.0f && projected_parent_edge > 0 &&
        projected_parent_edge <= 4.0f)
      return resident_depth - 1;
    return next;
  }
} // namespace hongtu::renderer
