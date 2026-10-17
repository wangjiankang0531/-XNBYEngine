#include <hongtu/renderer/lod/lod_system.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace hongtu::renderer {

// ============================================================================
// LODSelector
// ============================================================================

LODSelector::LODSelector(const LODConfig& config) : config_(config) {
  setDefaultThresholds(config.max_levels);
}

void LODSelector::setDefaultThresholds(u32 num_levels) {
  levels_.clear();
  levels_.reserve(num_levels);

  for (u32 i = 0; i < num_levels; ++i) {
    LODLevel level;
    level.level = i;
    // Geometric progression: each level requires 1/4 of previous screen area
    // LOD 0: >= 0.25, LOD 1: >= 0.0625, LOD 2: >= 0.015625, etc.
    level.screen_area_min = (i == num_levels - 1) ? 0.0f : std::pow(0.25f, static_cast<f32>(i + 1));
    // Triangle ratios: 1.0, 0.5, 0.25, 0.125, 0.05
    f32 ratios[] = {1.0f, 0.5f, 0.25f, 0.125f, 0.05f};
    level.triangle_ratio = (i < 5) ? ratios[i] : 0.05f;
    levels_.push_back(level);
  }
}

void LODSelector::setLevels(const std::vector<LODLevel>& levels) {
  levels_ = levels;
  // Sort by screen_area_min descending (highest detail first)
  std::sort(levels_.begin(), levels_.end(), [](const LODLevel& a, const LODLevel& b) {
    return a.screen_area_min > b.screen_area_min;
  });
}

f32 LODSelector::priorityFromScreenArea(f32 geometric_error, f32 distance,
                                        f32 fov_y, f32 screen_height) {
  if (distance <= 0.0f || geometric_error <= 0.0f) return 0.0f;
  // SSE = geometric_error * screen_height / (distance * 2 * tan(fov_y/2)).
  const f32 denom = distance * 2.0f * std::tan(fov_y * 0.5f);
  if (denom <= 0.0f) return 0.0f;
  return geometric_error * screen_height / denom;
}

f32 LODSelector::computeScreenArea(f32 radius, f32 distance, f32 fov_y, f32 screen_height) {
  if (distance <= 0.0f || radius <= 0.0f) return 1.0f;

  // Project sphere radius to screen pixels
  f32 projected_radius = (radius / distance) * (screen_height / (2.0f * std::tan(fov_y * 0.5f)));
  // Normalized screen area (as fraction of screen height squared)
  f32 area = (3.14159265f * projected_radius * projected_radius) / (screen_height * screen_height);
  return std::min(area, 1.0f);
}

LODSelection LODSelector::selectLOD(f32 bounding_sphere_radius, f32 distance_to_camera,
                                    f32 fov_y, f32 screen_height) const {
  f32 screen_area = computeScreenArea(bounding_sphere_radius, distance_to_camera,
                                      fov_y, screen_height);

  // Apply LOD bias
  screen_area *= std::pow(2.0f, -config_.bias);

  LODSelection result;
  result.screen_area = screen_area;
  result.level = 0;
  result.blend_factor = 0.0f;

  if (levels_.empty()) return result;

  // Find appropriate LOD level
  for (u32 i = 0; i < static_cast<u32>(levels_.size()); ++i) {
    if (screen_area >= levels_[i].screen_area_min) {
      result.level = levels_[i].level;

      // Compute cross-fade blend factor
      if (config_.enable_crossfade && i + 1 < static_cast<u32>(levels_.size())) {
        f32 threshold = levels_[i].screen_area_min;
        f32 range = threshold * config_.crossfade_range;
        if (range > 0.0f && screen_area < threshold + range) {
          result.blend_factor = 1.0f - (screen_area - threshold) / range;
        }
      }
      break;
    }
    result.level = levels_[i].level;
  }

  return result;
}

// ============================================================================
// QEM Mesh Simplification (edge collapse based)
// ============================================================================

namespace qem {

/// Quadric error matrix (4x4 symmetric, stored as 10 floats).
struct Quadric {
  f64 a[10] = {}; // Upper triangle of 4x4 symmetric matrix

  void addPlane(f64 nx, f64 ny, f64 nz, f64 d) {
    // Q = p * p^T where p = [nx, ny, nz, d]
    a[0] += nx * nx; a[1] += nx * ny; a[2] += nx * nz; a[3] += nx * d;
    a[4] += ny * ny; a[5] += ny * nz; a[6] += ny * d;
    a[7] += nz * nz; a[8] += nz * d;
    a[9] += d * d;
  }

  Quadric operator+(const Quadric& other) const {
    Quadric result;
    for (int i = 0; i < 10; ++i) result.a[i] = a[i] + other.a[i];
    return result;
  }

  f64 evaluate(f64 x, f64 y, f64 z) const {
    // v^T * Q * v where v = [x, y, z, 1]
    return a[0] * x * x + 2 * a[1] * x * y + 2 * a[2] * x * z + 2 * a[3] * x
         + a[4] * y * y + 2 * a[5] * y * z + 2 * a[6] * y
         + a[7] * z * z + 2 * a[8] * z
         + a[9];
  }
};

SimplifiedMesh simplify(const std::vector<glm::vec3>& vertices,
                        const std::vector<u32>& indices,
                        f32 target_ratio) {
  if (target_ratio >= 1.0f || vertices.empty() || indices.size() < 3) {
    return {vertices, indices, static_cast<u32>(indices.size() / 3)};
  }

  u32 target_triangles = std::max(1u, static_cast<u32>(indices.size() / 3 * target_ratio));

  // Build adjacency: for each vertex, list of triangle indices
  u32 num_tris = static_cast<u32>(indices.size() / 3);
  std::vector<std::vector<u32>> vertex_tris(vertices.size());
  for (u32 t = 0; t < num_tris; ++t) {
    vertex_tris[indices[t * 3 + 0]].push_back(t);
    vertex_tris[indices[t * 3 + 1]].push_back(t);
    vertex_tris[indices[t * 3 + 2]].push_back(t);
  }

  // Compute per-vertex quadrics from adjacent face planes
  std::vector<Quadric> quadrics(vertices.size());
  for (u32 t = 0; t < num_tris; ++t) {
    auto v0 = vertices[indices[t * 3 + 0]];
    auto v1 = vertices[indices[t * 3 + 1]];
    auto v2 = vertices[indices[t * 3 + 2]];
    auto edge1 = v1 - v0;
    auto edge2 = v2 - v0;
    auto normal = glm::normalize(glm::cross(edge1, edge2));
    f64 d = -glm::dot(glm::dvec3(normal), glm::dvec3(v0));

    for (int j = 0; j < 3; ++j) {
      quadrics[indices[t * 3 + j]].addPlane(normal.x, normal.y, normal.z, d);
    }
  }

  // Simple greedy edge collapse
  // Use a working copy of indices and mark collapsed triangles
  std::vector<u32> work_indices = indices;
  std::vector<glm::vec3> work_vertices = vertices;
  std::vector<bool> tri_alive(num_tris, true);
  std::vector<u32> vertex_remap(vertices.size());
  for (u32 i = 0; i < static_cast<u32>(vertices.size()); ++i) vertex_remap[i] = i;

  auto getRoot = [&](u32 v) -> u32 {
    while (vertex_remap[v] != v) v = vertex_remap[v] = vertex_remap[vertex_remap[v]];
    return v;
  };

  u32 alive_tris = num_tris;

  while (alive_tris > target_triangles) {
    // Find best edge to collapse (brute force - simple but O(n²) per pass)
    f64 best_cost = 1e30;
    u32 best_v0 = 0, best_v1 = 0;
    glm::vec3 best_pos(0);

    for (u32 t = 0; t < num_tris && alive_tris > target_triangles; ++t) {
      if (!tri_alive[t]) continue;
      for (int e = 0; e < 3; ++e) {
        u32 i0 = getRoot(work_indices[t * 3 + e]);
        u32 i1 = getRoot(work_indices[t * 3 + (e + 1) % 3]);
        if (i0 == i1) continue;

        Quadric combined = quadrics[i0] + quadrics[i1];
        // Use midpoint as collapse target
        auto mid = (work_vertices[i0] + work_vertices[i1]) * 0.5f;
        f64 cost = combined.evaluate(mid.x, mid.y, mid.z);

        if (cost < best_cost) {
          best_cost = cost;
          best_v0 = i0;
          best_v1 = i1;
          best_pos = mid;
        }
      }
    }

    if (best_v0 == best_v1) break; // No more edges to collapse

    // Collapse edge: merge v1 into v0
    vertex_remap[best_v1] = best_v0;
    work_vertices[best_v0] = best_pos;
    quadrics[best_v0] = quadrics[best_v0] + quadrics[best_v1];

    // Remove degenerate triangles
    for (u32 t = 0; t < num_tris; ++t) {
      if (!tri_alive[t]) continue;
      u32 a = getRoot(work_indices[t * 3 + 0]);
      u32 b = getRoot(work_indices[t * 3 + 1]);
      u32 c = getRoot(work_indices[t * 3 + 2]);
      if (a == b || b == c || a == c) {
        tri_alive[t] = false;
        --alive_tris;
      }
    }
  }

  // Build output mesh
  SimplifiedMesh result;
  std::unordered_map<u32, u32> vertex_map;

  for (u32 t = 0; t < num_tris; ++t) {
    if (!tri_alive[t]) continue;
    for (int j = 0; j < 3; ++j) {
      u32 v = getRoot(work_indices[t * 3 + j]);
      if (vertex_map.find(v) == vertex_map.end()) {
        vertex_map[v] = static_cast<u32>(result.vertices.size());
        result.vertices.push_back(work_vertices[v]);
      }
      result.indices.push_back(vertex_map[v]);
    }
  }

  result.triangle_count = static_cast<u32>(result.indices.size() / 3);
  return result;
}

std::vector<SimplifiedMesh> generateLODChain(const std::vector<glm::vec3>& vertices,
                                              const std::vector<u32>& indices,
                                              const std::vector<f32>& ratios) {
  std::vector<SimplifiedMesh> chain;
  chain.reserve(ratios.size());

  for (f32 ratio : ratios) {
    chain.push_back(simplify(vertices, indices, ratio));
  }

  return chain;
}

} // namespace qem

} // namespace hongtu::renderer
