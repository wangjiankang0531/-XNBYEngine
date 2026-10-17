#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/gis/tile/tile_coord.hpp>
#include <array>
#include <glm/vec3.hpp>
#include <vector>

namespace hongtu::gis {

inline constexpr std::size_t kTileGeometryVertexCount = 33 * 33;  // 1089

struct GeometryTile {
  TileCoord coord;
  std::array<glm::vec3, kTileGeometryVertexCount> vertices{};
  std::array<f32, kTileGeometryVertexCount> heights{};
  f32 min_height = 0.0f;
  f32 max_height = 0.0f;
  i32 last_edge_scales = 0;  // bit-packed L|R|T|B (see IndexBufferTable::tableIndex)
  bool dirty_height = false;   ///< set by DemBaker, cleared by mesh rebuild
  bool dem_baked = false;      ///< true once heights[] has been populated from a DEM source; prevents re-baking the same DEM every frame
  u64 frame = 0;
};

class TileGeometryCache {
 public:
  /// Find-or-create the tile and stamp it with `frame`. Stable pointer
  /// until the next eviction.
  GeometryTile* touch(const TileCoord& coord, u64 frame);

  /// Same as touch but returns nullptr if absent (no insert).
  GeometryTile* find(const TileCoord& coord);

  /// Peak×ratio LRU: cap = max(used_this_frame, peak_seen × ratio).
  /// Erases tiles whose frame < newest cap-th frame.
  void endFrameWithEviction(u64 frame);

  std::size_t size() const { return tiles_.size(); }
  void setRatio(f32 r) { ratio_ = r; }
  void clear() { tiles_.clear(); peak_ = 0; }

  /// Direct access to the underlying storage (sorted by coord). Useful for
  /// iterating to invalidate DEM bakes across all entries when a new DEM
  /// tile arrives. Pointer/iterator stability holds only between mutating
  /// calls (touch / clear / endFrameWithEviction).
  std::vector<GeometryTile>& tiles() { return tiles_; }
  const std::vector<GeometryTile>& tiles() const { return tiles_; }

 private:
  std::vector<GeometryTile> tiles_;     // sorted by coord
  u32 peak_ = 0;
  f32 ratio_ = 2.0f;
};

}  // namespace hongtu::gis
