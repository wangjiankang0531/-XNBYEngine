#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/gis/tile/tile_coord.hpp>

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace hongtu::gis {

  /// Decoded tile image data.
  struct TileData {
    std::vector<u8> image_data; ///< Raw RGBA pixel data
    i32 width = 0;              ///< Image width in pixels
    i32 height = 0;             ///< Image height in pixels
    u64 last_access_time = 0;   ///< Timestamp of last access (for LRU eviction)
  };

  /// Two-level tile cache with in-memory LRU and disk persistence.
  ///
  /// Memory cache uses LRU eviction when the total stored image data exceeds
  /// the configured memory budget. Disk cache stores tiles as binary files
  /// organized by provider namespace/zoom/x/y. An empty namespace preserves the
  /// legacy zoom/x/y layout for standalone users that do not select a provider.
  class TileCache {
  public:
    TileCache();
    ~TileCache() = default;

    // Non-copyable, non-movable (contains mutex)
    TileCache(const TileCache&) = delete;
    TileCache& operator=(const TileCache&) = delete;

    /// Retrieve a tile from cache.
    /// Checks memory first, then disk. Updates access time on hit.
    /// @return The tile data, or nullopt if not found.
    std::optional<TileData> get(const TileCoord& coord);

    /// Store a tile in both memory and disk cache.
    void put(const TileCoord& coord, TileData data);

    /// Check if a tile exists in the memory cache.
    bool contains(const TileCoord& coord) const;

    /// Set the maximum memory budget in bytes (default: 512 MB).
    void setMemoryBudget(std::size_t bytes);

    /// Get the current memory budget in bytes.
    std::size_t getMemoryBudget() const {
      return memory_budget_;
    }

    /// Evict least-recently-used entries from memory until usage is within budget.
    void evict();

    /// Set the directory for disk cache storage.
    void setCachePath(const std::string& path);

    /// Select the provider namespace used by both memory and disk cache keys.
    /// Changing namespace clears the in-memory LRU so identical z/x/y
    /// coordinates can never return bytes from the previous imagery source.
    void setNamespace(std::string cache_namespace);

    /// Provider namespace currently used by the cache.
    const std::string& getNamespace() const {
      return cache_namespace_;
    }

    /// Get current memory usage in bytes.
    std::size_t getMemoryUsage() const {
      return memory_usage_;
    }

    /// Get the number of cache hits.
    std::size_t getCacheHitCount() const {
      return hit_count_;
    }

    /// Get the number of cache misses.
    std::size_t getCacheMissCount() const {
      return miss_count_;
    }

    /// Clear all entries from memory cache.
    void clearMemory();

  private:
    /// Build the disk file path for a tile coordinate.
    std::filesystem::path diskPath(const TileCoord& coord) const;

    /// Write tile data to disk.
    bool writeToDisk(const TileCoord& coord, const TileData& data);

    /// Read tile data from disk.
    std::optional<TileData> readFromDisk(const TileCoord& coord);

    /// Get a monotonically increasing timestamp for LRU ordering.
    u64 nextTimestamp();

    /// Evict one LRU entry (must be called with mutex held).
    void evictOneLocked();

    std::unordered_map<TileCoord, TileData, TileCoord::Hash> memory_cache_;
    std::filesystem::path cache_path_;
    std::string cache_namespace_;
    std::string cache_namespace_directory_;
    std::size_t memory_budget_ = 512 * 1024 * 1024; // 512 MB
    std::size_t memory_usage_ = 0;
    std::size_t hit_count_ = 0;
    std::size_t miss_count_ = 0;
    u64 timestamp_counter_ = 0;
    mutable std::mutex mutex_;
  };

} // namespace hongtu::gis
