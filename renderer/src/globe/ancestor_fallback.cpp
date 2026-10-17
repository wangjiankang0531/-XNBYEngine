#include <hongtu/renderer/globe/ancestor_fallback.hpp>

namespace hongtu::renderer {

void computeUvRemap(const gis::TileCoord& leaf, const gis::TileCoord& ancestor,
                    glm::vec2& offset, glm::vec2& scale) {
  const i32 dz = leaf.z - ancestor.z;
  const i32 div = 1 << dz;
  const f32 inv_div = 1.0f / static_cast<f32>(div);

  scale = glm::vec2(inv_div, inv_div);

  const i32 ox = leaf.x - (ancestor.x << dz);
  const i32 oy = leaf.y - (ancestor.y << dz);
  offset = glm::vec2(static_cast<f32>(ox) * inv_div,
                     static_cast<f32>(oy) * inv_div);
}

}  // namespace hongtu::renderer
