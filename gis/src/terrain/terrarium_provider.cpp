#include <hongtu/gis/terrain/terrarium_provider.hpp>
#include <stb_image.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace hongtu::gis {

f32 DemTile::sample(f32 u, f32 v) const {
  if (heights.empty() || width <= 0 || height <= 0) return 0.0f;
  // clamp
  u = std::clamp(u, 0.0f, 1.0f);
  v = std::clamp(v, 0.0f, 1.0f);
  f32 px = u * static_cast<f32>(width  - 1);
  f32 py = v * static_cast<f32>(height - 1);
  i32 x0 = static_cast<i32>(px);
  i32 y0 = static_cast<i32>(py);
  i32 x1 = std::min(x0 + 1, width  - 1);
  i32 y1 = std::min(y0 + 1, height - 1);
  f32 fx = px - static_cast<f32>(x0);
  f32 fy = py - static_cast<f32>(y0);
  f32 h00 = heights[static_cast<size_t>(y0 * width + x0)];
  f32 h10 = heights[static_cast<size_t>(y0 * width + x1)];
  f32 h01 = heights[static_cast<size_t>(y1 * width + x0)];
  f32 h11 = heights[static_cast<size_t>(y1 * width + x1)];
  return h00 * (1 - fx) * (1 - fy) + h10 * fx * (1 - fy) + h01 * (1 - fx) * fy + h11 * fx * fy;
}

std::string TerrariumProvider::buildTileUrl(const TileCoord& coord) const {
  std::ostringstream url;
  url << "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/"
      << coord.z << "/" << coord.x << "/" << coord.y << ".png";
  return url.str();
}

DemTile TerrariumProvider::decode(const std::vector<u8>& png_bytes) const {
  DemTile tile;
  if (png_bytes.empty()) return tile;

  int w = 0, h = 0, ch = 0;
  stbi_uc* data = stbi_load_from_memory(
      png_bytes.data(),
      static_cast<int>(png_bytes.size()),
      &w, &h, &ch, 3);  // force 3-channel RGB

  if (!data) {
    spdlog::warn("TerrariumProvider: stbi_load failed: {}", stbi_failure_reason());
    return tile;
  }

  tile.width  = w;
  tile.height = h;
  tile.heights.resize(static_cast<size_t>(w) * static_cast<size_t>(h));

  for (size_t i = 0; i < static_cast<size_t>(w) * static_cast<size_t>(h); ++i) {
    f32 r = static_cast<f32>(data[i * 3 + 0]);
    f32 g = static_cast<f32>(data[i * 3 + 1]);
    f32 b = static_cast<f32>(data[i * 3 + 2]);
    tile.heights[i] = r * 256.0f + g + b / 256.0f - 32768.0f;
  }

  stbi_image_free(data);
  return tile;
}

}  // namespace hongtu::gis
