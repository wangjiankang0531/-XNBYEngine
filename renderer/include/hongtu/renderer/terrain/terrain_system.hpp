#pragma once

#include <hongtu/core/common.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <unordered_map>

namespace hongtu::renderer {

/// DEM (Digital Elevation Model) data.
struct DEMData {
  std::vector<f32> heights;    ///< Row-major height values
  u32 width = 0, height = 0;  ///< Grid dimensions
  f64 origin_x = 0, origin_y = 0; ///< Geographic origin (bottom-left)
  f64 cell_size_x = 1, cell_size_y = 1; ///< Cell size in geographic units
  f32 min_height = 0, max_height = 0;

  /// Sample height at geographic position (bilinear interpolation).
  f32 sampleHeight(f64 x, f64 y) const;

  /// Get the geographic bounds.
  void getBounds(f64& min_x, f64& min_y, f64& max_x, f64& max_y) const;
};

/// Terrain tile in the LOD quadtree.
struct TerrainTile {
  u32 lod_level;               ///< LOD level (0 = most detailed)
  i32 tile_x, tile_y;          ///< Tile coordinates at this LOD level
  f64 geo_min_x, geo_min_y;   ///< Geographic bounds
  f64 geo_max_x, geo_max_y;
  f32 min_height, max_height;  ///< Height range for culling
  f32 geometric_error;         ///< Error metric for LOD selection
  bool needs_split = false;    ///< Should be subdivided (closer to camera)
  bool has_data = false;
};

/// CDLOD (Chunked Distance-dependent LOD) terrain configuration.
struct TerrainConfig {
  u32 max_lod_levels = 15;       ///< Maximum LOD levels
  u32 tile_resolution = 33;     ///< Vertices per tile edge (must be 2^n + 1)
  f32 lod_distance_ratio = 2.0f;///< Distance multiplier per LOD level
  f32 base_cell_size = 1.0f;    ///< World-space cell size at LOD 0
  f32 height_scale = 1.0f;      ///< Vertical scale multiplier
  f32 max_pixel_error = 4.0f;   ///< Max screen-space error in pixels
};

/// LOD selection result for a terrain tile.
struct TerrainLODSelection {
  u32 lod_level;
  i32 tile_x, tile_y;
  f32 distance_to_camera;
  f32 morph_factor;              ///< LOD transition blend [0,1]
};

/// Terrain LOD system (CDLOD algorithm, CPU side).
class TerrainLODSystem {
 public:
  explicit TerrainLODSystem(const TerrainConfig& config = {});

  /// Select visible tiles for a given camera position.
  /// @param camera_pos   Camera world position.
  /// @param fov_y        Vertical field of view (radians).
  /// @param screen_height Screen height in pixels.
  /// @param terrain_bounds Geographic bounds of the terrain.
  std::vector<TerrainLODSelection> selectTiles(
      const glm::vec3& camera_pos, f32 fov_y, f32 screen_height,
      f64 terrain_min_x, f64 terrain_min_y,
      f64 terrain_max_x, f64 terrain_max_y) const;

  /// Compute geometric error for a given LOD level.
  f32 computeGeometricError(u32 lod_level) const;

  /// Compute the LOD level for a given distance.
  u32 computeLODLevel(f32 distance, f32 fov_y, f32 screen_height) const;

  /// Get the cell size at a given LOD level.
  f32 cellSizeAtLOD(u32 lod) const;

  /// Get config.
  const TerrainConfig& getConfig() const { return config_; }

 private:
  TerrainConfig config_;
};

/// Parse a simple ASCII DEM file (ESRI ASCII Grid format).
/// Format: ncols, nrows, xllcorner, yllcorner, cellsize, NODATA_value, then grid data.
bool loadDEMAscii(const std::string& content, DEMData& dem);

/// Generate a procedural DEM for testing.
DEMData generateTestDEM(u32 width, u32 height, f64 origin_x, f64 origin_y,
                         f64 cell_size, f32 amplitude = 1000.0f);

} // namespace hongtu::renderer
