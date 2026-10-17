#pragma once

#include <hongtu/gis/imagery/imagery_provider.hpp>
#include <string>

namespace hongtu::gis {

using namespace hongtu;

/// ArcGIS World Imagery tile provider (remote-sensing / satellite).
///
/// URL format:
///   https://server.arcgisonline.com/ArcGIS/rest/services/
///       World_Imagery/MapServer/tile/{z}/{y}/{x}
/// Note the path order is z/y/x (not the usual z/x/y of OSM-style schemes).
/// Free, no API key. Tiles are 256x256 JPEG. Coverage to ~zoom 19 globally
/// (some regions to z23). Reachable from mainland China.
class EsriImageryProvider : public ImageryProvider {
 public:
  EsriImageryProvider() = default;

  std::string buildTileUrl(const TileCoord& coord) const override;
  i32 getMinLevel() const override { return 0; }
  i32 getMaxLevel() const override { return 19; }
  i32 getTileSize() const override { return 256; }
  std::string getName() const override { return "Esri World Imagery"; }
};

}  // namespace hongtu::gis
