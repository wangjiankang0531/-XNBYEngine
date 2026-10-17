#pragma once

#include <hongtu/coordinate/ellipsoid.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <hongtu/gis/terrain/dem_provider.hpp>
#include <hongtu/gis/tile/tile_coord.hpp>
#include <hongtu/renderer/globe/ellipsoid_mesh.hpp>  // for Vertex struct re-use
#include <hongtu/renderer/globe/index_buffer_table.hpp>
#include <array>

namespace hongtu::renderer {

/// Generate exactly `kTileVertexCount` vertices (33×33) for a tile coord,
/// in RTC space. Heights come from `heights` (one per vertex) — if a DEM
/// is provided, the caller has already baked it; if not, pass `nullptr`
/// to use sea-level altitude.
///
/// Layout: row-major, north-to-south. Vertex (col, row) → vertices[row*33+col].
void buildTileVertices(const gis::TileCoord& coord,
                       const coordinate::Ellipsoid& ellipsoid,
                       const coordinate::RTCOrigin& rtc_origin,
                       const std::array<f32, kTileVertexCount>* heights,
                       std::array<EllipsoidMesh::Vertex, kTileVertexCount>& out_vertices);

}  // namespace hongtu::renderer
