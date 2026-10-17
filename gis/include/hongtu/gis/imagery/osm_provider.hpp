#pragma once

#include <hongtu/gis/imagery/imagery_provider.hpp>
#include <string>

namespace hongtu::gis {

using namespace hongtu;

/// OpenStreetMap Slippy Map tile provider.
///
/// Fetches tiles from the OpenStreetMap tile server.
/// URL format: https://{a,b,c}.tile.openstreetmap.org/{z}/{x}/{y}.png
/// Free, no API key required. Tiles are 256x256 PNG. Max zoom 19.
/// Round-robins across 3 CDN servers (a/b/c) for load balancing.
class OsmProvider : public ImageryProvider {
 public:
  OsmProvider() = default;

  std::string buildTileUrl(const TileCoord& coord) const override;
  i32 getMinLevel() const override { return 0; }
  i32 getMaxLevel() const override { return 19; }
  i32 getTileSize() const override { return 256; }
  std::string getName() const override { return "OpenStreetMap"; }

 private:
  mutable i32 server_index_ = 0;  ///< Round-robin server index (0-2)
};

}  // namespace hongtu::gis
