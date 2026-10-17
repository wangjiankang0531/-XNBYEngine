#pragma once

#include <hongtu/core/common.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <deque>

namespace hongtu::renderer {

/// A single GPU timing record for a render pass.
struct PassTimingRecord {
  std::string name;
  f64 start_ms = 0.0;
  f64 duration_ms = 0.0;
  u32 draw_calls = 0;
  u32 triangles = 0;
};

/// Per-frame statistics.
struct FrameStats {
  u32 frame_number = 0;
  f64 total_gpu_ms = 0.0;
  f64 total_cpu_ms = 0.0;
  f64 frame_time_ms = 0.0;
  u32 total_draw_calls = 0;
  u32 total_triangles = 0;
  u64 gpu_memory_bytes = 0;
  u64 buffer_memory_bytes = 0;
  u64 texture_memory_bytes = 0;
  std::vector<PassTimingRecord> pass_timings;
};

/// Memory usage breakdown by category.
struct MemoryUsage {
  u64 vertex_buffers = 0;
  u64 index_buffers = 0;
  u64 uniform_buffers = 0;
  u64 textures = 0;
  u64 render_targets = 0;
  u64 staging = 0;

  u64 total() const {
    return vertex_buffers + index_buffers + uniform_buffers +
           textures + render_targets + staging;
  }
};

/// Time series data for a named metric (rolling window).
class TimeSeries {
 public:
  explicit TimeSeries(u32 max_samples = 300);

  /// Add a new value.
  void push(f64 value);

  /// Get the most recent value.
  f64 latest() const;

  /// Get average over the window.
  f64 average() const;

  /// Get min/max over the window.
  f64 min() const;
  f64 max() const;

  /// Get all values (for graph rendering).
  const std::deque<f64>& values() const { return values_; }

  /// Get sample count.
  u32 count() const { return static_cast<u32>(values_.size()); }

  /// Clear all data.
  void clear();

 private:
  std::deque<f64> values_;
  u32 max_samples_;
};

/// Render profiler: collects and manages per-frame GPU/CPU timing and resource statistics.
class RenderProfiler {
 public:
  explicit RenderProfiler(u32 history_frames = 300);

  /// Begin a new frame. Call at the start of each frame.
  void beginFrame(u32 frame_number);

  /// End the current frame. Records total frame time.
  void endFrame(f64 frame_time_ms);

  /// Record a render pass timing.
  void recordPass(const std::string& name, f64 duration_ms,
                  u32 draw_calls = 0, u32 triangles = 0);

  /// Update memory usage snapshot.
  void updateMemoryUsage(const MemoryUsage& usage);

  /// Get current frame stats (being built).
  const FrameStats& getCurrentFrame() const { return current_frame_; }

  /// Get the last completed frame stats.
  const FrameStats& getLastFrame() const;

  /// Get frame history.
  const std::deque<FrameStats>& getHistory() const { return frame_history_; }

  /// Get time series for a specific metric.
  const TimeSeries& getFrameTimeSeries() const { return frame_time_series_; }
  const TimeSeries& getGPUTimeSeries() const { return gpu_time_series_; }
  const TimeSeries& getDrawCallSeries() const { return draw_call_series_; }

  /// Get per-pass time series (by name).
  const TimeSeries* getPassTimeSeries(const std::string& name) const;

  /// Get current memory usage.
  const MemoryUsage& getMemoryUsage() const { return memory_usage_; }

  /// Get average FPS over the history window.
  f64 getAverageFPS() const;

  /// Get the 99th percentile frame time.
  f64 getP99FrameTime() const;

  /// Get summary text for display.
  std::string getSummaryText() const;

  /// Reset all data.
  void reset();

 private:
  u32 history_size_;
  FrameStats current_frame_;
  std::deque<FrameStats> frame_history_;
  MemoryUsage memory_usage_;

  TimeSeries frame_time_series_;
  TimeSeries gpu_time_series_;
  TimeSeries draw_call_series_;
  std::unordered_map<std::string, TimeSeries> pass_time_series_;
};

} // namespace hongtu::renderer
