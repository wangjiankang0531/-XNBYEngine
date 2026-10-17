#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace hongtu::renderer {

  /// CPU-side RGBA8 image used by globe renderers and integrated showcase
  /// passes. The loader accepts ordinary stb_image formats and the repository's
  /// historic BC1/DXT1 DDS files even when those files carry a `.jpg` suffix.
  struct EarthTextureImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    std::string source_encoding;

    bool valid() const {
      return width > 0 && height > 0 &&
             rgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    }
  };

  EarthTextureImage loadEarthTextureRgba8(const std::string& path);

  /// A rectangular XYZ/Web-Mercator tile selection assembled into one tightly
  /// packed RGBA8 atlas. Tiles are ordered north-to-south, west-to-east and are
  /// loaded from `<root>/<zoom>/<x>/<y>.{jpg,png,dds}`. The atlas fails closed
  /// when a requested tile is absent or has a different pixel grid, so a
  /// renderer never presents a partially shifted geographic region as valid.
  struct EarthTextureTileAtlas {
    EarthTextureImage image;
    int zoom = -1;
    int minimum_x = 0;
    int minimum_y = 0;
    int columns = 0;
    int rows = 0;
    int tile_width = 0;
    int tile_height = 0;
    int loaded_tiles = 0;
    std::string source_root;

    int expectedTiles() const {
      return columns * rows;
    }
    bool valid() const {
      return image.valid() && loaded_tiles == expectedTiles();
    }
  };

  EarthTextureTileAtlas loadEarthTextureTileAtlasRgba8(
      const std::string& root, int zoom, int minimum_x, int minimum_y, int columns, int rows);

} // namespace hongtu::renderer
