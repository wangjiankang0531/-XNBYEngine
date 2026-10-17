#include <hongtu/renderer/globe/earth_texture_loader.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace hongtu::renderer {
  namespace {

    void decodeDxt1Block(const std::uint8_t* block, std::uint8_t out[64]) {
      const auto u16 = [](std::uint8_t lo, std::uint8_t hi) -> std::uint16_t {
        return static_cast<std::uint16_t>(lo) | static_cast<std::uint16_t>(static_cast<std::uint16_t>(hi) << 8u);
      };
      const std::uint16_t c0 = u16(block[0], block[1]);
      const std::uint16_t c1 = u16(block[2], block[3]);
      const std::uint32_t bits = static_cast<std::uint32_t>(block[4]) | (static_cast<std::uint32_t>(block[5]) << 8u) |
                                 (static_cast<std::uint32_t>(block[6]) << 16u) |
                                 (static_cast<std::uint32_t>(block[7]) << 24u);

      std::uint8_t colors[4][3];
      const auto expand = [](std::uint16_t color, std::uint8_t rgb[3]) {
        rgb[0] = static_cast<std::uint8_t>(((color >> 11u) & 0x1fu) * 255u / 31u);
        rgb[1] = static_cast<std::uint8_t>(((color >> 5u) & 0x3fu) * 255u / 63u);
        rgb[2] = static_cast<std::uint8_t>((color & 0x1fu) * 255u / 31u);
      };
      expand(c0, colors[0]);
      expand(c1, colors[1]);

      if (c0 > c1) {
        for (int channel = 0; channel < 3; ++channel) {
          colors[2][channel] = static_cast<std::uint8_t>((2u * colors[0][channel] + colors[1][channel]) / 3u);
          colors[3][channel] = static_cast<std::uint8_t>((colors[0][channel] + 2u * colors[1][channel]) / 3u);
        }
      } else {
        for (int channel = 0; channel < 3; ++channel) {
          colors[2][channel] = static_cast<std::uint8_t>((colors[0][channel] + colors[1][channel]) / 2u);
          colors[3][channel] = 0;
        }
      }

      for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
          const int pixel_index = y * 4 + x;
          const int color_index = static_cast<int>((bits >> (pixel_index * 2)) & 0x3u);
          std::uint8_t* pixel = out + pixel_index * 4;
          pixel[0] = colors[color_index][0];
          pixel[1] = colors[color_index][1];
          pixel[2] = colors[color_index][2];
          pixel[3] = c0 <= c1 && color_index == 3 ? 0 : 255;
        }
      }
    }

    EarthTextureImage loadDxt1Dds(const std::string& path) {
      constexpr std::uint32_t kDdsMagic = 0x20534444u;
      constexpr std::uint32_t kFourCcDxt1 = 0x31545844u;

      std::ifstream stream(path, std::ios::binary | std::ios::ate);
      if (!stream.is_open())
        return {};
      const auto end = stream.tellg();
      if (end < static_cast<std::streamoff>(128))
        return {};
      const std::size_t file_size = static_cast<std::size_t>(end);
      stream.seekg(0);
      std::vector<std::uint8_t> bytes(file_size);
      if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(file_size)))
        return {};

      std::uint32_t magic = 0;
      std::uint32_t width = 0;
      std::uint32_t height = 0;
      std::uint32_t four_cc = 0;
      std::memcpy(&magic, bytes.data(), 4);
      std::memcpy(&height, bytes.data() + 12, 4);
      std::memcpy(&width, bytes.data() + 16, 4);
      std::memcpy(&four_cc, bytes.data() + 84, 4);
      if (magic != kDdsMagic || four_cc != kFourCcDxt1 || width == 0 || height == 0)
        return {};

      const std::size_t blocks_x = (width + 3u) / 4u;
      const std::size_t blocks_y = (height + 3u) / 4u;
      const std::size_t expected_bytes = blocks_x * blocks_y * 8u;
      if (file_size < 128u + expected_bytes)
        return {};

      EarthTextureImage image;
      image.width = static_cast<int>(width);
      image.height = static_cast<int>(height);
      image.rgba.assign(static_cast<std::size_t>(width) * height * 4u, 255u);
      image.source_encoding = "dds_bc1_dxt1";
      const std::uint8_t* source = bytes.data() + 128u;
      for (std::size_t block_y = 0; block_y < blocks_y; ++block_y) {
        for (std::size_t block_x = 0; block_x < blocks_x; ++block_x) {
          std::uint8_t decoded[64];
          decodeDxt1Block(source, decoded);
          source += 8;
          for (int y = 0; y < 4; ++y) {
            const std::size_t row = block_y * 4u + static_cast<std::size_t>(y);
            if (row >= height)
              continue;
            for (int x = 0; x < 4; ++x) {
              const std::size_t column = block_x * 4u + static_cast<std::size_t>(x);
              if (column >= width)
                continue;
              std::memcpy(&image.rgba[(row * width + column) * 4u], decoded + (y * 4 + x) * 4, 4);
            }
          }
        }
      }
      return image;
    }

  } // namespace

  EarthTextureImage loadEarthTextureRgba8(const std::string& path) {
    int width = 0;
    int height = 0;
    int components = 0;
    if (unsigned char* pixels = stbi_load(path.c_str(), &width, &height, &components, 4)) {
      EarthTextureImage image;
      image.width = width;
      image.height = height;
      image.rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4u);
      image.source_encoding = "stb_rgba8";
      stbi_image_free(pixels);
      return image;
    }
    return loadDxt1Dds(path);
  }

  EarthTextureTileAtlas loadEarthTextureTileAtlasRgba8(
      const std::string& root, int zoom, int minimum_x, int minimum_y, int columns, int rows) {
    EarthTextureTileAtlas atlas;
    atlas.zoom = zoom;
    atlas.minimum_x = minimum_x;
    atlas.minimum_y = minimum_y;
    atlas.columns = columns;
    atlas.rows = rows;
    atlas.source_root = root;
    if (zoom < 0 || minimum_x < 0 || minimum_y < 0 || columns <= 0 || rows <= 0)
      return atlas;

    const int tiles_per_axis = zoom < 30 ? (1 << zoom) : 0;
    if (tiles_per_axis == 0 || minimum_x + columns > tiles_per_axis || minimum_y + rows > tiles_per_axis)
      return atlas;

    namespace fs = std::filesystem;
    std::string common_encoding;
    for (int row = 0; row < rows; ++row) {
      for (int column = 0; column < columns; ++column) {
        const fs::path stem = fs::path(root) / std::to_string(zoom) / std::to_string(minimum_x + column) /
                              std::to_string(minimum_y + row);
        EarthTextureImage tile;
        for (const char* extension : {".jpg", ".png", ".dds"}) {
          tile = loadEarthTextureRgba8((stem.string() + extension));
          if (tile.valid())
            break;
        }
        if (!tile.valid()) {
          atlas.image = {};
          return atlas;
        }
        if (atlas.loaded_tiles == 0) {
          atlas.tile_width = tile.width;
          atlas.tile_height = tile.height;
          atlas.image.width = tile.width * columns;
          atlas.image.height = tile.height * rows;
          atlas.image.rgba.assign(static_cast<std::size_t>(atlas.image.width) * atlas.image.height * 4u, 0u);
          common_encoding = tile.source_encoding;
        } else if (tile.width != atlas.tile_width || tile.height != atlas.tile_height) {
          atlas.image = {};
          return atlas;
        } else if (tile.source_encoding != common_encoding) {
          common_encoding = "mixed_rgba8";
        }

        for (int source_y = 0; source_y < atlas.tile_height; ++source_y) {
          const std::size_t source_offset = static_cast<std::size_t>(source_y) * atlas.tile_width * 4u;
          const std::size_t destination_y = static_cast<std::size_t>(row * atlas.tile_height + source_y);
          const std::size_t destination_x = static_cast<std::size_t>(column * atlas.tile_width);
          const std::size_t destination_offset =
              (destination_y * static_cast<std::size_t>(atlas.image.width) + destination_x) * 4u;
          std::copy_n(tile.rgba.data() + source_offset,
                      static_cast<std::size_t>(atlas.tile_width) * 4u,
                      atlas.image.rgba.data() + destination_offset);
        }
        ++atlas.loaded_tiles;
      }
    }
    atlas.image.source_encoding = "xyz_tile_atlas_" + common_encoding;
    return atlas;
  }

} // namespace hongtu::renderer
