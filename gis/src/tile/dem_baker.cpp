#include <hongtu/gis/tile/dem_baker.hpp>
#include <limits>

namespace hongtu::gis {

namespace {

/// Map a leaf UV into an ancestor's UV space.
/// Returns offset (ou, ov) and scale (su, sv) such that:
///   ancestor_uv = leaf_uv * scale + offset
void leafToAncestorUv(const TileCoord& leaf, const TileCoord& ancestor,
                      f32& off_u, f32& off_v, f32& scale_u, f32& scale_v) {
  const i32 dz = leaf.z - ancestor.z;
  const i32 div = 1 << dz;
  scale_u = scale_v = 1.0f / static_cast<f32>(div);
  const i32 ox = leaf.x - (ancestor.x << dz);
  const i32 oy = leaf.y - (ancestor.y << dz);
  off_u = static_cast<f32>(ox) / static_cast<f32>(div);
  off_v = static_cast<f32>(oy) / static_cast<f32>(div);
}

}  // namespace

bool bakeDemIntoGeometry(
    GeometryTile& geom,
    const std::function<const DemTile*(const TileCoord&)>& dem_for,
    i32 target_dem_level) {
  TileCoord c = geom.coord;
  // If caller specified a target level, first walk up to AT MOST that
  // level (or stop at the leaf if it's already shallower). This forces
  // every tile to bake from the same DEM zoom — adjacent tiles end up
  // sampling edge-consistent heights from the SAME provider tile (or
  // from sibling provider tiles whose Terrarium-style edge pixels match
  // by construction), so the rendered surface joins without elevation
  // cracks. Without this, two neighbors could pick different deepest
  // DEMs depending on what was loaded at bake time, and the resulting
  // height mismatch shows as a vertical wall at their shared edge.
  if (target_dem_level >= 0) {
    while (c.z > target_dem_level) c = c.parent();
  }
  // Walk further up until a loaded DEM is found.
  const DemTile* dem = nullptr;
  while (true) {
    dem = dem_for(c);
    if (dem) break;
    if (c.z == 0) return false;
    c = c.parent();
  }

  // Compute UV mapping from leaf into ancestor tile space.
  f32 ou, ov, su, sv;
  leafToAncestorUv(geom.coord, c, ou, ov, su, sv);

  constexpr i32 N = 33;
  f32 hmin =  std::numeric_limits<f32>::infinity();
  f32 hmax = -std::numeric_limits<f32>::infinity();

  for (i32 row = 0; row < N; ++row) {
    const f32 leaf_v = static_cast<f32>(row) / 32.0f;
    for (i32 col = 0; col < N; ++col) {
      const f32 leaf_u = static_cast<f32>(col) / 32.0f;
      const f32 anc_u = leaf_u * su + ou;
      const f32 anc_v = leaf_v * sv + ov;
      // Clamp bathymetry to sea level: Terrarium carries ocean depth as
      // negative heights, and neighboring LOD rings smooth it differently —
      // at grazing angles the mismatched ocean meshes read as rectangular
      // plateau ridges on the water. We render the sea SURFACE, not the
      // seabed, so 0 is the right height for all ocean. (Costs accuracy only
      // at land depressions like the Dead Sea, −430 m.)
      const f32 h = std::max(0.0f, dem->sample(anc_u, anc_v));
      geom.heights[row * N + col] = h;
      hmin = std::min(hmin, h);
      hmax = std::max(hmax, h);
    }
  }

  geom.min_height = hmin;
  geom.max_height = hmax;
  geom.dirty_height = true;
  return true;
}

}  // namespace hongtu::gis
