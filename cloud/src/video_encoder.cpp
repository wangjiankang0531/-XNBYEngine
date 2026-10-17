#include <hongtu/cloud/video_encoder.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hongtu::cloud {

VideoEncoder::VideoEncoder(const EncoderConfig& config) : config_(config) {}

bool VideoEncoder::initialize() {
  // Validate configuration
  if (config_.width == 0 || config_.height == 0) return false;
  if (config_.fps <= 0.0f) return false;
  if (config_.bitrate_kbps == 0) return false;
  if (config_.gop_size == 0) return false;

  // Width and height should be even for most codecs
  if (config_.width % 2 != 0 || config_.height % 2 != 0) return false;

  // Reset stats
  stats_ = {};
  frame_counter_ = 0;
  initialized_ = true;
  return true;
}

EncodedFrame VideoEncoder::encodeFrame(const RawFrame& frame) {
  EncodedFrame output;

  if (!initialized_) {
    return output;
  }

  if (!frame.pixels || frame.width != config_.width || frame.height != config_.height) {
    return output;
  }

  // Determine if this is a keyframe
  bool is_keyframe = (frame_counter_ % config_.gop_size == 0);

  // Estimate encoded frame size based on rate control
  u32 frame_size = estimateFrameSize(is_keyframe);

  // Simulate encoding: in a real implementation, this would call FFmpeg/NVENC/etc.
  // We produce a placeholder encoded bitstream.
  output.data.resize(frame_size);

  // Create a simple simulated bitstream header
  // Real encoder would produce actual NAL units here
  if (frame_size >= 8) {
    // Fake start code + frame type marker
    output.data[0] = 0x00;
    output.data[1] = 0x00;
    output.data[2] = 0x00;
    output.data[3] = 0x01;
    output.data[4] = is_keyframe ? 0x65 : 0x41;  // IDR vs non-IDR NAL type (H.264-like)
    output.data[5] = static_cast<u8>(frame_counter_ & 0xFF);
    output.data[6] = static_cast<u8>((frame_counter_ >> 8) & 0xFF);
    output.data[7] = static_cast<u8>((frame_counter_ >> 16) & 0xFF);

    // Fill rest with a hash of the input pixels (simulates actual encoding work)
    u32 hash = 0;
    size_t pixel_count = static_cast<size_t>(frame.width) * frame.height * 4;
    size_t step = std::max(pixel_count / 1024, static_cast<size_t>(1));
    for (size_t i = 0; i < pixel_count; i += step) {
      hash = hash * 31 + frame.pixels[i];
    }
    for (size_t i = 8; i < frame_size; ++i) {
      output.data[i] = static_cast<u8>((hash >> ((i % 4) * 8)) & 0xFF);
      hash = hash * 1103515245 + 12345;  // LCG
    }
  }

  // Simulate encode timing
  // Rough estimate: quality_preset affects encode speed
  f64 base_time_ms = 1.0;  // 1ms baseline
  f64 quality_factor = static_cast<f64>(config_.quality_preset) / 5.0;
  f64 resolution_factor =
      static_cast<f64>(config_.width * config_.height) / (1920.0 * 1080.0);
  f64 encode_time = base_time_ms * quality_factor * resolution_factor;
  if (is_keyframe) encode_time *= 2.0;  // Keyframes take longer

  output.frame_number = frame_counter_;
  output.is_keyframe = is_keyframe;
  output.encode_time_ms = encode_time;
  output.pts = static_cast<f64>(frame_counter_) / static_cast<f64>(config_.fps);
  output.size_bytes = frame_size;

  // Update statistics
  frame_counter_++;
  stats_.frames_encoded++;
  if (is_keyframe) stats_.keyframe_count++;
  stats_.total_bytes += frame_size;
  stats_.total_time_sec = static_cast<f64>(stats_.frames_encoded) / static_cast<f64>(config_.fps);

  // Exponential moving average for encode time
  f64 alpha = 0.1;
  stats_.avg_encode_time_ms =
      stats_.avg_encode_time_ms * (1.0 - alpha) + encode_time * alpha;

  // Compute average bitrate
  if (stats_.total_time_sec > 0.0) {
    stats_.avg_bitrate_kbps =
        static_cast<f64>(stats_.total_bytes * 8) / (stats_.total_time_sec * 1000.0);
  }

  return output;
}

std::vector<EncodedFrame> VideoEncoder::flush() {
  // In a real encoder with B-frames, there may be buffered frames.
  // Since max_b_frames can be 0 (low latency), we may have nothing to flush.
  // Simulate flushing any remaining buffered frames.
  std::vector<EncodedFrame> flushed;

  if (!initialized_) return flushed;

  // With B-frames, we might have up to max_b_frames buffered
  // For simplicity, we just return empty - no frames buffered in our simulation
  // since we output immediately in encodeFrame.

  return flushed;
}

u32 VideoEncoder::estimateBitrate(u32 width, u32 height, f32 fps, u32 quality) {
  // Empirical formula for target bitrate (kbps)
  // Based on common streaming recommendations
  f64 pixels_per_frame = static_cast<f64>(width) * height;
  f64 pixels_1080p = 1920.0 * 1080.0;

  // Base bitrate for 1080p30 at mid quality
  f64 base_kbps = 3000.0;

  // Scale by resolution
  f64 resolution_factor = pixels_per_frame / pixels_1080p;

  // Scale by framerate
  f64 fps_factor = static_cast<f64>(fps) / 30.0;

  // Scale by quality (1-10)
  f64 quality_factor = 0.3 + static_cast<f64>(quality) * 0.15;

  return static_cast<u32>(base_kbps * resolution_factor * fps_factor * quality_factor);
}

const char* VideoEncoder::codecName(VideoCodec codec) {
  switch (codec) {
    case VideoCodec::H264: return "H.264/AVC";
    case VideoCodec::H265: return "H.265/HEVC";
    case VideoCodec::VP9: return "VP9";
    case VideoCodec::AV1: return "AV1";
    default: return "Unknown";
  }
}

const char* VideoEncoder::hardwareName(EncoderHardware hw) {
  switch (hw) {
    case EncoderHardware::Software: return "Software (libx264/libx265)";
    case EncoderHardware::NVENC: return "NVIDIA NVENC";
    case EncoderHardware::VAAPI: return "VA-API";
    case EncoderHardware::VideoToolbox: return "Apple VideoToolbox";
    case EncoderHardware::QSV: return "Intel Quick Sync";
    default: return "Unknown";
  }
}

u32 VideoEncoder::estimateFrameSize(bool keyframe) const {
  // Estimate frame size based on bitrate and GOP structure
  // Total bits per second = bitrate_kbps * 1000
  // Bits per frame = total_bits / fps
  f64 bits_per_frame = static_cast<f64>(config_.bitrate_kbps) * 1000.0 / static_cast<f64>(config_.fps);

  // Keyframes are typically 2-5x larger than P-frames
  f64 keyframe_ratio = 3.0;

  if (keyframe) {
    // Distribute budget: gop_size frames, 1 keyframe + (gop_size-1) P-frames
    // keyframe_bits = keyframe_ratio * p_frame_bits
    // total_bits = keyframe_bits + (gop_size - 1) * p_frame_bits
    // total_bits = (keyframe_ratio + gop_size - 1) * p_frame_bits
    f64 total_gop_bits = bits_per_frame * config_.gop_size;
    f64 p_frame_bits = total_gop_bits / (keyframe_ratio + config_.gop_size - 1);
    bits_per_frame = p_frame_bits * keyframe_ratio;
  } else {
    f64 total_gop_bits = bits_per_frame * config_.gop_size;
    f64 p_frame_bits = total_gop_bits / (keyframe_ratio + config_.gop_size - 1);
    bits_per_frame = p_frame_bits;
  }

  // VBR mode allows some variation
  if (config_.rate_control == RateControlMode::VBR) {
    // VBR can vary +/- 30%
    bits_per_frame *= (keyframe ? 1.3 : 0.9);
  }

  u32 bytes = static_cast<u32>(bits_per_frame / 8.0);
  return std::max(bytes, 64u);  // Minimum 64 bytes
}

}  // namespace hongtu::cloud
