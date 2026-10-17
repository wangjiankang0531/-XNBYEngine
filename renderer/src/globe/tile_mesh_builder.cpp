#include <hongtu/renderer/globe/tile_mesh_builder.hpp>
#include <hongtu/coordinate/geodetic.hpp>

namespace hongtu::renderer {

using namespace hongtu;
using namespace hongtu::coordinate;

void buildTileVertices(const gis::TileCoord& coord,
                       const Ellipsoid& ellipsoid,
                       const RTCOrigin& rtc_origin,
                       const std::array<f32, kTileVertexCount>* heights,
                       std::array<EllipsoidMesh::Vertex, kTileVertexCount>& out) {
  const auto bounds = coord.toBounds();
  constexpr i32 N = kTileGridSize;
  constexpr i32 C = kTileCellsPerSide;

  for (i32 row = 0; row < N; ++row) {
    const f64 v = static_cast<f64>(row) / static_cast<f64>(C);
    const f64 lat = bounds.north - v * (bounds.north - bounds.south);
    for (i32 col = 0; col < N; ++col) {
      const f64 u = static_cast<f64>(col) / static_cast<f64>(C);
      const f64 lon = bounds.west + u * (bounds.east - bounds.west);
      const i32 idx = row * N + col;

      f64 alt = 0.0;
      if (heights) alt = static_cast<f64>((*heights)[idx]);

      const glm::dvec3 ecef = geodeticToECEF(Geodetic{lat, lon, alt}, ellipsoid);
      const glm::vec3 rtc_pos = rtc_origin.ecefToRTC(ecef);
      const glm::vec3 normal = glm::normalize(glm::vec3(ecef));

      EllipsoidMesh::Vertex vert{};
      vert.position = rtc_pos;
      vert.normal = normal;
      vert.uv = glm::vec2(static_cast<f32>(u), static_cast<f32>(v));
      out[idx] = vert;
    }
  }
}

}  // namespace hongtu::renderer
