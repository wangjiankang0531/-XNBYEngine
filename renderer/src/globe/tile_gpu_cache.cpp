#include <hongtu/renderer/globe/tile_gpu_cache.hpp>

#include <algorithm>

namespace hongtu::renderer {

  namespace {

    struct CoordLess {
      bool operator()(const GpuTile& a, const gis::TileCoord& b) const {
        if (a.coord.z != b.z)
          return a.coord.z < b.z;
        if (a.coord.x != b.x)
          return a.coord.x < b.x;
        return a.coord.y < b.y;
      }
      bool operator()(const gis::TileCoord& a, const GpuTile& b) const {
        if (a.z != b.coord.z)
          return a.z < b.coord.z;
        if (a.x != b.coord.x)
          return a.x < b.coord.x;
        return a.y < b.coord.y;
      }
    };

  } // namespace

  GpuTile* TileGpuCache::find(const gis::TileCoord& c) {
    auto it = std::lower_bound(tiles_.begin(), tiles_.end(), c, CoordLess{});
    if (it != tiles_.end() && it->coord == c)
      return &*it;
    return nullptr;
  }

  const GpuTile* TileGpuCache::find(const gis::TileCoord& c) const {
    auto it = std::lower_bound(tiles_.begin(), tiles_.end(), c, CoordLess{});
    if (it != tiles_.end() && it->coord == c)
      return &*it;
    return nullptr;
  }

  GpuTile* TileGpuCache::touch(const gis::TileCoord& c, u64 frame) {
    auto it = std::lower_bound(tiles_.begin(), tiles_.end(), c, CoordLess{});
    if (it != tiles_.end() && it->coord == c) {
      it->frame = frame;
      return &*it;
    }
    GpuTile fresh;
    fresh.coord = c;
    fresh.frame = frame;
    auto inserted = tiles_.insert(it, std::move(fresh));
    return &*inserted;
  }

  void TileGpuCache::endFrameWithEviction(u64 frame) {
    u32 used = 0;
    for (const auto& t : tiles_)
      if (t.frame == frame)
        ++used;
    peak_ = std::max(peak_, used);

    std::size_t cap = static_cast<std::size_t>(static_cast<f64>(peak_) * static_cast<f64>(ratio_));
    cap = std::max<std::size_t>(cap, used);
    if (tiles_.size() <= cap)
      return;

    std::nth_element(tiles_.begin(), tiles_.begin() + cap, tiles_.end(), [](const GpuTile& a, const GpuTile& b) {
      return a.frame > b.frame;
    });
    tiles_.erase(tiles_.begin() + static_cast<std::ptrdiff_t>(cap), tiles_.end());
    std::sort(tiles_.begin(), tiles_.end(), [](const GpuTile& a, const GpuTile& b) {
      if (a.coord.z != b.coord.z)
        return a.coord.z < b.coord.z;
      if (a.coord.x != b.coord.x)
        return a.coord.x < b.coord.x;
      return a.coord.y < b.coord.y;
    });
  }

} // namespace hongtu::renderer
