#pragma once

#include <hongtu/core/common.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <functional>
#include <string>
#include <mutex>

namespace hongtu::renderer {

/// Cell coordinate in the spatial grid.
struct CellCoord {
  i32 x, y, z;
  bool operator==(const CellCoord& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct CellCoordHash {
  size_t operator()(const CellCoord& c) const {
    size_t h = std::hash<i32>()(c.x);
    h ^= std::hash<i32>()(c.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<i32>()(c.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
  }
};

/// Cell load state.
enum class CellState { Unloaded, Loading, Loaded, Unloading };

/// A single partition cell.
struct PartitionCell {
  CellCoord coord;
  CellState state = CellState::Unloaded;
  f32 priority = 0.0f;          ///< Load priority (higher = more urgent)
  u64 memory_bytes = 0;         ///< Estimated memory usage
  std::string content_path;      ///< Path to cell data
  glm::vec3 center;              ///< World-space center
  f32 radius;                    ///< Bounding radius
};

/// Configuration for the world partition system.
struct WorldPartitionConfig {
  f32 cell_size = 100.0f;        ///< Size of each cell in world units
  f32 load_radius = 500.0f;      ///< Distance to load cells around camera
  f32 unload_radius = 700.0f;    ///< Distance to unload cells (must be > load_radius)
  u32 max_concurrent_loads = 4;  ///< Max simultaneous async loads
  u64 memory_budget_bytes = 4ULL * 1024 * 1024 * 1024; ///< 4GB default
  u32 max_cells_per_frame = 2;   ///< Max cells to start loading per frame
};

/// Async load request.
struct LoadRequest {
  CellCoord coord;
  f32 priority;
  bool operator<(const LoadRequest& o) const { return priority < o.priority; }
};

/// World partition system: spatial grid-based streaming.
class WorldPartition {
 public:
  explicit WorldPartition(const WorldPartitionConfig& config = {});

  /// Update streaming based on camera position.
  /// Returns cells that changed state this frame.
  struct UpdateResult {
    std::vector<CellCoord> cells_to_load;
    std::vector<CellCoord> cells_to_unload;
    u32 loaded_count;
    u32 loading_count;
    u64 used_memory;
  };
  UpdateResult update(const glm::vec3& camera_pos);

  /// Mark a cell as finished loading (called from async loader).
  void onCellLoaded(const CellCoord& coord, u64 memory_bytes);

  /// Mark a cell as finished unloading.
  void onCellUnloaded(const CellCoord& coord);

  /// Register content path for a cell.
  void registerCell(const CellCoord& coord, const std::string& path, u64 estimated_bytes = 0);

  /// Convert world position to cell coordinate.
  CellCoord worldToCell(const glm::vec3& pos) const;

  /// f64 overload — for callers that hold ECEF or other large-magnitude
  /// coordinates. Avoids the float32 precision loss that `glm::vec3` would
  /// introduce when called from an ECEF context (radius ~6.4e6 m).
  CellCoord worldToCell(const glm::dvec3& pos) const;

  /// Convert cell coordinate to world center.
  glm::vec3 cellToWorld(const CellCoord& coord) const;

  /// Get cell state.
  CellState getCellState(const CellCoord& coord) const;

  /// Get all loaded cells.
  std::vector<CellCoord> getLoadedCells() const;

  /// Get current memory usage.
  u64 getUsedMemory() const { return used_memory_; }

  /// Get config.
  const WorldPartitionConfig& getConfig() const { return config_; }

  /// Total registered cells.
  u32 getTotalCells() const { return static_cast<u32>(cells_.size()); }

 private:
  f32 cellDistance(const CellCoord& coord, const glm::vec3& camera_pos) const;

  WorldPartitionConfig config_;
  std::unordered_map<CellCoord, PartitionCell, CellCoordHash> cells_;
  u64 used_memory_ = 0;
  u32 active_loads_ = 0;
};

} // namespace hongtu::renderer
