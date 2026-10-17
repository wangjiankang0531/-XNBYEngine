#include <gtest/gtest.h>
#include <hongtu/gis/tile/tile_cache.hpp>

#include <filesystem>

using namespace hongtu;
using namespace hongtu::gis;

class TileCacheTest : public ::testing::Test {
protected:
  void SetUp() override {
    temp_dir_ = std::filesystem::temp_directory_path() / "ht_tile_cache_test";
    std::filesystem::create_directories(temp_dir_);
  }

  void TearDown() override {
    std::filesystem::remove_all(temp_dir_);
  }

  TileData makeTile(i32 width, i32 height, u8 fill = 0xAB) {
    TileData data;
    data.width = width;
    data.height = height;
    data.image_data.assign(static_cast<std::size_t>(width * height * 4), fill);
    return data;
  }

  std::filesystem::path temp_dir_;
};

// ============================================================================
// Basic put/get (memory)
// ============================================================================

TEST_F(TileCacheTest, PutAndGetFromMemory) {
  TileCache cache;
  cache.setCachePath(temp_dir_.string());

  TileCoord coord{5, 10, 20};
  auto tile = makeTile(256, 256, 0xFF);

  cache.put(coord, tile);
  EXPECT_TRUE(cache.contains(coord));

  auto result = cache.get(coord);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->width, 256);
  EXPECT_EQ(result->height, 256);
  EXPECT_EQ(result->image_data.size(), 256u * 256u * 4u);
  EXPECT_EQ(result->image_data[0], 0xFF);
  EXPECT_EQ(result->image_data.back(), 0xFF);
}

// ============================================================================
// Miss returns nullopt
// ============================================================================

TEST_F(TileCacheTest, MissReturnsNullopt) {
  TileCache cache;
  cache.setCachePath(temp_dir_.string());

  TileCoord coord{3, 2, 1};
  auto result = cache.get(coord);
  EXPECT_FALSE(result.has_value());
  EXPECT_FALSE(cache.contains(coord));
}

// ============================================================================
// Memory budget eviction
// ============================================================================

TEST_F(TileCacheTest, MemoryBudgetEviction) {
  TileCache cache;
  cache.setCachePath(temp_dir_.string());

  // Each 16x16 tile = 16*16*4 = 1024 bytes
  // Set budget to hold ~2 tiles
  cache.setMemoryBudget(2048);

  TileCoord c1{0, 0, 0};
  TileCoord c2{1, 0, 0};
  TileCoord c3{1, 1, 0};

  cache.put(c1, makeTile(16, 16, 0x01));
  cache.put(c2, makeTile(16, 16, 0x02));
  EXPECT_TRUE(cache.contains(c1));
  EXPECT_TRUE(cache.contains(c2));
  EXPECT_EQ(cache.getMemoryUsage(), 2048u);

  // Adding a third tile should evict the LRU (c1)
  cache.put(c3, makeTile(16, 16, 0x03));
  EXPECT_FALSE(cache.contains(c1)); // evicted from memory
  EXPECT_TRUE(cache.contains(c3));
  EXPECT_LE(cache.getMemoryUsage(), 2048u);
}

// ============================================================================
// Disk persistence
// ============================================================================

TEST_F(TileCacheTest, DiskPersistence) {
  TileCoord coord{3, 5, 6};

  // Write with one cache instance
  {
    TileCache cache;
    cache.setCachePath(temp_dir_.string());
    cache.put(coord, makeTile(64, 64, 0xCD));
  }

  // Read with a fresh cache instance (empty memory)
  {
    TileCache cache;
    cache.setCachePath(temp_dir_.string());
    EXPECT_FALSE(cache.contains(coord)); // not in memory

    auto result = cache.get(coord);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->width, 64);
    EXPECT_EQ(result->height, 64);
    EXPECT_EQ(result->image_data.size(), 64u * 64u * 4u);
    EXPECT_EQ(result->image_data[0], 0xCD);
    EXPECT_EQ(result->image_data.back(), 0xCD);

    // After get, it should be promoted to memory
    EXPECT_TRUE(cache.contains(coord));
  }
}

TEST_F(TileCacheTest, ProviderNamespacesIsolateMemoryAndDisk) {
  const TileCoord coord{7, 101, 53};

  TileCache cache;
  cache.setCachePath(temp_dir_.string());
  cache.setNamespace("Esri World Imagery");
  cache.put(coord, makeTile(8, 8, 0x11));

  // Changing provider must discard the same-coordinate memory entry and use a
  // different persistent path.
  cache.setNamespace("Tianditu/Imagery");
  EXPECT_FALSE(cache.contains(coord));
  EXPECT_FALSE(cache.get(coord).has_value());
  cache.put(coord, makeTile(8, 8, 0x22));

  // Both providers survive independently across fresh cache instances.
  TileCache esri;
  esri.setCachePath(temp_dir_.string());
  esri.setNamespace("Esri World Imagery");
  auto esri_result = esri.get(coord);
  ASSERT_TRUE(esri_result.has_value());
  EXPECT_EQ(esri_result->image_data.front(), 0x11);

  TileCache tianditu;
  tianditu.setCachePath(temp_dir_.string());
  tianditu.setNamespace("Tianditu/Imagery");
  auto tianditu_result = tianditu.get(coord);
  ASSERT_TRUE(tianditu_result.has_value());
  EXPECT_EQ(tianditu_result->image_data.front(), 0x22);

  // Unsafe characters never escape the cache root; only one provider
  // directory is created beneath the fixed "providers" segment.
  EXPECT_TRUE(std::filesystem::exists(temp_dir_ / "providers"));
  EXPECT_EQ(std::distance(std::filesystem::directory_iterator(temp_dir_ / "providers"),
                          std::filesystem::directory_iterator{}),
            2);
}

// ============================================================================
// Statistics (hit/miss counts)
// ============================================================================

TEST_F(TileCacheTest, HitMissStatistics) {
  TileCache cache;
  cache.setCachePath(temp_dir_.string());

  EXPECT_EQ(cache.getCacheHitCount(), 0u);
  EXPECT_EQ(cache.getCacheMissCount(), 0u);

  TileCoord c1{0, 0, 0};
  TileCoord c2{1, 0, 0};

  cache.put(c1, makeTile(16, 16));

  // Hit: get existing tile from memory
  cache.get(c1);
  EXPECT_EQ(cache.getCacheHitCount(), 1u);
  EXPECT_EQ(cache.getCacheMissCount(), 0u);

  // Miss: get non-existent tile
  cache.get(c2);
  EXPECT_EQ(cache.getCacheHitCount(), 1u);
  EXPECT_EQ(cache.getCacheMissCount(), 1u);

  // Another hit
  cache.get(c1);
  EXPECT_EQ(cache.getCacheHitCount(), 2u);
  EXPECT_EQ(cache.getCacheMissCount(), 1u);
}

// ============================================================================
// Disk hit counts as hit
// ============================================================================

TEST_F(TileCacheTest, DiskHitCountsAsHit) {
  TileCoord coord{2, 1, 1};

  // Write to disk via one instance
  {
    TileCache cache;
    cache.setCachePath(temp_dir_.string());
    cache.put(coord, makeTile(16, 16));
  }

  // Fresh instance - disk read should count as hit
  TileCache cache;
  cache.setCachePath(temp_dir_.string());
  auto result = cache.get(coord);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(cache.getCacheHitCount(), 1u);
  EXPECT_EQ(cache.getCacheMissCount(), 0u);
}

// ============================================================================
// Clear memory
// ============================================================================

TEST_F(TileCacheTest, ClearMemory) {
  TileCache cache;
  cache.setCachePath(temp_dir_.string());

  cache.put({0, 0, 0}, makeTile(16, 16));
  cache.put({1, 0, 0}, makeTile(16, 16));
  EXPECT_GT(cache.getMemoryUsage(), 0u);

  cache.clearMemory();
  EXPECT_EQ(cache.getMemoryUsage(), 0u);
  EXPECT_FALSE(cache.contains({0, 0, 0}));
}

// ============================================================================
// LRU eviction order
// ============================================================================

TEST_F(TileCacheTest, LRUEvictionOrder) {
  TileCache cache;
  cache.setCachePath(temp_dir_.string());
  cache.setMemoryBudget(3072); // room for 3 tiles of 1024 bytes

  TileCoord c1{0, 0, 0};
  TileCoord c2{1, 0, 0};
  TileCoord c3{1, 1, 0};

  cache.put(c1, makeTile(16, 16)); // oldest
  cache.put(c2, makeTile(16, 16));
  cache.put(c3, makeTile(16, 16)); // newest

  // Access c1 to make it most recently used
  cache.get(c1);

  // Now add c4 - should evict c2 (the actual LRU)
  TileCoord c4{1, 0, 1};
  cache.put(c4, makeTile(16, 16));

  EXPECT_TRUE(cache.contains(c1));  // recently accessed
  EXPECT_FALSE(cache.contains(c2)); // LRU, evicted
  EXPECT_TRUE(cache.contains(c3));
  EXPECT_TRUE(cache.contains(c4));
}
