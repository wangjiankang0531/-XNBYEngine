#include <hongtu/renderer/terrain/terrain_system.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace hongtu::renderer {

// ============================================================================
// DEMData
// ============================================================================

f32 DEMData::sampleHeight(f64 x, f64 y) const {
  if (width == 0 || height == 0 || heights.empty()) return 0.0f;

  // Convert geographic position to grid coordinates
  f64 gx = (x - origin_x) / cell_size_x;
  f64 gy = (y - origin_y) / cell_size_y;

  // Clamp to valid range
  gx = std::max(0.0, std::min(gx, static_cast<f64>(width - 1)));
  gy = std::max(0.0, std::min(gy, static_cast<f64>(height - 1)));

  // Integer indices for the four surrounding samples
  u32 x0 = static_cast<u32>(std::floor(gx));
  u32 y0 = static_cast<u32>(std::floor(gy));
  u32 x1 = std::min(x0 + 1, width - 1);
  u32 y1 = std::min(y0 + 1, height - 1);

  // Fractional part for interpolation
  f32 fx = static_cast<f32>(gx - static_cast<f64>(x0));
  f32 fy = static_cast<f32>(gy - static_cast<f64>(y0));

  // Fetch four samples (row-major: row * width + col)
  f32 h00 = heights[y0 * width + x0];
  f32 h10 = heights[y0 * width + x1];
  f32 h01 = heights[y1 * width + x0];
  f32 h11 = heights[y1 * width + x1];

  // Bilinear interpolation
  f32 h0 = h00 * (1.0f - fx) + h10 * fx;
  f32 h1 = h01 * (1.0f - fx) + h11 * fx;

  return h0 * (1.0f - fy) + h1 * fy;
}

void DEMData::getBounds(f64& min_x, f64& min_y, f64& max_x, f64& max_y) const {
  min_x = origin_x;
  min_y = origin_y;
  max_x = origin_x + static_cast<f64>(width) * cell_size_x;
  max_y = origin_y + static_cast<f64>(height) * cell_size_y;
}

// ============================================================================
// TerrainLODSystem
// ============================================================================

TerrainLODSystem::TerrainLODSystem(const TerrainConfig& config) : config_(config) {}

f32 TerrainLODSystem::cellSizeAtLOD(u32 lod) const {
  return config_.base_cell_size * static_cast<f32>(1u << lod);
}

f32 TerrainLODSystem::computeGeometricError(u32 lod_level) const {
  // Geometric error doubles with each LOD level
  // At LOD 0, the error is the base cell size (finest resolution)
  return config_.base_cell_size * static_cast<f32>(1u << lod_level) * config_.height_scale;
}

u32 TerrainLODSystem::computeLODLevel(f32 distance, f32 fov_y, f32 screen_height) const {
  if (distance <= 0.0f) return 0;

  // Screen-space error metric:
  // pixel_error = (geometric_error / distance) * (screen_height / (2 * tan(fov/2)))
  // We want pixel_error <= max_pixel_error
  // geometric_error <= max_pixel_error * distance * 2 * tan(fov/2) / screen_height
  f32 perspective_factor = 2.0f * std::tan(fov_y * 0.5f) / screen_height;
  f32 max_error = config_.max_pixel_error * distance * perspective_factor;

  // Find the highest LOD (coarsest) where geometric_error <= max_error
  // geometric_error(lod) = base_cell_size * 2^lod * height_scale
  // base_cell_size * 2^lod * height_scale <= max_error
  // 2^lod <= max_error / (base_cell_size * height_scale)
  f32 base_error = config_.base_cell_size * config_.height_scale;
  if (base_error <= 0.0f) return 0;

  f32 max_lod_f = std::log2(max_error / base_error);
  if (max_lod_f < 0.0f) return 0;

  u32 lod = static_cast<u32>(max_lod_f);
  return std::min(lod, config_.max_lod_levels - 1);
}

std::vector<TerrainLODSelection> TerrainLODSystem::selectTiles(
    const glm::vec3& camera_pos, f32 fov_y, f32 screen_height, f64 terrain_min_x,
    f64 terrain_min_y, f64 terrain_max_x, f64 terrain_max_y) const {
  std::vector<TerrainLODSelection> selections;

  // CDLOD: start from coarsest LOD and refine towards the camera
  // For each LOD level, compute the tile grid and check which tiles need
  // to be rendered at this level vs. refined to the next finer level.

  u32 max_lod = config_.max_lod_levels - 1;

  // Process each LOD level from coarsest to finest
  // We use a simple approach: for each LOD level, generate candidate tiles
  // and determine if they should be rendered or split.

  struct TileCandidate {
    u32 lod;
    i32 tx, ty;
    f64 min_x, min_y, max_x, max_y;
  };

  // Start with tiles at the coarsest LOD
  std::vector<TileCandidate> candidates;
  f32 coarse_cell = cellSizeAtLOD(max_lod);
  f64 tile_size = static_cast<f64>(coarse_cell) * static_cast<f64>(config_.tile_resolution - 1);

  i32 start_tx = static_cast<i32>(std::floor(terrain_min_x / tile_size));
  i32 end_tx = static_cast<i32>(std::ceil(terrain_max_x / tile_size));
  i32 start_ty = static_cast<i32>(std::floor(terrain_min_y / tile_size));
  i32 end_ty = static_cast<i32>(std::ceil(terrain_max_y / tile_size));

  for (i32 ty = start_ty; ty < end_ty; ++ty) {
    for (i32 tx = start_tx; tx < end_tx; ++tx) {
      f64 mx = static_cast<f64>(tx) * tile_size;
      f64 my = static_cast<f64>(ty) * tile_size;
      candidates.push_back({max_lod, tx, ty, mx, my, mx + tile_size, my + tile_size});
    }
  }

  // Refine candidates
  while (!candidates.empty()) {
    std::vector<TileCandidate> next_candidates;

    for (const auto& c : candidates) {
      // Compute distance from camera to tile center
      f64 cx = (c.min_x + c.max_x) * 0.5;
      f64 cy = (c.min_y + c.max_y) * 0.5;

      f32 dx = static_cast<f32>(cx) - camera_pos.x;
      f32 dy = static_cast<f32>(cy) - camera_pos.y;
      f32 dz = camera_pos.z;  // Assume terrain at z=0 for distance calc
      f32 distance = std::sqrt(dx * dx + dy * dy + dz * dz);

      u32 desired_lod = computeLODLevel(distance, fov_y, screen_height);

      if (c.lod <= desired_lod || c.lod == 0) {
        // This tile is at the right LOD or finest possible, render it
        TerrainLODSelection sel;
        sel.lod_level = c.lod;
        sel.tile_x = c.tx;
        sel.tile_y = c.ty;
        sel.distance_to_camera = distance;

        // Compute morph factor for smooth LOD transitions
        // morph_factor = 0 means fully this LOD, 1 means ready to transition
        if (c.lod > 0 && desired_lod < c.lod) {
          f32 geo_error_this = computeGeometricError(c.lod);
          f32 geo_error_finer = computeGeometricError(c.lod - 1);
          f32 perspective_factor = 2.0f * std::tan(fov_y * 0.5f) / screen_height;
          f32 pixel_error = geo_error_this / std::max(distance, 0.001f) / perspective_factor;
          f32 pixel_error_finer =
              geo_error_finer / std::max(distance, 0.001f) / perspective_factor;

          if (pixel_error > config_.max_pixel_error) {
            sel.morph_factor = 0.0f;
          } else {
            f32 range = pixel_error - pixel_error_finer;
            if (range > 0.0f) {
              sel.morph_factor =
                  1.0f - (config_.max_pixel_error - pixel_error) / (config_.max_pixel_error * 0.5f);
              sel.morph_factor = std::max(0.0f, std::min(1.0f, sel.morph_factor));
            } else {
              sel.morph_factor = 0.0f;
            }
          }
        } else {
          sel.morph_factor = 0.0f;
        }

        selections.push_back(sel);
      } else {
        // Split into 4 children at the next finer LOD
        u32 child_lod = c.lod - 1;
        f64 mid_x = (c.min_x + c.max_x) * 0.5;
        f64 mid_y = (c.min_y + c.max_y) * 0.5;

        i32 base_tx = c.tx * 2;
        i32 base_ty = c.ty * 2;

        next_candidates.push_back({child_lod, base_tx, base_ty, c.min_x, c.min_y, mid_x, mid_y});
        next_candidates.push_back(
            {child_lod, base_tx + 1, base_ty, mid_x, c.min_y, c.max_x, mid_y});
        next_candidates.push_back(
            {child_lod, base_tx, base_ty + 1, c.min_x, mid_y, mid_x, c.max_y});
        next_candidates.push_back(
            {child_lod, base_tx + 1, base_ty + 1, mid_x, mid_y, c.max_x, c.max_y});
      }
    }

    candidates = std::move(next_candidates);
  }

  return selections;
}

// ============================================================================
// ASCII DEM Parser (ESRI ASCII Grid)
// ============================================================================

bool loadDEMAscii(const std::string& content, DEMData& dem) {
  std::istringstream iss(content);
  std::string key;
  u32 ncols = 0, nrows = 0;
  f64 xllcorner = 0, yllcorner = 0, cellsize = 1;
  f32 nodata_value = -9999.0f;

  // Parse header
  auto readHeader = [&](const std::string& expected) -> bool {
    std::string k;
    if (!(iss >> k)) return false;
    // Case-insensitive compare
    std::string lower_k = k;
    std::transform(lower_k.begin(), lower_k.end(), lower_k.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::string lower_exp = expected;
    std::transform(lower_exp.begin(), lower_exp.end(), lower_exp.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return lower_k == lower_exp;
  };

  if (!readHeader("ncols") || !(iss >> ncols)) return false;
  if (!readHeader("nrows") || !(iss >> nrows)) return false;
  if (!readHeader("xllcorner") || !(iss >> xllcorner)) return false;
  if (!readHeader("yllcorner") || !(iss >> yllcorner)) return false;
  if (!readHeader("cellsize") || !(iss >> cellsize)) return false;

  // NODATA_value is optional
  std::streampos pos = iss.tellg();
  std::string maybe_nodata;
  if (iss >> maybe_nodata) {
    std::string lower = maybe_nodata;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lower == "nodata_value") {
      iss >> nodata_value;
    } else {
      // Not nodata header, rewind
      iss.seekg(pos);
    }
  }

  if (ncols == 0 || nrows == 0) return false;

  dem.width = ncols;
  dem.height = nrows;
  dem.origin_x = xllcorner;
  dem.origin_y = yllcorner;
  dem.cell_size_x = cellsize;
  dem.cell_size_y = cellsize;
  dem.heights.resize(static_cast<size_t>(ncols) * nrows);

  dem.min_height = std::numeric_limits<f32>::max();
  dem.max_height = std::numeric_limits<f32>::lowest();

  // Read grid data (row-major, top-to-bottom in the file but we store bottom-to-top)
  for (u32 row = 0; row < nrows; ++row) {
    // ESRI ASCII grid is top-to-bottom, so row 0 in file = last row in our array
    u32 dest_row = nrows - 1 - row;
    for (u32 col = 0; col < ncols; ++col) {
      f32 val;
      if (!(iss >> val)) return false;

      // Replace NODATA with 0
      if (std::abs(val - nodata_value) < 0.001f) {
        val = 0.0f;
      }

      dem.heights[dest_row * ncols + col] = val;
      dem.min_height = std::min(dem.min_height, val);
      dem.max_height = std::max(dem.max_height, val);
    }
  }

  return true;
}

// ============================================================================
// Test DEM Generator
// ============================================================================

DEMData generateTestDEM(u32 width, u32 height, f64 origin_x, f64 origin_y, f64 cell_size,
                         f32 amplitude) {
  DEMData dem;
  dem.width = width;
  dem.height = height;
  dem.origin_x = origin_x;
  dem.origin_y = origin_y;
  dem.cell_size_x = cell_size;
  dem.cell_size_y = cell_size;
  dem.heights.resize(static_cast<size_t>(width) * height);

  dem.min_height = std::numeric_limits<f32>::max();
  dem.max_height = std::numeric_limits<f32>::lowest();

  constexpr f64 PI = 3.14159265358979323846;

  for (u32 y = 0; y < height; ++y) {
    for (u32 x = 0; x < width; ++x) {
      f64 nx = static_cast<f64>(x) / static_cast<f64>(width);
      f64 ny = static_cast<f64>(y) / static_cast<f64>(height);

      // Multi-frequency sine wave terrain
      f32 h = 0.0f;
      h += static_cast<f32>(std::sin(nx * 2.0 * PI) * std::cos(ny * 2.0 * PI)) * amplitude * 0.5f;
      h += static_cast<f32>(std::sin(nx * 4.0 * PI + 0.5) * std::sin(ny * 3.0 * PI)) *
           amplitude * 0.25f;
      h += static_cast<f32>(std::cos(nx * 7.0 * PI) * std::cos(ny * 5.0 * PI + 1.0)) *
           amplitude * 0.125f;

      // Add a gentle slope
      h += static_cast<f32>(nx * ny) * amplitude * 0.1f;

      // Shift to positive range
      h += amplitude;

      dem.heights[y * width + x] = h;
      dem.min_height = std::min(dem.min_height, h);
      dem.max_height = std::max(dem.max_height, h);
    }
  }

  return dem;
}

}  // namespace hongtu::renderer
