#include <hongtu/renderer/globe/ellipsoid_mesh.hpp>
#include <hongtu/coordinate/geodetic.hpp>
#include <cmath>

using namespace hongtu;
using namespace hongtu::coordinate;
using namespace hongtu::renderer;

EllipsoidMesh::TileMeshData EllipsoidMesh::generateFullGlobe(const Ellipsoid& ellipsoid,
                                                             const RTCOrigin& rtc_origin,
                                                             i32 lon_segments,
                                                             i32 lat_segments) {
  TileMeshData mesh;
  const i32 rows = lat_segments + 1;
  const i32 cols = lon_segments + 1;
  mesh.vertices.reserve(static_cast<size_t>(rows * cols));
  mesh.indices.reserve(static_cast<size_t>(lat_segments * lon_segments * 6));

  constexpr f64 kPI = 3.14159265358979323846;

  for (i32 j = 0; j < rows; ++j) {
    f64 v = static_cast<f64>(j) / static_cast<f64>(lat_segments);
    f64 lat = kPI * 0.5 - v * kPI;  // north pole to south pole

    for (i32 i = 0; i < cols; ++i) {
      f64 u = static_cast<f64>(i) / static_cast<f64>(lon_segments);
      f64 lon = -kPI + u * 2.0 * kPI;  // -PI to +PI

      // Sink the base globe ~5 km below the WGS84 surface so per-tile meshes
      // (rendered at altitude 0 or DEM height) always sit cleanly above it.
      // This eliminates z-fighting between the two layers entirely — they
      // are at geometrically different depths everywhere, no per-pixel
      // depth precision games required.
      Geodetic geo{lat, lon, -5000.0};
      glm::dvec3 ecef = geodeticToECEF(geo, ellipsoid);
      glm::vec3 rtc_pos = rtc_origin.ecefToRTC(ecef);

      // Procedural land/water coloring
      const f64 wave = std::sin(lon * 6.0) * 0.35 + std::cos(lat * 9.0) * 0.35 +
                       std::sin((lat + lon) * 5.0) * 0.3;
      const bool land = wave > -0.08 || std::abs(lat) > 1.1;
      glm::vec3 color = land ? glm::vec3(90.0f / 255.0f, 126.0f / 255.0f, 76.0f / 255.0f)
                             : glm::vec3(26.0f / 255.0f, 76.0f / 255.0f, 130.0f / 255.0f);

      Vertex vertex{};
      vertex.position = rtc_pos;
      vertex.normal = color;
      vertex.uv = glm::vec2(static_cast<f32>(u), static_cast<f32>(v));
      mesh.vertices.push_back(vertex);
    }
  }

  for (i32 j = 0; j < lat_segments; ++j) {
    for (i32 i = 0; i < lon_segments; ++i) {
      u32 top_left = static_cast<u32>(j * cols + i);
      u32 top_right = top_left + 1;
      u32 bottom_left = static_cast<u32>((j + 1) * cols + i);
      u32 bottom_right = bottom_left + 1;

      mesh.indices.push_back(top_left);
      mesh.indices.push_back(bottom_left);
      mesh.indices.push_back(top_right);

      mesh.indices.push_back(top_right);
      mesh.indices.push_back(bottom_left);
      mesh.indices.push_back(bottom_right);
    }
  }

  return mesh;
}
