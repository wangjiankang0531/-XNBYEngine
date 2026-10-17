#pragma once
#include <hongtu/gis/tiles3d/tileset_node.hpp>
#include <memory>
#include <string>

namespace hongtu::gis {

/// Parse a Cesium 3D Tiles tileset.json into a TilesetNode tree.
class TilesetLoader {
 public:
  /// Parse tileset.json text. Returns the root node, or nullptr on parse error.
  static std::unique_ptr<TilesetNode> parse(const std::string& json_text);
};

} // namespace hongtu::gis
