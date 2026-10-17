#include <hongtu/gis/tile/tile_cache.hpp>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace hongtu::gis {

  using namespace hongtu;

  TileCache::TileCache() = default;

  std::optional<TileData> TileCache::get(const TileCoord& coord) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check memory cache first
    auto it = memory_cache_.find(coord);
    if (it != memory_cache_.end()) {
      it->second.last_access_time = nextTimestamp();
      ++hit_count_;
      return it->second;
    }

    // Try disk cache
    auto disk_data = readFromDisk(coord);
    if (disk_data.has_value()) {
      ++hit_count_;
      // Promote to memory cache
      disk_data->last_access_time = nextTimestamp();
      std::size_t data_size = disk_data->image_data.size();
      memory_cache_[coord] = *disk_data;
      memory_usage_ += data_size;
      // Evict if over budget (without holding lock again - internal call)
      while (memory_usage_ > memory_budget_ && !memory_cache_.empty()) {
        evictOneLocked();
      }
      return disk_data;
    }

    ++miss_count_;
    return std::nullopt;
  }

  void TileCache::put(const TileCoord& coord, TileData data) {
    std::lock_guard<std::mutex> lock(mutex_);

    data.last_access_time = nextTimestamp();
    std::size_t data_size = data.image_data.size();

    // Remove old entry if it exists
    auto it = memory_cache_.find(coord);
    if (it != memory_cache_.end()) {
      memory_usage_ -= it->second.image_data.size();
      it->second = std::move(data);
      memory_usage_ += data_size;
    } else {
      memory_cache_[coord] = std::move(data);
      memory_usage_ += data_size;
    }

    // Write to disk
    writeToDisk(coord, memory_cache_[coord]);

    // Evict if over budget
    while (memory_usage_ > memory_budget_ && memory_cache_.size() > 1) {
      evictOneLocked();
    }
  }

  bool TileCache::contains(const TileCoord& coord) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return memory_cache_.find(coord) != memory_cache_.end();
  }

  void TileCache::setMemoryBudget(std::size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    memory_budget_ = bytes;
    while (memory_usage_ > memory_budget_ && !memory_cache_.empty()) {
      evictOneLocked();
    }
  }

  void TileCache::evict() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (memory_usage_ > memory_budget_ && !memory_cache_.empty()) {
      evictOneLocked();
    }
  }

  void TileCache::setCachePath(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_path_ = path;
  }

  void TileCache::setNamespace(std::string cache_namespace) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (cache_namespace_ == cache_namespace) {
      return;
    }

    cache_namespace_ = std::move(cache_namespace);
    cache_namespace_directory_.clear();
    if (!cache_namespace_.empty()) {
      std::string readable;
      readable.reserve(std::min<std::size_t>(cache_namespace_.size(), 48));
      bool previous_dash = false;
      for (const unsigned char c : cache_namespace_) {
        const bool safe = std::isalnum(c) != 0 || c == '.' || c == '_' || c == '-';
        const char next = safe ? static_cast<char>(c) : '-';
        if (next == '-' && previous_dash) {
          continue;
        }
        readable.push_back(next);
        previous_dash = next == '-';
        if (readable.size() == 48) {
          break;
        }
      }
      if (readable.empty()) {
        readable = "provider";
      }

      std::uint64_t hash = 14695981039346656037ull;
      for (const unsigned char c : cache_namespace_) {
        hash ^= c;
        hash *= 1099511628211ull;
      }
      std::ostringstream directory;
      directory << readable << '-' << std::hex << std::setfill('0') << std::setw(16) << hash;
      cache_namespace_directory_ = directory.str();
    }

    memory_cache_.clear();
    memory_usage_ = 0;
  }

  void TileCache::clearMemory() {
    std::lock_guard<std::mutex> lock(mutex_);
    memory_cache_.clear();
    memory_usage_ = 0;
  }

  std::filesystem::path TileCache::diskPath(const TileCoord& coord) const {
    if (cache_path_.empty()) {
      return {};
    }
    std::filesystem::path root = cache_path_;
    if (!cache_namespace_directory_.empty()) {
      root /= "providers";
      root /= cache_namespace_directory_;
    }
    return root / std::to_string(coord.z) / std::to_string(coord.x) / (std::to_string(coord.y) + ".tile");
  }

  bool TileCache::writeToDisk(const TileCoord& coord, const TileData& data) {
    auto path = diskPath(coord);
    if (path.empty()) {
      return false;
    }

    std::filesystem::create_directories(path.parent_path());

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
      return false;
    }

    // Binary format: width(i32) + height(i32) + size(u64) + image_data(bytes)
    u64 size = data.image_data.size();
    file.write(reinterpret_cast<const char*>(&data.width), sizeof(i32));
    file.write(reinterpret_cast<const char*>(&data.height), sizeof(i32));
    file.write(reinterpret_cast<const char*>(&size), sizeof(u64));
    file.write(reinterpret_cast<const char*>(data.image_data.data()), static_cast<std::streamsize>(size));

    return file.good();
  }

  std::optional<TileData> TileCache::readFromDisk(const TileCoord& coord) {
    auto path = diskPath(coord);
    if (path.empty() || !std::filesystem::exists(path)) {
      return std::nullopt;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
      return std::nullopt;
    }

    TileData data;
    u64 size = 0;

    file.read(reinterpret_cast<char*>(&data.width), sizeof(i32));
    file.read(reinterpret_cast<char*>(&data.height), sizeof(i32));
    file.read(reinterpret_cast<char*>(&size), sizeof(u64));

    if (!file.good() || size == 0) {
      return std::nullopt;
    }

    data.image_data.resize(size);
    file.read(reinterpret_cast<char*>(data.image_data.data()), static_cast<std::streamsize>(size));

    if (!file.good()) {
      return std::nullopt;
    }

    return data;
  }

  u64 TileCache::nextTimestamp() {
    return ++timestamp_counter_;
  }

  void TileCache::evictOneLocked() {
    if (memory_cache_.empty()) {
      return;
    }

    // Find the entry with the smallest last_access_time (LRU)
    auto lru_it = memory_cache_.begin();
    for (auto it = memory_cache_.begin(); it != memory_cache_.end(); ++it) {
      if (it->second.last_access_time < lru_it->second.last_access_time) {
        lru_it = it;
      }
    }

    memory_usage_ -= lru_it->second.image_data.size();
    memory_cache_.erase(lru_it);
  }

} // namespace hongtu::gis
