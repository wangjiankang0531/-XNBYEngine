#pragma once

#include <hongtu/gis/imagery/imagery_provider.hpp>
#include <string>

namespace hongtu::gis {

using namespace hongtu;

/// Tianditu (National Platform for Common GeoSpatial Information Services) tile provider.
///
/// Builds WMTS URLs for China's Tianditu satellite imagery, vector, and annotation services.
/// Uses round-robin server selection (t0-t7) for load balancing.
class TiandituProvider : public ImageryProvider {
 public:
  /// Available layer types from the Tianditu WMTS service.
  enum class LayerType {
    Imagery,             ///< Satellite imagery (img)
    ImageryAnnotation,   ///< Imagery annotation overlay (cia)
    Vector,              ///< Vector map tiles (vec)
    VectorAnnotation,    ///< Vector annotation overlay (cva)
    Terrain              ///< Terrain tiles (ter)
  };

  /// Construct a Tianditu provider with the given API token and layer type.
  /// @param token  API access token from tianditu.gov.cn
  /// @param type   Layer type to request (default: Imagery)
  explicit TiandituProvider(const std::string& token, LayerType type = LayerType::Imagery);

  std::string buildTileUrl(const TileCoord& coord) const override;
  i32 getMinLevel() const override;
  i32 getMaxLevel() const override;
  i32 getTileSize() const override;
  std::string getName() const override;

 private:
  /// Get the layer ID string for the current layer type.
  std::string getLayerId() const;

  std::string token_;
  LayerType layer_type_;
  mutable i32 server_index_ = 0;  ///< Round-robin server index (0-7)
};

}  // namespace hongtu::gis
