#include <hongtu/gis/imagery/esri_provider.hpp>

#include <sstream>

namespace hongtu::gis {

std::string EsriImageryProvider::buildTileUrl(const TileCoord& coord) const {
  std::ostringstream url;
  url << "https://server.arcgisonline.com/ArcGIS/rest/services/"
         "World_Imagery/MapServer/tile/"
      << coord.z << "/" << coord.y << "/" << coord.x;
  return url.str();
}

}  // namespace hongtu::gis
