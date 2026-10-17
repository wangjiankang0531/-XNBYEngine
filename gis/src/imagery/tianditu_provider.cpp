#include <hongtu/gis/imagery/tianditu_provider.hpp>

#include <sstream>

namespace hongtu::gis {

using namespace hongtu;

TiandituProvider::TiandituProvider(const std::string& token, LayerType type)
    : token_(token), layer_type_(type) {}

std::string TiandituProvider::buildTileUrl(const TileCoord& coord) const {
  const std::string layer = getLayerId();
  i32 server = server_index_;
  server_index_ = (server_index_ + 1) % 8;

  std::ostringstream url;
  url << "https://t" << server << ".tianditu.gov.cn/" << layer
      << "_w/wmts?SERVICE=WMTS&REQUEST=GetTile&VERSION=1.0.0&LAYER=" << layer
      << "&STYLE=default&TILEMATRIXSET=w&FORMAT=tiles"
      << "&TILECOL=" << coord.x << "&TILEROW=" << coord.y << "&TILEMATRIX=" << coord.z
      << "&tk=" << token_;

  return url.str();
}

i32 TiandituProvider::getMinLevel() const { return 1; }

i32 TiandituProvider::getMaxLevel() const { return 18; }

i32 TiandituProvider::getTileSize() const { return 256; }

std::string TiandituProvider::getName() const { return "Tianditu"; }

std::string TiandituProvider::getLayerId() const {
  switch (layer_type_) {
    case LayerType::Imagery:
      return "img";
    case LayerType::ImageryAnnotation:
      return "cia";
    case LayerType::Vector:
      return "vec";
    case LayerType::VectorAnnotation:
      return "cva";
    case LayerType::Terrain:
      return "ter";
    default:
      return "img";
  }
}

}  // namespace hongtu::gis
