#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/gis/tile/tile_coord.hpp>
#include <glm/vec2.hpp>
#include <optional>

namespace hongtu::renderer {

/// Result of an ancestor-fallback lookup.
///
/// `source_coord` is the tile whose texture should actually be sampled.
/// `offset` and `scale` map the leaf's [0,1]² UV onto the ancestor texture:
///     uv_in_ancestor = uv_leaf * scale + offset
///
/// Pack into a shader `vec4 uv_offset_scale` as
///     glm::vec4(offset.x, offset.y, scale.x, scale.y)
/// (i.e. xy = offset, zw = scale). The vertex shader reads
///     outUV = inUV * pc.uv_offset_scale.zw + pc.uv_offset_scale.xy;
struct AncestorFallback {
  gis::TileCoord source_coord;
  glm::vec2 offset;  // (off.x, off.y) — packed into vec4.xy
  glm::vec2 scale;   // (scale.x, scale.y) — packed into vec4.zw
};

/// Compute the UV (offset, scale) that maps the leaf onto the ancestor texture.
/// Exposed for testing; `resolveAncestorFallback` calls it internally.
///
/// Pre: `ancestor.z <= leaf.z` and `ancestor` is an actual ancestor of `leaf`.
void computeUvRemap(const gis::TileCoord& leaf, const gis::TileCoord& ancestor,
                    glm::vec2& offset, glm::vec2& scale);

// ----------------------- template implementation -----------------------

/// Walk up the quadtree from `leaf` looking for the nearest ancestor whose
/// `owns_real_texture` is true. Caps the search depth at `max_dz` levels
/// above the leaf — beyond this, the upsampling is so coarse (1/2^max_dz
/// per axis) that the result is visually indistinguishable from a single
/// solid color, which is worse than skipping the leaf entirely (the z=3
/// permanent base layer is then visible at its own coord with correct UV).
///
/// Default `max_dz = 4` corresponds to a 16× upsample (16×16 ancestor
/// pixels per leaf), which still has recognizable shape after bilinear
/// filtering. Pass a larger `max_dz` to accept coarser fallback, or 0 to
/// require an exact-match leaf-self texture.
template <typename TileT, typename Finder>
std::optional<AncestorFallback> resolveAncestorFallback(
    const gis::TileCoord& leaf, Finder find, i32 max_dz = 4) {
  gis::TileCoord c = leaf;
  i32 dz = 0;
  while (true) {
    const TileT* t = find(c);
    if (t && t->owns_real_texture) {
      AncestorFallback out;
      out.source_coord = c;
      computeUvRemap(leaf, c, out.offset, out.scale);
      return out;
    }
    if (c.z == 0 || dz >= max_dz) break;
    c = c.parent();
    ++dz;
  }
  return std::nullopt;
}

}  // namespace hongtu::renderer
