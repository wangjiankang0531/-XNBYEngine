#include <hongtu/gis/imagery/osm_provider.hpp>

#include <sstream>

namespace hongtu::gis {

std::string OsmProvider::buildTileUrl(const TileCoord& coord) const {
  static const char servers[] = {'a', 'b', 'c'};
  char s = servers[server_index_];
  server_index_ = (server_index_ + 1) % 3;

  std::ostringstream url;
  url << "https://" << s << ".tile.openstreetmap.org/" << coord.z << "/"
      << coord.x << "/" << coord.y << ".png";

  return url.str();
}

}  // namespace hongtu::gis
