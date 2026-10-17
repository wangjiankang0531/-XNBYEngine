// 3D Tiles integration tests:
//   - TilesetLoader: parse a synthesised tileset.json with a small tree
//   - readB3dmPayload: synthesise a valid b3dm + minimal GLB, extract bytes
//   - TileScheduler: fake fetcher + walk a 3-tile tree, verify all visited
//
// No on-disk fixtures — everything is built inline. Keeps the test
// self-contained and avoids ctest working-directory questions.

#include <gtest/gtest.h>
#include <hongtu/gis/tiles3d/b3dm_reader.hpp>
#include <hongtu/gis/tiles3d/tile_scheduler.hpp>
#include <hongtu/gis/tiles3d/tileset_loader.hpp>

#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

using hongtu::gis::TilesetLoader;
using hongtu::gis::TileScheduler;
using hongtu::gis::TilesetNode;
using hongtu::gis::B3dmPayload;
using hongtu::gis::readB3dmPayload;
using hongtu::gis::readB3dm;
using hongtu::u8;

// =============================================================================
// TilesetLoader
// =============================================================================

TEST(TilesetLoaderTest, ParsesMinimalSphericalRoot) {
  const std::string json = R"({
    "asset": {"version": "1.0"},
    "geometricError": 100.0,
    "root": {
      "boundingVolume": {"sphere": [0, 0, 0, 1000]},
      "geometricError": 50.0,
      "refine": "REPLACE",
      "content": {"uri": "root.b3dm"}
    }
  })";
  auto root = TilesetLoader::parse(json);
  ASSERT_NE(root, nullptr);
  EXPECT_TRUE(root->bounding_volume.is_sphere);
  EXPECT_DOUBLE_EQ(root->bounding_volume.sphere_radius, 1000.0);
  EXPECT_DOUBLE_EQ(root->geometric_error, 50.0);
  EXPECT_EQ(root->refine, "REPLACE");
  EXPECT_EQ(root->content_uri, "root.b3dm");
  EXPECT_TRUE(root->children.empty());
}

TEST(TilesetLoaderTest, ParsesNestedChildren) {
  const std::string json = R"({
    "asset": {"version": "1.0"},
    "root": {
      "boundingVolume": {"sphere": [0, 0, 0, 1000]},
      "geometricError": 100.0,
      "children": [
        {
          "boundingVolume": {"sphere": [10, 0, 0, 500]},
          "geometricError": 50.0,
          "content": {"uri": "a.b3dm"}
        },
        {
          "boundingVolume": {"sphere": [-10, 0, 0, 500]},
          "geometricError": 50.0,
          "content": {"uri": "b.b3dm"}
        }
      ]
    }
  })";
  auto root = TilesetLoader::parse(json);
  ASSERT_NE(root, nullptr);
  ASSERT_EQ(root->children.size(), 2u);
  EXPECT_EQ(root->children[0]->content_uri, "a.b3dm");
  EXPECT_EQ(root->children[1]->content_uri, "b.b3dm");
  EXPECT_TRUE(root->children[0]->content_uri != root->children[1]->content_uri);
}

TEST(TilesetLoaderTest, RejectsMalformedJson) {
  EXPECT_EQ(TilesetLoader::parse("{ not json"), nullptr);
  // Valid JSON but no `root` field.
  EXPECT_EQ(TilesetLoader::parse(R"({"asset":{"version":"1.0"}})"), nullptr);
}

// =============================================================================
// B3dm reader
// =============================================================================

namespace {

// Append `n` bytes from `src` to `out`.
inline void appendBytes(std::vector<u8>& out, const void* src, size_t n) {
  const auto* p = static_cast<const uint8_t*>(src);
  out.insert(out.end(), p, p + n);
}
inline void appendU32(std::vector<u8>& out, uint32_t v) {
  appendBytes(out, &v, sizeof(v));
}

// Build the smallest possible valid GLB-v2 container (no scenes/meshes,
// just the required `asset` object). tinygltf accepts this; b3dm reader
// only validates magic + version + declared length, so the JSON contents
// don't matter to it.
std::vector<u8> makeMinimalGlb() {
  const std::string json = R"({"asset":{"version":"2.0"}})";
  // Pad JSON to 4-byte boundary with spaces — required by GLB spec.
  std::string padded = json;
  while (padded.size() % 4 != 0) padded.push_back(' ');

  std::vector<u8> glb;
  glb.reserve(12 + 8 + padded.size());
  appendU32(glb, 0x46546C67u);                  // magic "glTF"
  appendU32(glb, 2u);                           // version
  appendU32(glb, 12u + 8u + static_cast<uint32_t>(padded.size())); // total length
  appendU32(glb, static_cast<uint32_t>(padded.size()));            // JSON chunk len
  appendU32(glb, 0x4E4F534Au);                  // chunk type "JSON"
  appendBytes(glb, padded.data(), padded.size());
  return glb;
}

// Build a valid b3dm wrapping an empty feature/batch table and `glb`.
std::vector<u8> makeB3dm(const std::vector<u8>& glb) {
  std::vector<u8> b3dm;
  const uint32_t header_size = 28u;
  const uint32_t ft_json = 0u, ft_bin = 0u, bt_json = 0u, bt_bin = 0u;
  const uint32_t total = header_size + ft_json + ft_bin + bt_json + bt_bin
                       + static_cast<uint32_t>(glb.size());
  b3dm.reserve(total);
  appendBytes(b3dm, "b3dm", 4);
  appendU32(b3dm, 1u);                          // version
  appendU32(b3dm, total);                       // byteLength
  appendU32(b3dm, ft_json);
  appendU32(b3dm, ft_bin);
  appendU32(b3dm, bt_json);
  appendU32(b3dm, bt_bin);
  appendBytes(b3dm, glb.data(), glb.size());
  return b3dm;
}

}  // namespace

TEST(B3dmReaderTest, ParsesValidB3dmWithMinimalGlb) {
  auto glb  = makeMinimalGlb();
  auto b3dm = makeB3dm(glb);

  B3dmPayload p = readB3dmPayload(b3dm);
  ASSERT_TRUE(p.valid) << "error: " << p.error;
  EXPECT_EQ(p.glb_offset, 28u);
  EXPECT_EQ(p.glb_bytes.size(), glb.size());
  // Verify the extracted bytes equal the GLB we baked in.
  EXPECT_EQ(0, std::memcmp(p.glb_bytes.data(), glb.data(), glb.size()));
}

TEST(B3dmReaderTest, RejectsTooSmallBuffer) {
  std::vector<u8> tiny(10, 0);
  auto p = readB3dmPayload(tiny);
  EXPECT_FALSE(p.valid);
  EXPECT_FALSE(p.error.empty());
}

TEST(B3dmReaderTest, RejectsBadMagic) {
  auto glb  = makeMinimalGlb();
  auto b3dm = makeB3dm(glb);
  b3dm[0] = 'X';  // corrupt magic
  auto p = readB3dmPayload(b3dm);
  EXPECT_FALSE(p.valid);
}

TEST(B3dmReaderTest, RejectsBadVersion) {
  auto glb  = makeMinimalGlb();
  auto b3dm = makeB3dm(glb);
  b3dm[4] = 2;  // version = 2 (unsupported)
  auto p = readB3dmPayload(b3dm);
  EXPECT_FALSE(p.valid);
}

TEST(B3dmReaderTest, RejectsBadEmbeddedGlbMagic) {
  auto glb  = makeMinimalGlb();
  glb[0] = 'X';  // break glTF magic byte
  auto b3dm = makeB3dm(glb);
  auto p = readB3dmPayload(b3dm);
  EXPECT_FALSE(p.valid);
}

TEST(B3dmReaderTest, LegacyEntrypointReturnsPlaceholderOnSuccess) {
  auto glb  = makeMinimalGlb();
  auto b3dm = makeB3dm(glb);
  auto mesh = readB3dm(b3dm);
  EXPECT_TRUE(mesh.valid);
  EXPECT_FALSE(mesh.vertices.empty());
  EXPECT_FALSE(mesh.indices.empty());
}

// =============================================================================
// TileScheduler
// =============================================================================

TEST(TileSchedulerTest, FetchesAllTilesInTree) {
  // Build a 3-tile tree: root with two children, each has content_uri.
  auto root = std::make_unique<TilesetNode>();
  root->bounding_volume.center = {0, 0, 0};
  root->geometric_error = 100.0;
  root->content_uri = "root.b3dm";

  auto a = std::make_unique<TilesetNode>();
  a->bounding_volume.center = {10, 0, 0};
  a->geometric_error = 50.0;
  a->content_uri = "a.b3dm";

  auto b = std::make_unique<TilesetNode>();
  b->bounding_volume.center = {-10, 0, 0};
  b->geometric_error = 50.0;
  b->content_uri = "b.b3dm";

  root->children.push_back(std::move(a));
  root->children.push_back(std::move(b));

  std::atomic<int> fetched{0};
  TileScheduler sched;
  sched.start(
      /*workers=*/2,
      /*fetcher=*/[&fetched](const std::string& uri) -> std::vector<u8> {
        fetched.fetch_add(1);
        return std::vector<u8>(uri.begin(), uri.end());  // dummy non-empty
      },
      /*sink=*/[](TilesetNode* node, const std::vector<u8>&) {
        node->content_loaded  = true;
        node->content_loading = false;
      });

  sched.update(root.get(), glm::dvec3(0, 0, 100.0), /*max_inflight=*/16);

  // Wait for all 3 fetches to complete (bounded poll, no infinite loop).
  for (int spin = 0; spin < 200 && sched.getCompletedCount() < 3u; ++spin) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  sched.stop();

  EXPECT_EQ(fetched.load(), 3);
  EXPECT_GE(sched.getQueuedCount(),    3u);
  EXPECT_GE(sched.getCompletedCount(), 3u);
  EXPECT_TRUE(root->content_loaded);
  EXPECT_TRUE(root->children[0]->content_loaded);
  EXPECT_TRUE(root->children[1]->content_loaded);
}

TEST(TileSchedulerTest, RespectsMaxInflightBudget) {
  // Build a tree of 5 tiles. With max_inflight=2 the first update only
  // enqueues 2; subsequent updates pick up the rest.
  auto root = std::make_unique<TilesetNode>();
  root->geometric_error = 100.0;
  root->content_uri = "root.b3dm";
  for (int i = 0; i < 4; ++i) {
    auto c = std::make_unique<TilesetNode>();
    c->geometric_error = 50.0;
    c->content_uri = "child_" + std::to_string(i) + ".b3dm";
    root->children.push_back(std::move(c));
  }

  TileScheduler sched;
  // Slow fetcher so we can inspect the queue mid-flight.
  sched.start(1,
      [](const std::string&) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return std::vector<u8>{1};
      },
      [](TilesetNode* node, const std::vector<u8>&) {
        node->content_loaded = true;
        node->content_loading = false;
      });

  sched.update(root.get(), glm::dvec3(0, 0, 0), /*max_inflight=*/2);
  EXPECT_LE(sched.getQueuedCount(), 2u);

  // Drain — loop more updates until everything is in.
  for (int spin = 0; spin < 200 && sched.getCompletedCount() < 5u; ++spin) {
    sched.update(root.get(), glm::dvec3(0, 0, 0), /*max_inflight=*/2);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  sched.stop();
  EXPECT_EQ(sched.getCompletedCount(), 5u);
}

TEST(TileSchedulerTest, FailedFetchClearsLoadingFlag) {
  auto root = std::make_unique<TilesetNode>();
  root->geometric_error = 100.0;
  root->content_uri = "root.b3dm";

  TileScheduler sched;
  sched.start(1,
      [](const std::string&) { return std::vector<u8>{}; },  // empty = failure
      [](TilesetNode* node, const std::vector<u8>&) {
        node->content_loaded = true;
        node->content_loading = false;
      });
  sched.update(root.get(), glm::dvec3(0, 0, 0), 4);
  for (int spin = 0; spin < 100 && sched.getCompletedCount() < 1u; ++spin) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  sched.stop();
  EXPECT_FALSE(root->content_loaded);
  EXPECT_FALSE(root->content_loading);
}
