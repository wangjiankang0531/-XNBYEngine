#pragma once
#include <hongtu/core/common.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace hongtu::gis {

struct BoundingVolume {
  glm::dvec3 center{0.0};
  // For box: half-size along each axis-aligned dimension
  glm::dvec3 half_extents{0.0};
  // For sphere: radius (center is reused)
  f64 sphere_radius = 0.0;
  bool is_sphere = false;
};

struct TilesetNode {
  BoundingVolume bounding_volume;
  f64 geometric_error = 0.0;
  std::string refine;        ///< "ADD" or "REPLACE"
  std::string content_uri;   ///< relative URI to .b3dm/.i3dm/.pnts file
  glm::dmat4 transform{1.0}; ///< tile-to-parent transform (column-major)
  std::vector<std::unique_ptr<TilesetNode>> children;

  // Runtime streaming state
  bool content_loaded  = false;
  bool content_loading = false;
};

} // namespace hongtu::gis
