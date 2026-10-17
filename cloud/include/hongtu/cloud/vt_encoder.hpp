#pragma once

#include <hongtu/core/common.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace hongtu::cloud {

/// REAL H.264 encoder backed by Apple VideoToolbox (hardware on Apple
/// Silicon). Replaces the simulated bitstream of `VideoEncoder` for the
/// cloud-render pipeline — the old class kept its API for tests, this one
/// produces decodable NAL units a browser can actually play.
///
/// Output convention: Annex-B (start codes), SPS/PPS prepended to every
/// keyframe — exactly what rtc::H264RtpPacketizer(StartSequence) and
/// browsers expect.
///
/// macOS only (guarded in CMake). Frames are BGRA8 top-down, the natural
/// output of `RHIDevice::readbackSwapchainImage`.
class VTEncoder {
 public:
  struct Config {
    u32 width        = 1280;
    u32 height       = 720;
    f32 fps          = 30.0f;
    u32 bitrate_kbps = 4000;
    u32 keyframe_interval = 60;  ///< GOP length in frames
  };

  /// (annexb_data, is_keyframe, pts_90khz)
  using FrameCallback =
      std::function<void(const std::vector<uint8_t>&, bool, uint64_t)>;

  VTEncoder() = default;
  ~VTEncoder();

  VTEncoder(const VTEncoder&) = delete;
  VTEncoder& operator=(const VTEncoder&) = delete;

  bool initialize(const Config& cfg, FrameCallback on_frame);

  /// Encode one BGRA8 frame (stride = width*4). `pts_90khz` monotonic.
  /// The callback fires on the VT callback thread — keep it cheap/queue.
  bool encodeBGRA(const uint8_t* bgra, uint64_t pts_90khz);

  /// Force the next frame to be an IDR (new viewer joined).
  void requestKeyframe() { force_keyframe_ = true; }

  void shutdown();

 private:
  struct Impl;
  Impl*  impl_ = nullptr;
  Config cfg_{};
  FrameCallback on_frame_;
  bool   force_keyframe_ = false;

  friend struct VTEncoderTrampoline;
};

}  // namespace hongtu::cloud
