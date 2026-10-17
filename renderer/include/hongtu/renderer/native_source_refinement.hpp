#pragma once
#include <algorithm>
#include <cmath>
namespace hongtu::renderer {
  // Screen tessellation/texel heuristic, not an authored geometric-deviation SSE.
  inline float nativeSourceRefinementScore(
      float qualityPixels, float projectedDiameter, float viewportHeight, float centerDistance, bool canRefine) {
    if (!canRefine || !std::isfinite(qualityPixels) || !std::isfinite(projectedDiameter) ||
        !std::isfinite(viewportHeight) || !std::isfinite(centerDistance) || qualityPixels <= 6 ||
        projectedDiameter <= 0 || viewportHeight <= 0 || centerDistance < 0)
      return 0;
    const float coverage = std::min(2.0f, projectedDiameter / viewportHeight);
    return std::log2(1.0f + qualityPixels / 6.0f) * coverage * coverage / (1.0f + centerDistance);
  }
} // namespace hongtu::renderer
