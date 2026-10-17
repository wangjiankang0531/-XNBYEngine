#pragma once

#include <hongtu/core/common.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>
#include <memory>

namespace hongtu::gis {

/// 3D Tiles bounding volume types.
struct BoundingRegion {
  f64 west, south, east, north;  ///< Radians
  f64 min_height, max_height;    ///< Meters
};

struct BoundingBox {
  glm::dvec3 center;
  glm::dvec3 half_axes[3];  ///< Three half-axis vectors (oriented box)
};

struct BoundingSphere {
  glm::dvec3 center;
  f64 radius;
};

/// 3D Tiles bounding volume (one of three types).
struct BoundingVolume {
  enum class Type { Region, Box, Sphere, None } type = Type::None;
  BoundingRegion region{};
  BoundingBox box{};
  BoundingSphere sphere{};
};

/// 3D Tiles refinement strategy.
enum class Refinement {
  Add,       ///< Children are added to parent
  Replace,   ///< Children replace parent
};

/// A single tile in the 3D Tiles tileset hierarchy.
struct Tile {
  BoundingVolume bounding_volume;
  f64 geometric_error = 0.0;
  Refinement refine = Refinement::Replace;
  std::string content_uri;           ///< URI to tile content (b3dm, i3dm, pnts, etc.)
  glm::dmat4 transform = glm::dmat4(1.0);
  std::vector<std::unique_ptr<Tile>> children;
};

/// 3D Tiles tileset metadata.
struct TilesetInfo {
  std::string version;             ///< e.g. "1.0" or "1.1"
  f64 geometric_error = 0.0;       ///< Root geometric error
  u32 tile_count = 0;              ///< Total tiles in hierarchy
  u32 max_depth = 0;               ///< Maximum tree depth
  BoundingVolume root_bounding_volume;
};

/// Reads 3D Tiles tileset.json metadata.
/// Does NOT load actual tile content (b3dm, pnts, etc.) - only the hierarchy.
class Tileset3DReader {
 public:
  Tileset3DReader() = default;

  /// Parse a tileset.json string.
  bool parse(const std::string& json_str);

  /// Parse from a JSON object.
  bool parse(const nlohmann::json& j);

  /// Get tileset metadata.
  const TilesetInfo& getInfo() const { return info_; }

  /// Get the root tile.
  const Tile* getRoot() const { return root_.get(); }

  /// Get all content URIs in the tileset (DFS order).
  std::vector<std::string> getAllContentURIs() const;

  /// Get error message.
  const std::string& getError() const { return error_; }

 private:
  std::unique_ptr<Tile> parseTile(const nlohmann::json& j);
  BoundingVolume parseBoundingVolume(const nlohmann::json& j);
  void countTiles(const Tile* tile, u32 depth);

  TilesetInfo info_;
  std::unique_ptr<Tile> root_;
  std::string error_;
};

} // namespace hongtu::gis
