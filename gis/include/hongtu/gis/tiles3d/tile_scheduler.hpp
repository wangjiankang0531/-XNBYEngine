#pragma once

#include <hongtu/core/common.hpp>
#include <hongtu/gis/tiles3d/tileset_node.hpp>

#include <glm/glm.hpp>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace hongtu::gis {

/// Async prefetch scheduler for 3D Tiles tile content.
///
/// Inputs:
///   - a tileset root (`TilesetNode`)
///   - a camera position (ECEF/world-f64)
///   - a content fetcher (caller-supplied: URI → bytes, blocking)
///   - a content sink   (caller-supplied: URI + bytes → user data; called
///     on a worker thread, must be thread-safe in the caller's code)
///
/// Per `update()` call:
///   1. Walk the tile tree DFS.
///   2. For each tile with a `content_uri`, compute a load priority based on
///      `geometricError / camera_distance` (a coarse screen-space proxy —
///      no projection needed).
///   3. Push the highest-priority not-yet-loaded tile URIs into a worker
///      thread pool that runs the fetcher then the sink.
///   4. Skip tiles already loaded or currently loading.
///
/// Cancellation: `stop()` joins all workers; pending tasks not yet picked
/// up are dropped. Idempotent.
class TileScheduler {
 public:
  /// Fetcher signature: blocking. Implementations: read from disk, or hit
  /// a CDN over HTTP. Empty return = fetch failed.
  using Fetcher = std::function<std::vector<u8>(const std::string&)>;

  /// Sink signature: called on the worker thread after a successful fetch.
  /// `node` is the tileset node whose content was fetched.
  using Sink    = std::function<void(TilesetNode* node,
                                      const std::vector<u8>& bytes)>;

  TileScheduler() = default;
  ~TileScheduler();

  TileScheduler(const TileScheduler&)            = delete;
  TileScheduler& operator=(const TileScheduler&) = delete;

  /// Start the worker pool. `worker_count = 0` means use hardware concurrency.
  /// Must be called before `update()`. Calling twice is a no-op.
  void start(uint32_t worker_count, Fetcher fetcher, Sink sink);

  /// Stop the worker pool. Pending tasks are dropped.
  void stop();

  /// Walk the tileset, score tiles by `geometricError / distance`, and
  /// enqueue up to `max_inflight` not-yet-loaded tiles for fetch.
  ///
  /// Tiles with `content_loaded == true` are skipped. The scheduler sets
  /// `content_loading = true` when it picks a tile up; the sink should
  /// flip the flags to `loaded=true, loading=false` after a successful
  /// fetch (or `loading=false` after a failed one).
  void update(TilesetNode* root,
              const glm::dvec3& camera_world_f64,
              uint32_t max_inflight = 16);

  /// Diagnostics — how many tiles have been queued vs completed since start.
  uint64_t getQueuedCount()    const { return queued_total_.load(); }
  uint64_t getCompletedCount() const { return completed_total_.load(); }

  /// Priority function: given a tile + camera position, return a non-negative
  /// urgency score (higher = fetch sooner). The default uses the simple
  /// `geometric_error / distance` heuristic. Use
  /// `renderer::LODSelector::priorityFromScreenArea` for screen-space-error
  /// based selection (the 3D Tiles standard SSE).
  using PriorityFn = std::function<double(const TilesetNode& node,
                                           const glm::dvec3& camera)>;

  /// Override the default priority strategy. Pass `nullptr` to restore the
  /// default. Must be called before `update()` to take effect.
  void setPriorityFn(PriorityFn fn) { priority_fn_ = std::move(fn); }

 private:
  struct Task {
    TilesetNode* node = nullptr;
    std::string  uri;
    double       priority = 0.0;       ///< higher = fetch sooner
    bool operator<(const Task& o) const { return priority < o.priority; }
  };

  void workerLoop();
  void enqueueTileIfNew(TilesetNode* node,
                         const glm::dvec3& camera,
                         uint32_t& slot_budget);
  void walk(TilesetNode* node,
             const glm::dvec3& camera,
             uint32_t& slot_budget);

  std::vector<std::thread>   workers_;
  std::priority_queue<Task>  queue_;
  std::mutex                 q_mu_;
  std::condition_variable    q_cv_;
  std::atomic<bool>          running_{false};

  Fetcher    fetcher_;
  Sink       sink_;
  PriorityFn priority_fn_;   // null = default geometric_error / distance

  std::atomic<uint64_t> queued_total_{0};
  std::atomic<uint64_t> completed_total_{0};
};

}  // namespace hongtu::gis
