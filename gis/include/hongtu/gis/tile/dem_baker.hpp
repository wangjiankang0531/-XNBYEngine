#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/gis/terrain/dem_provider.hpp>
#include <hongtu/gis/tile/tile_coord.hpp>
#include <hongtu/gis/tile/tile_geometry_cache.hpp>
#include <functional>

namespace hongtu::gis {

/// Bake DEM heights into the leaf's 33x33 height grid.
///
/// `dem_for` returns the decoded DemTile for a given coord, or nullptr.
/// The baker walks `geom.coord` upward until `dem_for` returns non-null,
/// then bilinearly samples that ancestor DEM into the leaf's heights array.
/// Sets `geom.min_height`, `geom.max_height`, `geom.dirty_height` on success.
///
/// `target_dem_level`, if >= 0, forces the walk to start at AT MOST that
/// level (even if a finer DEM is loaded for this tile's area). This is
/// used to make every visible tile bake from the same DEM zoom level —
/// adjacent tiles then sample edge-consistent heights and the rendered
/// surface joins without elevation cracks. Pass -1 to use the deepest
/// loaded DEM (the previous behavior).
///
/// Returns true on success, false if no ancestor DEM is available.
bool bakeDemIntoGeometry(
    GeometryTile& geom,
    const std::function<const DemTile*(const TileCoord&)>& dem_for,
    i32 target_dem_level = -1);

}  // namespace hongtu::gis
