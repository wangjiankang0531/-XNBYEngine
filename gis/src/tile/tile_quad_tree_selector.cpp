#include <hongtu/gis/tile/tile_quad_tree_selector.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <unordered_map>

namespace hongtu::gis {

using namespace hongtu::coordinate;

namespace {

/// Pixels-on-screen wide for a tile of `tile_length` meters at `distance`.
/// Matches DeepEye TerrainView.cpp:621 calcResolution but rewritten in our
/// units (texels/pixel; >=1 means "tile already as sharp as the screen").
f32 calcTexelsPerPixel(f64 distance, f64 tile_length, f64 tan_fov_y,
                        f32 screen_height, i32 tile_pixel_size = 256) {
  if (tile_length <= 0.0 || screen_height <= 0.0f) return 1e9f;
  const f64 y_length = tan_fov_y * distance;
  const f64 result = (y_length * static_cast<f64>(tile_pixel_size)) /
                     (tile_length * static_cast<f64>(screen_height));
  return static_cast<f32>(result);
}

f32 computeSlopeFactor(f64 distance, f64 tile_length,
                       const glm::dvec3& closest, const glm::dvec3& camera,
                       f32 cap_near, f32 cap_far) {
  if (distance <= tile_length) return 1.0f;
  glm::dvec3 cam_up = camera;
  const f64 cam_len = glm::length(cam_up);
  if (cam_len < 1.0) return 1.0f;
  cam_up /= cam_len;

  glm::dvec3 to_tile = closest - camera;
  const f64 tt_len = glm::length(to_tile);
  if (tt_len < 1.0) return 1.0f;
  to_tile /= tt_len;

  const f64 slope = std::abs(glm::dot(to_tile, cam_up));
  const f64 cap = (distance > tile_length * 4.0)
                   ? static_cast<f64>(cap_far) : static_cast<f64>(cap_near);
  // Lerp(cap, 1, slope)
  return static_cast<f32>(cap + (1.0 - cap) * slope);
}

}  // namespace

TileQuadTreeSelector::TileQuadTreeSelector() : config_() {
  assert(config_.min_level <= config_.max_level);
}

TileQuadTreeSelector::TileQuadTreeSelector(const Config& cfg) : config_(cfg) {
  assert(config_.min_level <= config_.max_level);
}

void TileQuadTreeSelector::recurse(const TileCoord& coord,
                                    FrustumIntersection parent_state,
                                    const Context& ctx,
                                    std::vector<TileCoord>& out) const {
  if (out.size() >= config_.max_visible_tiles) return;

  OBB obb;
  // Compute OBB first so we can pass it to frustum test.
  f64 min_h = 0.0, max_h = 0.0;
  if (ctx.query) (*ctx.query)(coord, min_h, max_h);
  obb = lonLatAltBoxToOBB(coord.toBounds(),
                          min_h - config_.obb_alt_margin,
                          max_h + config_.obb_alt_margin,
                          *ctx.ellipsoid);

  FrustumIntersection inter = parent_state == FrustumIntersection::Inside
                              ? FrustumIntersection::Inside
                              : frustumIntersectOBB(ctx.frustum, obb);
  if (coord.z == 0 && inter == FrustumIntersection::Outside) {
    // The root tile defines the world; if the frustum rejects it, the entire
    // traversal would be lost.  Trust that the camera is anywhere reasonable
    // (in or near the Earth's vicinity) and proceed as Intersecting.
    inter = FrustumIntersection::Intersecting;
  }
  if (inter == FrustumIntersection::Outside) return;

  // Decision using the same OBB
  const glm::dvec3 closest = closestPointOnOBB(ctx.camera_ecef, obb);
  const f64 distance = glm::length(ctx.camera_ecef - closest);
  const f64 tile_length = std::sqrt(obb.half_extent.x * obb.half_extent.x +
                                    obb.half_extent.y * obb.half_extent.y) * 2.0;
  assert(tile_length > 0.0);
  const f32 slope = computeSlopeFactor(distance, tile_length, closest, ctx.camera_ecef,
                                       config_.slope_factor_near,
                                       config_.slope_factor_far);
  const f32 tpp = calcTexelsPerPixel(distance, tile_length, ctx.tan_fov_y,
                                      ctx.screen_height) * slope;

  // Accept when tpp * land_resolution >= 1.0 (tile is sufficiently detailed).
  const bool accept = tpp * config_.land_resolution >= 1.0f;
  if (accept && coord.z >= config_.min_level) {
    out.push_back(coord);
    return;
  }

  // Either not accepted yet, below min_level, or hit max_level as hard stop.
  if (coord.z >= config_.max_level) {
    if (coord.z >= config_.min_level) out.push_back(coord);
    return;
  }

  for (const auto& child : coord.children()) {
    recurse(child, inter, ctx, out);
  }
}

void TileQuadTreeSelector::computeEdgeScales(
    const std::vector<TileCoord>& leaves,
    std::vector<SelectedTile>& out) const {
  // Build a quick lookup from coord → its z (for neighbor checks).
  std::unordered_map<TileCoord, i32, TileCoord::Hash> leaf_z;
  leaf_z.reserve(leaves.size());
  for (const auto& c : leaves) leaf_z[c] = c.z;

  auto neighborZ = [&](const TileCoord& self, i32 dx, i32 dy) -> i32 {
    const i32 max_xy = 1 << self.z;
    i32 nx = self.x + dx, ny = self.y + dy;
    if (nx < 0 || nx >= max_xy || ny < 0 || ny >= max_xy) return self.z;
    TileCoord cand{self.z, nx, ny};
    auto it = leaf_z.find(cand);
    if (it != leaf_z.end()) return it->second;
    // Else look at parent: if a coarser leaf covers this neighbor.
    TileCoord p = cand;
    while (p.z > 0) {
      p = p.parent();
      auto pit = leaf_z.find(p);
      if (pit != leaf_z.end()) return pit->second;
    }
    return self.z;
  };

  out.clear();
  out.reserve(leaves.size());
  for (const auto& c : leaves) {
    SelectedTile s;
    s.coord = c;
    s.left_scale   = neighborZ(c, -1, 0) < c.z ? 2 : 1;
    s.right_scale  = neighborZ(c, +1, 0) < c.z ? 2 : 1;
    s.top_scale    = neighborZ(c, 0, -1) < c.z ? 2 : 1;
    s.bottom_scale = neighborZ(c, 0, +1) < c.z ? 2 : 1;
    out.push_back(s);
  }
}

std::vector<SelectedTile> TileQuadTreeSelector::select(
    const glm::dvec3& camera_ecef, const glm::dmat4& view,
    const glm::dmat4& projection, f32 screen_height,
    const HeightQuery* query) const {
  Context ctx;
  ctx.camera_ecef = camera_ecef;
  const glm::dmat4 vp = projection * view;
  ctx.frustum = Frustum::fromViewProjection(vp);
  // proj[1][1] = 1 / tan(fov_y/2) for any glm::perspective-built matrix,
  // independent of the view's orientation. Use it directly, not the
  // combined matrix's [1][1] which mixes in V[1][1].
  const f64 p11 = projection[1][1];
  ctx.tan_fov_y = std::abs(p11) < 1e-9 ? 1.0 : 2.0 / std::abs(p11);
  ctx.screen_height = screen_height;
  ctx.query = query;
  ctx.ellipsoid = &WGS84;

  std::vector<TileCoord> raw;
  raw.reserve(64);
  recurse(TileCoord{0, 0, 0}, FrustumIntersection::Intersecting, ctx, raw);

  std::vector<SelectedTile> out;
  computeEdgeScales(raw, out);
  return out;
}

}  // namespace hongtu::gis
