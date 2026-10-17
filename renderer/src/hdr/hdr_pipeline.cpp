#include <hongtu/renderer/hdr/hdr_pipeline.hpp>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace hongtu::renderer {

// ============================================================================
// HDR Format Utilities
// ============================================================================

u32 hdrFormatBytesPerPixel(HDRFormat format) {
  switch (format) {
    case HDRFormat::RGBA16F:    return 8;
    case HDRFormat::RGBA32F:    return 16;
    case HDRFormat::R11G11B10F: return 4;
    case HDRFormat::RGB9E5:     return 4;
  }
  return 8;
}

const char* hdrFormatName(HDRFormat format) {
  switch (format) {
    case HDRFormat::RGBA16F:    return "RGBA16F";
    case HDRFormat::RGBA32F:    return "RGBA32F";
    case HDRFormat::R11G11B10F: return "R11G11B10F";
    case HDRFormat::RGB9E5:     return "RGB9E5";
  }
  return "Unknown";
}

// ============================================================================
// LuminanceHistogram
// ============================================================================

LuminanceHistogram::LuminanceHistogram() { clear(); }

void LuminanceHistogram::clear() {
  bins_.fill(0);
  total_samples_ = 0;
}

u32 LuminanceHistogram::luminanceToBin(f32 lum) const {
  if (lum <= 0.0f) return 0;
  f32 log_lum = std::log2(lum);
  f32 t = (log_lum - min_log_luminance_) / (max_log_luminance_ - min_log_luminance_);
  t = glm::clamp(t, 0.0f, 1.0f);
  return std::min(static_cast<u32>(t * (BIN_COUNT - 1)), BIN_COUNT - 1);
}

f32 LuminanceHistogram::binToLuminance(u32 bin) const {
  f32 t = static_cast<f32>(bin) / static_cast<f32>(BIN_COUNT - 1);
  f32 log_lum = min_log_luminance_ + t * (max_log_luminance_ - min_log_luminance_);
  return std::pow(2.0f, log_lum);
}

void LuminanceHistogram::addSample(f32 lum) {
  bins_[luminanceToBin(lum)]++;
  total_samples_++;
}

void LuminanceHistogram::buildFromBuffer(const f32* buffer, u32 count) {
  clear();
  for (u32 i = 0; i < count; ++i) {
    addSample(buffer[i]);
  }
}

f32 LuminanceHistogram::computeAverageLuminance(f32 min_lum, f32 max_lum) const {
  if (total_samples_ == 0) return 0.1f;

  // Compute log-average, excluding outliers
  u32 min_bin = luminanceToBin(min_lum);
  u32 max_bin = luminanceToBin(max_lum);

  f64 sum_log = 0.0;
  u32 count = 0;

  for (u32 i = min_bin; i <= max_bin; ++i) {
    if (bins_[i] > 0) {
      f32 lum = binToLuminance(i);
      sum_log += std::log(static_cast<f64>(lum) + 1e-6) * bins_[i];
      count += bins_[i];
    }
  }

  if (count == 0) return 0.1f;
  return static_cast<f32>(std::exp(sum_log / count));
}

f32 LuminanceHistogram::computeExposure(f32 avg_luminance, f32 key_value) {
  if (avg_luminance <= 0.0f) return 1.0f;
  return key_value / avg_luminance;
}

u32 LuminanceHistogram::getBin(u32 index) const {
  return (index < BIN_COUNT) ? bins_[index] : 0;
}

void LuminanceHistogram::getLuminanceRange(f32& min_log, f32& max_log) const {
  min_log = min_log_luminance_;
  max_log = max_log_luminance_;
}

// ============================================================================
// AutoExposure
// ============================================================================

AutoExposure::AutoExposure(const HDRConfig& config) : config_(config) {}

f32 AutoExposure::update(f32 avg_luminance, f32 delta_time) {
  if (!config_.auto_exposure) {
    current_exposure_ = config_.manual_exposure;
    return current_exposure_;
  }

  target_exposure_ = LuminanceHistogram::computeExposure(avg_luminance, config_.key_value);
  target_exposure_ = glm::clamp(target_exposure_, config_.exposure_min, config_.exposure_max);

  // Smooth adaptation
  f32 speed = (target_exposure_ > current_exposure_)
                  ? config_.adaptation_speed_up
                  : config_.adaptation_speed_down;

  f32 factor = 1.0f - std::exp(-delta_time * speed);
  current_exposure_ += (target_exposure_ - current_exposure_) * factor;
  current_exposure_ = glm::clamp(current_exposure_, config_.exposure_min, config_.exposure_max);

  return current_exposure_;
}

void AutoExposure::reset() {
  current_exposure_ = 1.0f;
  target_exposure_ = 1.0f;
}

} // namespace hongtu::renderer
