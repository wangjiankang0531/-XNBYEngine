#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/coordinate/ellipsoid.hpp>
#include <hongtu/gis/tile/obb.hpp>
#include <hongtu/gis/tile/tile_coord.hpp>
#include <glm/glm.hpp>
#include <functional>
#include <vector>

namespace hongtu::gis {

/// One selected LOD leaf plus its 4 edge scales (1 = same/finer neighbor,
/// 2 = neighbor one level coarser).
struct SelectedTile {
  TileCoord coord;
  i32 left_scale   = 1;
  i32 right_scale  = 1;
  i32 top_scale    = 1;
  i32 bottom_scale = 1;
};

/// LOD selector using OBB + texels-per-pixel + slopeFactor.
class TileQuadTreeSelector {
 public:
  struct Config {
    i32 min_level = 1;
    i32 max_level = 18;
    /// LOD quality factor. Accept (stop subdividing) when tpp * land_resolution >= 1.0.
    /// Lower values require higher quality (more tiles); higher values allow coarser LOD.
    f32 land_resolution = 1.0f;
    /// Vertical extent margin around the OBB (meters).
    f64 obb_alt_margin = 10.0;
    /// Slope factor cap for distance > tile_length × 4 (DeepEye default 8).
    f32 slope_factor_far = 8.0f;
    /// Slope factor cap for tile_length < distance <= tile_length × 4.
    f32 slope_factor_near = 4.0f;
    /// Hard cap on total selected tiles (safety net; not the primary control).
    std::size_t max_visible_tiles = 1024;
  };

  /// Callback for DEM-driven height bounds. Receives a tile coord, returns
  /// (min, max) ellipsoidal altitudes for that tile. nullptr → flat earth.
  using HeightQuery = std::function<void(const TileCoord&, f64& min_h, f64& max_h)>;

  TileQuadTreeSelector();
  explicit TileQuadTreeSelector(const Config& config);

  std::vector<SelectedTile> select(const glm::dvec3& camera_ecef,
                                    const glm::dmat4& view,
                                    const glm::dmat4& projection,
                                    f32 screen_height,
                                    const HeightQuery* query) const;

  const Config& config() const { return config_; }

 private:
  struct Context {
    glm::dvec3 camera_ecef;
    Frustum frustum;
    f64 tan_fov_y;
    f32 screen_height;
    const HeightQuery* query;
    const coordinate::Ellipsoid* ellipsoid;
  };

  void recurse(const TileCoord& coord, FrustumIntersection parent_state,
               const Context& ctx, std::vector<TileCoord>& out_leaves) const;

  void computeEdgeScales(const std::vector<TileCoord>& leaves,
                          std::vector<SelectedTile>& out) const;

  Config config_;
};

}  // namespace hongtu::gis
