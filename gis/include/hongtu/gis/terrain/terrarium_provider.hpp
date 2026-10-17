#pragma once
#include <hongtu/gis/terrain/dem_provider.hpp>

namespace hongtu::gis {

/// AWS Terrain Tiles in Terrarium RGB-encoded format.
/// URL: https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png
/// Formula: height_m = R*256.0 + G + B/256.0 - 32768.0
/// Free, no API key needed. Max zoom level 15.
class TerrariumProvider : public DemProvider {
 public:
  TerrariumProvider() = default;
  std::string buildTileUrl(const TileCoord& coord) const override;
  DemTile     decode(const std::vector<u8>& png_bytes) const override;
  i32         getMaxLevel() const override { return 15; }
};

}  // namespace hongtu::gis
