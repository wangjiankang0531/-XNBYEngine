#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/coordinate/ellipsoid.hpp>
#include <hongtu/coordinate/rtc_transform.hpp>
#include <glm/glm.hpp>
#include <vector>

namespace hongtu::renderer {

/// Generates mesh patches for individual Web Mercator tiles on a reference
/// ellipsoid. Each tile becomes a grid of quads with vertices in RTC
/// (Relative-To-Center) space for float32 GPU precision.
class EllipsoidMesh {
 public:
  struct Vertex {
    glm::vec3 position;  ///< RTC space (float32 safe)
    glm::vec3 normal;
    glm::vec2 uv;  ///< [0,1] within tile
  };

  struct TileMeshData {
    std::vector<Vertex> vertices;
    std::vector<u32> indices;
  };

  /// Generate a full ellipsoid mesh (entire globe) with procedural land/water coloring.
  /// @param ellipsoid     Reference ellipsoid (e.g. WGS84).
  /// @param rtc_origin    RTC origin for float32 precision.
  /// @param lon_segments  Longitude subdivisions (default 128).
  /// @param lat_segments  Latitude subdivisions (default 64).
  /// @return Mesh data with vertex colors baked into the normal field.
  static TileMeshData generateFullGlobe(const coordinate::Ellipsoid& ellipsoid,
                                         const coordinate::RTCOrigin& rtc_origin,
                                         i32 lon_segments = 128,
                                         i32 lat_segments = 64);
};

}  // namespace hongtu::renderer
