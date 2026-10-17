#pragma once
#include <hongtu/core/common.hpp>
#include <hongtu/gis/tile/tile_coord.hpp>
#include <string>
#include <vector>

namespace hongtu::gis {

/// Decoded DEM tile: float32 heights in meters, row-major, NW origin.
/// u=0 is west edge, u=1 is east edge, v=0 is north edge, v=1 is south edge.
struct DemTile {
  std::vector<f32> heights;  ///< height[row * width + col] in meters
  i32 width  = 0;
  i32 height = 0;

  bool empty() const { return heights.empty(); }

  /// Bilinear sample at normalized UV [0,1] (u=east, v=south).
  f32 sample(f32 u, f32 v) const;
};

/// Abstract interface for DEM tile providers.
class DemProvider {
 public:
  virtual ~DemProvider() = default;
  virtual std::string buildTileUrl(const TileCoord& coord) const = 0;
  /// Decode raw PNG bytes into a DemTile with float heights.
  virtual DemTile decode(const std::vector<u8>& png_bytes) const = 0;
  virtual i32 getMaxLevel() const = 0;
};

}  // namespace hongtu::gis
