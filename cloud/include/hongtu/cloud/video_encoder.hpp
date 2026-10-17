#pragma once

#include <hongtu/core/common.hpp>
#include <vector>
#include <string>
#include <functional>
#include <queue>

namespace hongtu::cloud {

/// Video codec type.
enum class VideoCodec { H264, H265, VP9, AV1 };

/// Encoder profile.
enum class EncoderProfile { Baseline, Main, High };

/// Rate control mode.
enum class RateControlMode { CBR, VBR, CQP };

/// Encoder hardware type.
enum class EncoderHardware { Software, NVENC, VAAPI, VideoToolbox, QSV };

/// Encoder configuration.
struct EncoderConfig {
  VideoCodec codec = VideoCodec::H264;
  EncoderProfile profile = EncoderProfile::Main;
  u32 width = 1920;
  u32 height = 1080;
  f32 fps = 60.0f;
  u32 bitrate_kbps = 5000;
  u32 gop_size = 30;              ///< Keyframe interval
  u32 max_b_frames = 0;           ///< B-frames (0 for low latency)
  RateControlMode rate_control = RateControlMode::CBR;
  EncoderHardware hardware = EncoderHardware::Software;
  bool low_latency = true;
  u32 quality_preset = 5;          ///< 1=fastest, 10=best quality
};

/// Encoded frame output.
struct EncodedFrame {
  std::vector<u8> data;
  u32 frame_number;
  bool is_keyframe;
  f64 encode_time_ms;
  f64 pts;                          ///< Presentation timestamp (seconds)
  u64 size_bytes;
};

/// Frame input for encoding.
struct RawFrame {
  const u8* pixels;               ///< RGBA8 pixel data
  u32 width, height;
  u32 frame_number;
  f64 timestamp;
};

/// Encoder statistics.
struct EncoderStats {
  u32 frames_encoded = 0;
  f64 avg_encode_time_ms = 0.0;
  f64 avg_bitrate_kbps = 0.0;
  u64 total_bytes = 0;
  u32 keyframe_count = 0;
  f64 total_time_sec = 0.0;
};

/// Video encoder abstraction (CPU framework, actual encoding delegated to FFmpeg/hardware).
class VideoEncoder {
 public:
  explicit VideoEncoder(const EncoderConfig& config = {});

  /// Initialize the encoder. Returns false on configuration error.
  bool initialize();

  /// Encode a raw frame. Returns the encoded frame (may be empty if buffered).
  EncodedFrame encodeFrame(const RawFrame& frame);

  /// Flush remaining buffered frames.
  std::vector<EncodedFrame> flush();

  /// Get encoder statistics.
  const EncoderStats& getStats() const { return stats_; }

  /// Get configuration.
  const EncoderConfig& getConfig() const { return config_; }

  /// Check if the encoder is initialized.
  bool isInitialized() const { return initialized_; }

  /// Estimate output bitrate for given config.
  static u32 estimateBitrate(u32 width, u32 height, f32 fps, u32 quality);

  /// Get codec name string.
  static const char* codecName(VideoCodec codec);

  /// Get hardware name string.
  static const char* hardwareName(EncoderHardware hw);

  /// Compute frame size estimate for rate control.
  u32 estimateFrameSize(bool keyframe) const;

 private:
  EncoderConfig config_;
  EncoderStats stats_;
  bool initialized_ = false;
  u32 frame_counter_ = 0;
};

} // namespace hongtu::cloud
