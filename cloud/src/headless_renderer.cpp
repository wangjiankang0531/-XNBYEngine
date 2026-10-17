#include <hongtu/cloud/headless_renderer.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace hongtu::cloud {

InstanceId HeadlessRenderer::allocateInstance(const RenderTaskConfig& config) {
  RenderInstance inst;
  inst.id = next_id_++;
  inst.config = config;
  inst.status = InstanceStatus::Idle;
  inst.frame_count = 0;
  inst.avg_frame_time_ms = 0.0;
  instances_.push_back(inst);
  updateResourceAllocation();
  return inst.id;
}

void HeadlessRenderer::updateResourceAllocation() {
  if (instances_.empty()) return;

  u64 per_gpu_vram = total_vram_ / gpu_count_;

  // Estimate VRAM per instance based on resolution
  // Base: framebuffer + depth + staging = width * height * (4+4+4) bytes * 2 (double buffer)
  u32 running = 0;
  for (auto& inst : instances_) {
    if (inst.status == InstanceStatus::Running || inst.status == InstanceStatus::Paused) {
      running++;
    }
  }

  u32 gpu_idx = 0;
  u32 per_gpu_count = 0;
  u32 max_per_gpu = running > 0 ? (running + gpu_count_ - 1) / gpu_count_ : 1;

  for (auto& inst : instances_) {
    u64 fb_bytes = static_cast<u64>(inst.config.width) * inst.config.height * 4 * 2;  // RGBA * double buffer
    u64 depth_bytes = static_cast<u64>(inst.config.width) * inst.config.height * 4;    // depth32
    u64 staging_bytes = fb_bytes / 2;
    u64 estimated_vram = fb_bytes + depth_bytes + staging_bytes;
    // Add overhead for textures, buffers, etc.
    estimated_vram += 64 * 1024 * 1024;  // 64MB base overhead

    inst.gpu_alloc.gpu_index = gpu_idx;
    inst.gpu_alloc.vram_budget_bytes = per_gpu_vram / std::max(max_per_gpu, 1u);
    inst.gpu_alloc.vram_used_bytes = estimated_vram;
    inst.gpu_alloc.gpu_utilization =
        static_cast<f32>(estimated_vram) / static_cast<f32>(per_gpu_vram);

    per_gpu_count++;
    if (per_gpu_count >= max_per_gpu && gpu_idx + 1 < gpu_count_) {
      gpu_idx++;
      per_gpu_count = 0;
    }
  }
}

InstanceId HeadlessRenderer::createInstance(const RenderTaskConfig& config) {
  return allocateInstance(config);
}

bool HeadlessRenderer::startInstance(InstanceId id) {
  for (auto& inst : instances_) {
    if (inst.id == id) {
      if (inst.status == InstanceStatus::Idle || inst.status == InstanceStatus::Stopped) {
        inst.status = InstanceStatus::Running;
        inst.frame_count = 0;
        inst.avg_frame_time_ms = 0.0;
        updateResourceAllocation();
        return true;
      }
      return false;
    }
  }
  return false;
}

bool HeadlessRenderer::stopInstance(InstanceId id) {
  for (auto& inst : instances_) {
    if (inst.id == id) {
      if (inst.status == InstanceStatus::Running || inst.status == InstanceStatus::Paused) {
        inst.status = InstanceStatus::Stopped;
        updateResourceAllocation();
        return true;
      }
      return false;
    }
  }
  return false;
}

bool HeadlessRenderer::pauseInstance(InstanceId id) {
  for (auto& inst : instances_) {
    if (inst.id == id) {
      if (inst.status == InstanceStatus::Running) {
        inst.status = InstanceStatus::Paused;
        return true;
      }
      return false;
    }
  }
  return false;
}

bool HeadlessRenderer::resumeInstance(InstanceId id) {
  for (auto& inst : instances_) {
    if (inst.id == id) {
      if (inst.status == InstanceStatus::Paused) {
        inst.status = InstanceStatus::Running;
        return true;
      }
      return false;
    }
  }
  return false;
}

bool HeadlessRenderer::destroyInstance(InstanceId id) {
  auto it = std::find_if(instances_.begin(), instances_.end(),
                         [id](const RenderInstance& inst) { return inst.id == id; });
  if (it != instances_.end()) {
    instances_.erase(it);
    updateResourceAllocation();
    return true;
  }
  return false;
}

const RenderInstance* HeadlessRenderer::getInstance(InstanceId id) const {
  for (const auto& inst : instances_) {
    if (inst.id == id) return &inst;
  }
  return nullptr;
}

u32 HeadlessRenderer::getRunningCount() const {
  u32 count = 0;
  for (const auto& inst : instances_) {
    if (inst.status == InstanceStatus::Running) count++;
  }
  return count;
}

std::vector<FrameOutput> HeadlessRenderer::tick(f64 delta_time) {
  std::vector<FrameOutput> outputs;

  for (auto& inst : instances_) {
    if (inst.status != InstanceStatus::Running) continue;

    // Frame scheduling: check if enough time has elapsed for this instance
    f64 frame_interval = 1000.0 / static_cast<f64>(inst.config.target_fps);  // ms

    // Simulate frame rendering
    // In real implementation, this would submit GPU work and read back pixels
    FrameOutput output;
    output.instance_id = inst.id;
    output.frame_number = inst.frame_count;
    output.width = inst.config.width;
    output.height = inst.config.height;
    output.render_time_ms = delta_time;
    output.timestamp =
        static_cast<f64>(inst.frame_count) / static_cast<f64>(inst.config.target_fps);

    // A4 follow-up: replace with real GPU render via RHIOffscreenTarget.
    // The RHI now exposes createOffscreenTarget + readbackToBuffer (see
    // rhi/include/hongtu/rhi/RHIDevice.h). For now retain the mock so the
    // higher-level instance management API (createInstance / tick / ...)
    // is still testable without an RHI device.
    // Generate placeholder pixel data (RGBA8)
    size_t pixel_count = static_cast<size_t>(inst.config.width) * inst.config.height;
    output.pixel_data.resize(pixel_count * 4);
    // Fill with a test pattern: gradient based on frame number
    u8 frame_tint = static_cast<u8>(inst.frame_count % 256);
    for (size_t i = 0; i < pixel_count; ++i) {
      u32 x = static_cast<u32>(i % inst.config.width);
      u32 y = static_cast<u32>(i / inst.config.width);
      output.pixel_data[i * 4 + 0] = static_cast<u8>(x * 255 / std::max(inst.config.width - 1, 1u));
      output.pixel_data[i * 4 + 1] = static_cast<u8>(y * 255 / std::max(inst.config.height - 1, 1u));
      output.pixel_data[i * 4 + 2] = frame_tint;
      output.pixel_data[i * 4 + 3] = 255;
    }

    // Update stats with exponential moving average
    inst.frame_count++;
    f64 alpha = 0.1;
    inst.avg_frame_time_ms = inst.avg_frame_time_ms * (1.0 - alpha) + delta_time * alpha;

    outputs.push_back(std::move(output));
  }

  return outputs;
}

void HeadlessRenderer::setGPUBudget(u64 total_vram, u32 gpu_count) {
  total_vram_ = total_vram;
  gpu_count_ = std::max(gpu_count, 1u);
  updateResourceAllocation();
}

u64 HeadlessRenderer::getTotalVRAMUsage() const {
  u64 total = 0;
  for (const auto& inst : instances_) {
    total += inst.gpu_alloc.vram_used_bytes;
  }
  return total;
}

u32 HeadlessRenderer::getMaxConcurrentInstances() const {
  if (instances_.empty()) {
    // Estimate based on a default 720p instance
    u64 per_instance = static_cast<u64>(1280) * 720 * 4 * 2 +  // framebuffer
                       static_cast<u64>(1280) * 720 * 4 +      // depth
                       static_cast<u64>(1280) * 720 * 4 +      // staging
                       64 * 1024 * 1024;                        // overhead
    return static_cast<u32>(total_vram_ / per_instance);
  }

  // Use average VRAM per instance
  u64 total_used = getTotalVRAMUsage();
  u64 avg_per_instance = total_used / static_cast<u64>(instances_.size());
  if (avg_per_instance == 0) return 100;  // arbitrary cap
  return static_cast<u32>(total_vram_ / avg_per_instance);
}

}  // namespace hongtu::cloud
