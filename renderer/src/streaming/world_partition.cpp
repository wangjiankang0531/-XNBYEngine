#include <hongtu/renderer/streaming/world_partition.hpp>

#include <algorithm>
#include <cmath>

namespace hongtu::renderer {

WorldPartition::WorldPartition(const WorldPartitionConfig& config) : config_(config) {}

CellCoord WorldPartition::worldToCell(const glm::vec3& pos) const {
  return {static_cast<i32>(std::floor(pos.x / config_.cell_size)),
          static_cast<i32>(std::floor(pos.y / config_.cell_size)),
          static_cast<i32>(std::floor(pos.z / config_.cell_size))};
}

CellCoord WorldPartition::worldToCell(const glm::dvec3& pos) const {
  // Do the division in f64 so ECEF magnitudes (~6.4e6 m) don't accumulate
  // precision error before the floor — cell coords are int32 anyway.
  const f64 cs = static_cast<f64>(config_.cell_size);
  return {static_cast<i32>(std::floor(pos.x / cs)),
          static_cast<i32>(std::floor(pos.y / cs)),
          static_cast<i32>(std::floor(pos.z / cs))};
}

glm::vec3 WorldPartition::cellToWorld(const CellCoord& coord) const {
  f32 half = config_.cell_size * 0.5f;
  return {static_cast<f32>(coord.x) * config_.cell_size + half,
          static_cast<f32>(coord.y) * config_.cell_size + half,
          static_cast<f32>(coord.z) * config_.cell_size + half};
}

f32 WorldPartition::cellDistance(const CellCoord& coord, const glm::vec3& camera_pos) const {
  glm::vec3 center = cellToWorld(coord);
  glm::vec3 diff = center - camera_pos;
  return std::sqrt(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
}

void WorldPartition::registerCell(const CellCoord& coord, const std::string& path,
                                  u64 estimated_bytes) {
  PartitionCell& cell = cells_[coord];
  cell.coord = coord;
  cell.content_path = path;
  cell.memory_bytes = estimated_bytes;
  cell.center = cellToWorld(coord);
  cell.radius = config_.cell_size * 0.866f;  // half-diagonal of a cube ~ sqrt(3)/2
  if (cell.state == CellState::Unloaded) {
    cell.state = CellState::Unloaded;
  }
}

CellState WorldPartition::getCellState(const CellCoord& coord) const {
  auto it = cells_.find(coord);
  if (it == cells_.end()) return CellState::Unloaded;
  return it->second.state;
}

std::vector<CellCoord> WorldPartition::getLoadedCells() const {
  std::vector<CellCoord> result;
  for (auto& [coord, cell] : cells_) {
    if (cell.state == CellState::Loaded) {
      result.push_back(coord);
    }
  }
  return result;
}

WorldPartition::UpdateResult WorldPartition::update(const glm::vec3& camera_pos) {
  UpdateResult result{};

  // Collect cells to unload: loaded or loading cells beyond unload radius
  for (auto& [coord, cell] : cells_) {
    f32 dist = cellDistance(coord, camera_pos);

    if (dist > config_.unload_radius) {
      if (cell.state == CellState::Loaded) {
        cell.state = CellState::Unloading;
        result.cells_to_unload.push_back(coord);
      } else if (cell.state == CellState::Loading) {
        // Cancel pending load by reverting to unloaded
        cell.state = CellState::Unloaded;
        if (active_loads_ > 0) active_loads_--;
      }
    }
  }

  // Build priority queue of cells to load: registered cells within load radius
  std::priority_queue<LoadRequest> load_queue;

  for (auto& [coord, cell] : cells_) {
    if (cell.state != CellState::Unloaded) continue;

    f32 dist = cellDistance(coord, camera_pos);
    if (dist <= config_.load_radius) {
      // Check memory budget
      if (used_memory_ + cell.memory_bytes <= config_.memory_budget_bytes) {
        // Priority: inverse distance (closer = higher priority)
        f32 priority = config_.load_radius - dist;
        cell.priority = priority;
        load_queue.push({coord, priority});
      }
    }
  }

  // Schedule loads up to limits
  u32 loads_this_frame = 0;
  while (!load_queue.empty() && loads_this_frame < config_.max_cells_per_frame &&
         active_loads_ < config_.max_concurrent_loads) {
    LoadRequest req = load_queue.top();
    load_queue.pop();

    auto it = cells_.find(req.coord);
    if (it == cells_.end() || it->second.state != CellState::Unloaded) continue;

    // Check memory budget again
    if (used_memory_ + it->second.memory_bytes > config_.memory_budget_bytes) continue;

    it->second.state = CellState::Loading;
    active_loads_++;
    loads_this_frame++;
    result.cells_to_load.push_back(req.coord);
  }

  // Compute summary stats
  result.used_memory = used_memory_;
  for (auto& [coord, cell] : cells_) {
    if (cell.state == CellState::Loaded) result.loaded_count++;
    if (cell.state == CellState::Loading) result.loading_count++;
  }

  return result;
}

void WorldPartition::onCellLoaded(const CellCoord& coord, u64 memory_bytes) {
  auto it = cells_.find(coord);
  if (it == cells_.end()) return;

  if (it->second.state == CellState::Loading) {
    it->second.state = CellState::Loaded;
    it->second.memory_bytes = memory_bytes;
    used_memory_ += memory_bytes;
    if (active_loads_ > 0) active_loads_--;
  }
}

void WorldPartition::onCellUnloaded(const CellCoord& coord) {
  auto it = cells_.find(coord);
  if (it == cells_.end()) return;

  if (it->second.state == CellState::Loaded || it->second.state == CellState::Unloading) {
    if (used_memory_ >= it->second.memory_bytes) {
      used_memory_ -= it->second.memory_bytes;
    } else {
      used_memory_ = 0;
    }
    it->second.state = CellState::Unloaded;
  }
}

}  // namespace hongtu::renderer
