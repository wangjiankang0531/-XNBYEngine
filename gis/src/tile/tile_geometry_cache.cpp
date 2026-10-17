#include <hongtu/gis/tile/tile_geometry_cache.hpp>
#include <algorithm>

namespace hongtu::gis {

namespace {

struct CoordLess {
  bool operator()(const GeometryTile& a, const TileCoord& b) const {
    if (a.coord.z != b.z) return a.coord.z < b.z;
    if (a.coord.x != b.x) return a.coord.x < b.x;
    return a.coord.y < b.y;
  }
  bool operator()(const TileCoord& a, const GeometryTile& b) const {
    if (a.z != b.coord.z) return a.z < b.coord.z;
    if (a.x != b.coord.x) return a.x < b.coord.x;
    return a.y < b.coord.y;
  }
};

}  // namespace

GeometryTile* TileGeometryCache::find(const TileCoord& c) {
  auto it = std::lower_bound(tiles_.begin(), tiles_.end(), c, CoordLess{});
  if (it != tiles_.end() && it->coord == c) return &*it;
  return nullptr;
}

GeometryTile* TileGeometryCache::touch(const TileCoord& c, u64 frame) {
  auto it = std::lower_bound(tiles_.begin(), tiles_.end(), c, CoordLess{});
  if (it != tiles_.end() && it->coord == c) {
    it->frame = frame;
    return &*it;
  }
  GeometryTile fresh;
  fresh.coord = c;
  fresh.frame = frame;
  auto inserted = tiles_.insert(it, std::move(fresh));
  return &*inserted;
}

void TileGeometryCache::endFrameWithEviction(u64 frame) {
  u32 used = 0;
  for (const auto& t : tiles_) if (t.frame == frame) ++used;
  peak_ = std::max(peak_, used);

  // Cast to f64 first to avoid f32 precision loss at large peak values.
  std::size_t cap = static_cast<std::size_t>(static_cast<f64>(peak_) * static_cast<f64>(ratio_));
  cap = std::max<std::size_t>(cap, used);
  if (tiles_.size() <= cap) return;

  // Move tiles with the oldest frame to the back (cap=N → keep the N newest).
  std::size_t keep = cap;
  auto middle = tiles_.begin() + static_cast<std::ptrdiff_t>(keep);
  std::nth_element(tiles_.begin(), middle, tiles_.end(),
                   [](const GeometryTile& a, const GeometryTile& b) {
                     return a.frame > b.frame;  // descending: newest first
                   });
  tiles_.erase(middle, tiles_.end());
  // Re-sort by coord so future lower_bound works.
  std::sort(tiles_.begin(), tiles_.end(),
            [](const GeometryTile& a, const GeometryTile& b) {
              if (a.coord.z != b.coord.z) return a.coord.z < b.coord.z;
              if (a.coord.x != b.coord.x) return a.coord.x < b.coord.x;
              return a.coord.y < b.coord.y;
            });
}

}  // namespace hongtu::gis
