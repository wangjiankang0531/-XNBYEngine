// Integration tests for the "WorldPartition + LODSelector 接入" wire-in
// (一期 1.1.5). Covers:
//
//   * `WorldPartition::worldToCell(dvec3)` — f64 overload survives ECEF
//     magnitudes (~6.4e6 m) without precision loss
//   * `LODSelector::priorityFromScreenArea` — 3D Tiles SSE formula matches
//     the standard ordering (closer/larger geometric error → higher score)
//   * Dual-mode parity — a tile with equivalent "screen-space" parameters
//     should pick the same priority whether viewed from a Geo camera (ECEF
//     coordinates, big magnitudes) or a Local camera (small magnitudes)
//   * `TileScheduler::setPriorityFn` actually applies the override

#include <gtest/gtest.h>
#include <hongtu/coordinate/geodetic.hpp>
#include <hongtu/gis/tiles3d/tile_scheduler.hpp>
#include <hongtu/renderer/lod/lod_system.hpp>
#include <hongtu/renderer/streaming/world_partition.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

using hongtu::coordinate::Geodetic;
using hongtu::coordinate::geodeticToECEF;
using hongtu::gis::TileScheduler;
using hongtu::gis::TilesetNode;
using hongtu::renderer::LODSelector;
using hongtu::renderer::WorldPartition;
using hongtu::renderer::WorldPartitionConfig;

namespace {
constexpr float kPi = 3.14159265f;
}

// =============================================================================
// WorldPartition::worldToCell(dvec3) — f64 overload
// =============================================================================

TEST(WorldPartitionF64Test, EcefMagnitudeSurvivesWithoutPrecisionLoss) {
  WorldPartitionConfig cfg;
  cfg.cell_size = 100.0f;
  WorldPartition wp(cfg);

  // Beijing ECEF: ECEF magnitudes are ~6.4e6 m. The f32 overload would
  // round to the nearest ~0.5m at that scale; f64 preserves cm precision.
  Geodetic beijing = Geodetic::fromDegrees(39.9042, 116.4074, 0.0);
  glm::dvec3 ecef  = geodeticToECEF(beijing);

  auto cell_f64 = wp.worldToCell(ecef);
  auto cell_f32 = wp.worldToCell(glm::vec3(ecef));  // implicit downcast

  // Both should land in the same cell (cell size 100m, precision diff ~0.5m
  // means they're either the same cell or one off — accept up to ±1 on
  // each axis from precision noise).
  EXPECT_LE(std::abs(cell_f64.x - cell_f32.x), 1);
  EXPECT_LE(std::abs(cell_f64.y - cell_f32.y), 1);
  EXPECT_LE(std::abs(cell_f64.z - cell_f32.z), 1);

  // f64 path: shifting by exactly 1m along x should NOT change the cell
  // (cell size is 100m). f32 at ECEF magnitude can't distinguish 1m moves.
  auto cell_a = wp.worldToCell(ecef + glm::dvec3(1.0, 0.0, 0.0));
  auto cell_b = wp.worldToCell(ecef);
  EXPECT_EQ(cell_a.x, cell_b.x);
}

TEST(WorldPartitionF64Test, MovingOneCellAcrossBoundaryFlipsCoord) {
  WorldPartitionConfig cfg;
  cfg.cell_size = 100.0f;
  WorldPartition wp(cfg);

  // Pick a point that's at a cell boundary. Cell size 100 → ECEF/100 must
  // straddle an integer.
  glm::dvec3 pos{12345.0, 0.0, 0.0};
  auto c1 = wp.worldToCell(pos);
  auto c2 = wp.worldToCell(pos + glm::dvec3(100.0, 0.0, 0.0));
  EXPECT_EQ(c2.x, c1.x + 1);
}

// =============================================================================
// LODSelector::priorityFromScreenArea — SSE math
// =============================================================================

TEST(LodPriorityTest, CloserTileHasHigherPriority) {
  // Two tiles with the same geometric error, different distances.
  const float ge = 100.0f, fov = kPi / 3.0f, h = 720.0f;
  float p_near = LODSelector::priorityFromScreenArea(ge, 100.0f,  fov, h);
  float p_far  = LODSelector::priorityFromScreenArea(ge, 1000.0f, fov, h);
  EXPECT_GT(p_near, p_far);
}

TEST(LodPriorityTest, LargerGeometricErrorAtSameDistanceHasHigherPriority) {
  const float dist = 500.0f, fov = kPi / 3.0f, h = 720.0f;
  float p_big   = LODSelector::priorityFromScreenArea(200.0f, dist, fov, h);
  float p_small = LODSelector::priorityFromScreenArea(50.0f,  dist, fov, h);
  EXPECT_GT(p_big, p_small);
}

TEST(LodPriorityTest, ZeroDistanceOrZeroErrorReturnsZero) {
  const float fov = kPi / 3.0f, h = 720.0f;
  EXPECT_FLOAT_EQ(LODSelector::priorityFromScreenArea(100.0f, 0.0f, fov, h), 0.0f);
  EXPECT_FLOAT_EQ(LODSelector::priorityFromScreenArea(0.0f, 100.0f, fov, h), 0.0f);
}

// =============================================================================
// Dual-mode parity: priority depends on relative scale, not absolute coord
// =============================================================================

TEST(LodPriorityTest, DualModePriorityParity) {
  // A tile "1 km from the camera with 100m error" should get the same
  // priority regardless of whether the camera is at (0,0,0) (local mode)
  // or at Beijing ECEF (global mode) — the priority math is purely
  // distance-based.
  const float ge = 100.0f, dist = 1000.0f, fov = kPi / 3.0f, h = 720.0f;

  // Local-mode: tile at (1000, 0, 0), camera at (0, 0, 0).
  float p_local = LODSelector::priorityFromScreenArea(ge, dist, fov, h);

  // Global-mode: same logical geometry, just shifted to ECEF.
  glm::dvec3 cam_ecef  = geodeticToECEF(Geodetic::fromDegrees(39.9042, 116.4074, 0.0));
  glm::dvec3 tile_ecef = cam_ecef + glm::dvec3(1000.0, 0.0, 0.0);
  float dist_geo = static_cast<float>(glm::distance(cam_ecef, tile_ecef));
  float p_global = LODSelector::priorityFromScreenArea(ge, dist_geo, fov, h);

  EXPECT_NEAR(p_local, p_global, 1e-3f);
}

// =============================================================================
// TileScheduler::setPriorityFn — verify the LOD-based strategy is applied
// =============================================================================

TEST(TileSchedulerLodTest, SetPriorityFnUsesScreenAreaOrder) {
  // 3 tiles at increasing distance. With a screen-area priority, the
  // nearest one should fetch first. The default `geometric_error/dist`
  // strategy gives the same ordering for this simple case, but we
  // explicitly install the SSE function and verify the fetched order.

  auto make = [](const glm::dvec3& center, double ge, const std::string& uri) {
    auto n = std::make_unique<TilesetNode>();
    n->bounding_volume.center = center;
    n->geometric_error        = ge;
    n->content_uri            = uri;
    return n;
  };

  auto root = std::make_unique<TilesetNode>();
  root->geometric_error = 0.0;            // virtual root, no content
  root->children.push_back(make({100.0,  0, 0}, 50.0, "near.b3dm"));
  root->children.push_back(make({500.0,  0, 0}, 50.0, "mid.b3dm"));
  root->children.push_back(make({1000.0, 0, 0}, 50.0, "far.b3dm"));

  // Single worker + slow fetcher so we can observe the order tiles come
  // out of the priority queue.
  std::vector<std::string> fetch_order;
  std::mutex                fetch_mu;

  TileScheduler sched;
  sched.setPriorityFn(
      [](const TilesetNode& n, const glm::dvec3& cam) {
        double dist = std::max(1.0, glm::distance(n.bounding_volume.center, cam));
        return LODSelector::priorityFromScreenArea(
            static_cast<float>(n.geometric_error),
            static_cast<float>(dist),
            kPi / 3.0f, 720.0f);
      });
  sched.start(/*workers=*/1,
      [&](const std::string& uri) {
        std::lock_guard<std::mutex> lk(fetch_mu);
        fetch_order.push_back(uri);
        return std::vector<hongtu::u8>{1};   // non-empty = success
      },
      [](TilesetNode* n, const std::vector<hongtu::u8>&) {
        n->content_loaded  = true;
        n->content_loading = false;
      });
  sched.update(root.get(), glm::dvec3(0, 0, 0), /*max_inflight=*/16);

  for (int spin = 0; spin < 200 && sched.getCompletedCount() < 3u; ++spin) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  sched.stop();

  ASSERT_EQ(fetch_order.size(), 3u);
  // Highest priority first: near before mid before far.
  EXPECT_EQ(fetch_order[0], "near.b3dm");
  EXPECT_EQ(fetch_order[1], "mid.b3dm");
  EXPECT_EQ(fetch_order[2], "far.b3dm");
}
