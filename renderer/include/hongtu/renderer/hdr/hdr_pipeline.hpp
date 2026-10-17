#pragma once

#include <hongtu/core/common.hpp>
#include <glm/glm.hpp>
#include <array>
#include <vector>

namespace hongtu::renderer {

/// HDR color format.
enum class HDRFormat {
  RGBA16F,     ///< 16-bit float per channel (64 bits total)
  RGBA32F,     ///< 32-bit float per channel (128 bits total)
  R11G11B10F,  ///< Packed 11/11/10 float (32 bits total, no alpha)
  RGB9E5,      ///< Shared exponent (32 bits, read-only)
};

/// Auto-exposure metering mode.
enum class MeteringMode {
  Average,         ///< Whole-screen average luminance
  CenterWeighted,  ///< Center-weighted average
  Spot,            ///< Small spot in center
  Histogram,       ///< Histogram-based (most accurate)
};

/// HDR pipeline configuration.
struct HDRConfig {
  HDRFormat render_format = HDRFormat::RGBA16F;
  u32 color_depth_bits = 12;        ///< Effective color depth
  bool auto_exposure = true;
  MeteringMode metering = MeteringMode::Histogram;
  f32 exposure_min = 0.01f;         ///< Minimum auto-exposure value
  f32 exposure_max = 10.0f;         ///< Maximum auto-exposure value
  f32 adaptation_speed_up = 2.0f;   ///< Speed of eye adaptation to brighter scenes
  f32 adaptation_speed_down = 1.0f; ///< Speed of eye adaptation to darker scenes
  f32 key_value = 0.18f;            ///< Target middle-grey key value
  f32 manual_exposure = 1.0f;       ///< Manual exposure when auto is disabled
  bool hdr_display = false;         ///< Whether output targets HDR display
  f32 hdr_max_nits = 1000.0f;       ///< Peak brightness for HDR display
  f32 sdr_max_nits = 80.0f;         ///< SDR reference white level
};

/// Luminance histogram for exposure computation.
class LuminanceHistogram {
 public:
  static constexpr u32 BIN_COUNT = 256;

  LuminanceHistogram();

  /// Clear the histogram.
  void clear();

  /// Add a luminance value to the histogram.
  /// @param luminance  Scene luminance value (>= 0)
  void addSample(f32 luminance);

  /// Build histogram from a buffer of luminance values.
  void buildFromBuffer(const f32* luminance_buffer, u32 count);

  /// Compute average luminance from histogram (log-average).
  f32 computeAverageLuminance(f32 min_luminance = 0.001f,
                               f32 max_luminance = 100.0f) const;

  /// Compute exposure from average luminance.
  /// Uses key_value / average_luminance formula.
  static f32 computeExposure(f32 avg_luminance, f32 key_value = 0.18f);

  /// Get bin count for a specific bin.
  u32 getBin(u32 index) const;

  /// Get total sample count.
  u32 getTotalSamples() const { return total_samples_; }

  /// Get the range of luminance mapped to histogram.
  void getLuminanceRange(f32& min_log, f32& max_log) const;

  /// Get raw bin data.
  const std::array<u32, BIN_COUNT>& getBins() const { return bins_; }

 private:
  /// Map luminance to bin index.
  u32 luminanceToBin(f32 luminance) const;
  /// Map bin index to luminance.
  f32 binToLuminance(u32 bin) const;

  std::array<u32, BIN_COUNT> bins_{};
  u32 total_samples_ = 0;
  f32 min_log_luminance_ = -8.0f;   // log2(1/256)
  f32 max_log_luminance_ = 8.0f;    // log2(256)
};

/// Auto-exposure controller with temporal adaptation.
class AutoExposure {
 public:
  explicit AutoExposure(const HDRConfig& config = {});

  /// Update exposure based on new luminance data.
  /// @param avg_luminance   Current frame average luminance.
  /// @param delta_time      Time since last frame in seconds.
  /// @return Current exposure value.
  f32 update(f32 avg_luminance, f32 delta_time);

  /// Get current exposure value.
  f32 getExposure() const { return current_exposure_; }

  /// Get target exposure (before adaptation).
  f32 getTargetExposure() const { return target_exposure_; }

  /// Reset to default exposure.
  void reset();

  /// Set config.
  void setConfig(const HDRConfig& config) { config_ = config; }
  const HDRConfig& getConfig() const { return config_; }

 private:
  HDRConfig config_;
  f32 current_exposure_ = 1.0f;
  f32 target_exposure_ = 1.0f;
};

/// Compute relative luminance from linear RGB.
inline f32 luminance(const glm::vec3& color) {
  return 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
}

/// Get bytes per pixel for an HDR format.
u32 hdrFormatBytesPerPixel(HDRFormat format);

/// Get format name string.
const char* hdrFormatName(HDRFormat format);

} // namespace hongtu::renderer
