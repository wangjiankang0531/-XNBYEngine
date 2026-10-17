#include <hongtu/renderer/profiler/render_profiler.hpp>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <vector>

namespace hongtu::renderer {

// ============================================================================
// TimeSeries
// ============================================================================

TimeSeries::TimeSeries(u32 max_samples) : max_samples_(max_samples) {}

void TimeSeries::push(f64 value) {
  values_.push_back(value);
  while (values_.size() > max_samples_) {
    values_.pop_front();
  }
}

f64 TimeSeries::latest() const {
  return values_.empty() ? 0.0 : values_.back();
}

f64 TimeSeries::average() const {
  if (values_.empty()) return 0.0;
  f64 sum = 0.0;
  for (f64 v : values_) sum += v;
  return sum / static_cast<f64>(values_.size());
}

f64 TimeSeries::min() const {
  if (values_.empty()) return 0.0;
  return *std::min_element(values_.begin(), values_.end());
}

f64 TimeSeries::max() const {
  if (values_.empty()) return 0.0;
  return *std::max_element(values_.begin(), values_.end());
}

void TimeSeries::clear() {
  values_.clear();
}

// ============================================================================
// RenderProfiler
// ============================================================================

RenderProfiler::RenderProfiler(u32 history_frames)
    : history_size_(history_frames),
      frame_time_series_(history_frames),
      gpu_time_series_(history_frames),
      draw_call_series_(history_frames) {}

void RenderProfiler::beginFrame(u32 frame_number) {
  current_frame_ = {};
  current_frame_.frame_number = frame_number;
}

void RenderProfiler::endFrame(f64 frame_time_ms) {
  current_frame_.frame_time_ms = frame_time_ms;

  // Sum up pass timings
  f64 gpu_total = 0.0;
  u32 dc_total = 0;
  u32 tri_total = 0;
  for (const auto& pass : current_frame_.pass_timings) {
    gpu_total += pass.duration_ms;
    dc_total += pass.draw_calls;
    tri_total += pass.triangles;
  }
  current_frame_.total_gpu_ms = gpu_total;
  current_frame_.total_draw_calls = dc_total;
  current_frame_.total_triangles = tri_total;
  current_frame_.gpu_memory_bytes = memory_usage_.total();

  // Update time series
  frame_time_series_.push(frame_time_ms);
  gpu_time_series_.push(gpu_total);
  draw_call_series_.push(static_cast<f64>(dc_total));

  // Update per-pass time series
  for (const auto& pass : current_frame_.pass_timings) {
    if (pass_time_series_.find(pass.name) == pass_time_series_.end()) {
      pass_time_series_.emplace(pass.name, TimeSeries(history_size_));
    }
    pass_time_series_[pass.name].push(pass.duration_ms);
  }

  // Store in history
  frame_history_.push_back(current_frame_);
  while (frame_history_.size() > history_size_) {
    frame_history_.pop_front();
  }
}

void RenderProfiler::recordPass(const std::string& name, f64 duration_ms,
                                 u32 draw_calls, u32 triangles) {
  current_frame_.pass_timings.push_back({name, 0.0, duration_ms, draw_calls, triangles});
}

void RenderProfiler::updateMemoryUsage(const MemoryUsage& usage) {
  memory_usage_ = usage;
}

const FrameStats& RenderProfiler::getLastFrame() const {
  static FrameStats empty;
  return frame_history_.empty() ? empty : frame_history_.back();
}

const TimeSeries* RenderProfiler::getPassTimeSeries(const std::string& name) const {
  auto it = pass_time_series_.find(name);
  return (it != pass_time_series_.end()) ? &it->second : nullptr;
}

f64 RenderProfiler::getAverageFPS() const {
  f64 avg_ms = frame_time_series_.average();
  return (avg_ms > 0.0) ? 1000.0 / avg_ms : 0.0;
}

f64 RenderProfiler::getP99FrameTime() const {
  auto& vals = frame_time_series_.values();
  if (vals.empty()) return 0.0;

  std::vector<f64> sorted(vals.begin(), vals.end());
  std::sort(sorted.begin(), sorted.end());
  u32 p99_idx = static_cast<u32>(sorted.size() * 99 / 100);
  return sorted[std::min(p99_idx, static_cast<u32>(sorted.size() - 1))];
}

std::string RenderProfiler::getSummaryText() const {
  std::ostringstream ss;
  ss.setf(std::ios::fixed);
  ss.precision(2);
  ss << "[profiler] FPS=" << static_cast<int>(getAverageFPS())
     << " frame=" << frame_time_series_.average() << "ms (avg)"
     << " gpu=" << gpu_time_series_.average() << "ms"
     << " p99=" << getP99FrameTime() << "ms"
     << " DC=" << static_cast<int>(draw_call_series_.average())
     << " VRAM=" << (memory_usage_.total() / (1024 * 1024)) << "MB";
  if (!pass_time_series_.empty()) {
    ss << "\n          passes:";
    // Deterministic order helps when diffing logs across runs.
    std::vector<std::string> names;
    names.reserve(pass_time_series_.size());
    for (const auto& kv : pass_time_series_) names.push_back(kv.first);
    std::sort(names.begin(), names.end());
    for (const auto& n : names) {
      ss << " " << n << "=" << pass_time_series_.at(n).average() << "ms";
    }
  }
  return ss.str();
}

void RenderProfiler::reset() {
  current_frame_ = {};
  frame_history_.clear();
  memory_usage_ = {};
  frame_time_series_.clear();
  gpu_time_series_.clear();
  draw_call_series_.clear();
  pass_time_series_.clear();
}

} // namespace hongtu::renderer
