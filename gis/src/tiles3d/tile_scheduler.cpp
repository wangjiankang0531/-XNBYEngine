#include <hongtu/gis/tiles3d/tile_scheduler.hpp>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <chrono>

namespace hongtu::gis {

TileScheduler::~TileScheduler() {
  stop();
}

void TileScheduler::start(uint32_t worker_count, Fetcher fetcher, Sink sink) {
  if (running_.load()) return;
  fetcher_ = std::move(fetcher);
  sink_    = std::move(sink);
  if (worker_count == 0) {
    worker_count = std::max(1u, std::thread::hardware_concurrency());
  }
  running_.store(true);
  workers_.reserve(worker_count);
  for (uint32_t i = 0; i < worker_count; ++i) {
    workers_.emplace_back([this] { workerLoop(); });
  }
  spdlog::debug("TileScheduler: started with {} workers", worker_count);
}

void TileScheduler::stop() {
  {
    std::lock_guard<std::mutex> lk(q_mu_);
    running_.store(false);
    // Drain pending tasks — they are no longer interesting.
    while (!queue_.empty()) queue_.pop();
  }
  q_cv_.notify_all();
  for (auto& t : workers_) if (t.joinable()) t.join();
  workers_.clear();
}

void TileScheduler::workerLoop() {
  while (true) {
    Task task;
    {
      std::unique_lock<std::mutex> lk(q_mu_);
      q_cv_.wait(lk, [this] { return !running_.load() || !queue_.empty(); });
      if (!running_.load() && queue_.empty()) return;
      task = queue_.top();
      queue_.pop();
    }
    if (!task.node) continue;
    std::vector<u8> bytes;
    if (fetcher_) bytes = fetcher_(task.uri);
    if (!bytes.empty() && sink_) {
      sink_(task.node, bytes);
    } else {
      // Failed fetch — release the loading flag so update() can retry later.
      task.node->content_loading = false;
    }
    completed_total_.fetch_add(1u);
  }
}

void TileScheduler::enqueueTileIfNew(TilesetNode* node,
                                      const glm::dvec3& camera,
                                      uint32_t& slot_budget) {
  if (!node || node->content_uri.empty()) return;
  if (node->content_loaded || node->content_loading) return;
  if (slot_budget == 0) return;

  // Priority: caller-supplied function (e.g. SSE via
  // `LODSelector::priorityFromScreenArea`), or default = `geometric_error /
  // distance(camera, node center)`. Larger = closer / visually significant
  // tiles get fetched first.
  double prio;
  if (priority_fn_) {
    prio = priority_fn_(*node, camera);
  } else {
    double dist = std::max(1.0, glm::distance(node->bounding_volume.center,
                                                camera));
    prio = node->geometric_error / dist;
  }

  node->content_loading = true;
  {
    std::lock_guard<std::mutex> lk(q_mu_);
    queue_.push({node, node->content_uri, prio});
  }
  q_cv_.notify_one();
  queued_total_.fetch_add(1u);
  --slot_budget;
}

void TileScheduler::walk(TilesetNode* node,
                          const glm::dvec3& camera,
                          uint32_t& slot_budget) {
  if (!node) return;
  enqueueTileIfNew(node, camera, slot_budget);
  for (const auto& c : node->children) walk(c.get(), camera, slot_budget);
}

void TileScheduler::update(TilesetNode* root,
                            const glm::dvec3& camera_world_f64,
                            uint32_t max_inflight) {
  if (!running_.load() || !root) return;
  uint32_t budget = max_inflight;
  walk(root, camera_world_f64, budget);
}

}  // namespace hongtu::gis
