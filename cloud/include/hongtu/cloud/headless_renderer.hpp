#pragma once

#include <hongtu/core/common.hpp>
#include <glm/glm.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <queue>
#include <mutex>

namespace hongtu::cloud {

/// Render instance identifier.
using InstanceId = u32;

/// Render task configuration.
struct RenderTaskConfig {
  u32 width = 1280;
  u32 height = 720;
  f32 target_fps = 30.0f;
  std::string scene_id;
};

/// Frame output from a render instance.
struct FrameOutput {
  InstanceId instance_id;
  u32 frame_number;
  u32 width, height;
  std::vector<u8> pixel_data;   ///< RGBA8 pixels (can be fed to encoder)
  f64 render_time_ms;
  f64 timestamp;
};

/// GPU resource allocation info.
struct GPUResourceAllocation {
  u32 gpu_index = 0;
  u64 vram_budget_bytes = 0;
  u64 vram_used_bytes = 0;
  f32 gpu_utilization = 0.0f;
};

/// Status of a render instance.
enum class InstanceStatus { Idle, Running, Paused, Error, Stopped };

/// A single headless render instance.
struct RenderInstance {
  InstanceId id;
  RenderTaskConfig config;
  InstanceStatus status = InstanceStatus::Idle;
  u32 frame_count = 0;
  f64 avg_frame_time_ms = 0.0;
  GPUResourceAllocation gpu_alloc;
};

/// Headless renderer: manages multiple render instances without display.
class HeadlessRenderer {
 public:
  HeadlessRenderer() = default;

  /// Create a new render instance. Returns instance ID.
  InstanceId createInstance(const RenderTaskConfig& config);

  /// Start rendering for an instance.
  bool startInstance(InstanceId id);

  /// Stop rendering for an instance.
  bool stopInstance(InstanceId id);

  /// Pause an instance.
  bool pauseInstance(InstanceId id);

  /// Resume a paused instance.
  bool resumeInstance(InstanceId id);

  /// Destroy an instance and free resources.
  bool destroyInstance(InstanceId id);

  /// Get instance info.
  const RenderInstance* getInstance(InstanceId id) const;

  /// Get all instances.
  const std::vector<RenderInstance>& getInstances() const { return instances_; }

  /// Get count of running instances.
  u32 getRunningCount() const;

  /// Simulate a frame tick for all running instances.
  /// In a real implementation this would trigger GPU rendering.
  /// Returns frame outputs ready for encoding.
  std::vector<FrameOutput> tick(f64 delta_time);

  /// Set total GPU memory budget.
  void setGPUBudget(u64 total_vram, u32 gpu_count = 1);

  /// Get total VRAM usage across all instances.
  u64 getTotalVRAMUsage() const;

  /// Max concurrent instances based on current GPU resources.
  u32 getMaxConcurrentInstances() const;

 private:
  InstanceId allocateInstance(const RenderTaskConfig& config);
  void updateResourceAllocation();

  std::vector<RenderInstance> instances_;
  InstanceId next_id_ = 1;
  u64 total_vram_ = 8ULL * 1024 * 1024 * 1024; // 8GB default
  u32 gpu_count_ = 1;
};

} // namespace hongtu::cloud
