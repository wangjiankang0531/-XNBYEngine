// VTEncoder — real VideoToolbox H.264 encoder (macOS only).
//
// Verifies the cloud-render encode contract end-to-end on hardware:
// Annex-B framing, SPS/PPS on keyframes, forced IDR, monotonic pts.
#ifdef __APPLE__

#include <hongtu/cloud/vt_encoder.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

using hongtu::cloud::VTEncoder;

namespace {

struct CapturedFrame {
  std::vector<uint8_t> data;
  bool                 keyframe = false;
  uint64_t             pts      = 0;
};

// Collects encoder callbacks (they fire on a VT thread).
struct Sink {
  std::mutex                  m;
  std::condition_variable     cv;
  std::vector<CapturedFrame>  frames;

  void push(const std::vector<uint8_t>& d, bool k, uint64_t p) {
    std::lock_guard<std::mutex> lock(m);
    frames.push_back({d, k, p});
    cv.notify_all();
  }
  bool waitForFrames(size_t n, int timeout_ms = 3000) {
    std::unique_lock<std::mutex> lock(m);
    return cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                       [&] { return frames.size() >= n; });
  }
};

// 4-byte Annex-B start code at offset?
bool hasStartCodeAt(const std::vector<uint8_t>& d, size_t off) {
  return d.size() >= off + 4 && d[off] == 0 && d[off + 1] == 0 && d[off + 2] == 0 &&
         d[off + 3] == 1;
}

// NAL types present in an Annex-B stream (4-byte start codes).
std::vector<int> nalTypes(const std::vector<uint8_t>& d) {
  std::vector<int> types;
  for (size_t i = 0; i + 4 < d.size(); ++i) {
    if (hasStartCodeAt(d, i)) types.push_back(d[i + 4] & 0x1F);
  }
  return types;
}

class VTEncoderTest : public ::testing::Test {
 protected:
  static constexpr uint32_t kW = 320, kH = 240;

  bool init(VTEncoder& enc, Sink& sink) {
    VTEncoder::Config cfg;
    cfg.width             = kW;
    cfg.height            = kH;
    cfg.fps               = 30.0f;
    cfg.bitrate_kbps      = 500;
    cfg.keyframe_interval = 30;
    return enc.initialize(cfg, [&sink](const std::vector<uint8_t>& d, bool k, uint64_t p) {
      sink.push(d, k, p);
    });
  }

  std::vector<uint8_t> solidBGRA(uint8_t b, uint8_t g, uint8_t r) {
    std::vector<uint8_t> px(static_cast<size_t>(kW) * kH * 4);
    for (size_t i = 0; i < px.size(); i += 4) {
      px[i] = b; px[i + 1] = g; px[i + 2] = r; px[i + 3] = 255;
    }
    return px;
  }
};

TEST_F(VTEncoderTest, ProducesAnnexBWithSpsPpsOnKeyframe) {
  VTEncoder enc;
  Sink      sink;
  ASSERT_TRUE(init(enc, sink));

  for (int i = 0; i < 5; ++i) {
    auto px = solidBGRA(static_cast<uint8_t>(40 * i), 90, 160);
    ASSERT_TRUE(enc.encodeBGRA(px.data(), static_cast<uint64_t>(i) * 3000));
  }
  enc.shutdown();  // flushes pending frames
  ASSERT_TRUE(sink.waitForFrames(5));

  std::lock_guard<std::mutex> lock(sink.m);
  // 首帧必须是关键帧且带 SPS(7)/PPS(8)/IDR(5)。
  ASSERT_FALSE(sink.frames.empty());
  const auto& first = sink.frames.front();
  EXPECT_TRUE(first.keyframe);
  EXPECT_TRUE(hasStartCodeAt(first.data, 0));
  auto types = nalTypes(first.data);
  EXPECT_NE(std::find(types.begin(), types.end(), 7), types.end()) << "missing SPS";
  EXPECT_NE(std::find(types.begin(), types.end(), 8), types.end()) << "missing PPS";
  EXPECT_NE(std::find(types.begin(), types.end(), 5), types.end()) << "missing IDR";

  // pts 单调递增(90 kHz)。
  for (size_t i = 1; i < sink.frames.size(); ++i) {
    EXPECT_GT(sink.frames[i].pts, sink.frames[i - 1].pts);
  }
}

TEST_F(VTEncoderTest, RequestKeyframeForcesIdr) {
  VTEncoder enc;
  Sink      sink;
  ASSERT_TRUE(init(enc, sink));

  auto px = solidBGRA(30, 120, 60);
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(enc.encodeBGRA(px.data(), static_cast<uint64_t>(i) * 3000));
  }
  enc.requestKeyframe();  // 新观众加入时的强制 IDR 路径
  for (int i = 3; i < 6; ++i) {
    ASSERT_TRUE(enc.encodeBGRA(px.data(), static_cast<uint64_t>(i) * 3000));
  }
  enc.shutdown();
  ASSERT_TRUE(sink.waitForFrames(6));

  std::lock_guard<std::mutex> lock(sink.m);
  int keyframes = 0;
  for (const auto& f : sink.frames) keyframes += f.keyframe ? 1 : 0;
  // 至少两个关键帧:开头 + 强制的那个(GOP=30,自然不会再出第二个)。
  EXPECT_GE(keyframes, 2);
}

}  // namespace

#endif  // __APPLE__
