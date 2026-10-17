#include <gtest/gtest.h>
#include <hongtu/cloud/headless_renderer.hpp>
#include <hongtu/cloud/video_encoder.hpp>
#include <hongtu/cloud/webrtc_transport.hpp>

using namespace hongtu::cloud;

// ============================================================================
// HeadlessRenderer
// ============================================================================

TEST(HeadlessRendererTest, CreateInstance) {
  HeadlessRenderer renderer;
  auto id = renderer.createInstance({1280, 720, 30.0f, "scene1"});
  EXPECT_GT(id, 0u);

  auto* inst = renderer.getInstance(id);
  ASSERT_NE(inst, nullptr);
  EXPECT_EQ(inst->status, InstanceStatus::Idle);
  EXPECT_EQ(inst->config.width, 1280u);
}

TEST(HeadlessRendererTest, StartStop) {
  HeadlessRenderer renderer;
  auto id = renderer.createInstance({1280, 720, 30.0f, "s1"});

  EXPECT_TRUE(renderer.startInstance(id));
  EXPECT_EQ(renderer.getInstance(id)->status, InstanceStatus::Running);
  EXPECT_EQ(renderer.getRunningCount(), 1u);

  EXPECT_TRUE(renderer.stopInstance(id));
  EXPECT_EQ(renderer.getInstance(id)->status, InstanceStatus::Stopped);
  EXPECT_EQ(renderer.getRunningCount(), 0u);
}

TEST(HeadlessRendererTest, PauseResume) {
  HeadlessRenderer renderer;
  auto id = renderer.createInstance({});
  renderer.startInstance(id);

  EXPECT_TRUE(renderer.pauseInstance(id));
  EXPECT_EQ(renderer.getInstance(id)->status, InstanceStatus::Paused);

  EXPECT_TRUE(renderer.resumeInstance(id));
  EXPECT_EQ(renderer.getInstance(id)->status, InstanceStatus::Running);
}

TEST(HeadlessRendererTest, MultipleInstances) {
  HeadlessRenderer renderer;
  for (int i = 0; i < 5; ++i) {
    auto id = renderer.createInstance({720, 480, 30.0f, "scene" + std::to_string(i)});
    renderer.startInstance(id);
  }
  EXPECT_EQ(renderer.getRunningCount(), 5u);
  EXPECT_EQ(renderer.getInstances().size(), 5u);
}

TEST(HeadlessRendererTest, Tick) {
  HeadlessRenderer renderer;
  auto id = renderer.createInstance({64, 64, 30.0f, "test"});
  renderer.startInstance(id);

  auto frames = renderer.tick(0.033);
  EXPECT_EQ(frames.size(), 1u);
  EXPECT_EQ(frames[0].instance_id, id);
  EXPECT_EQ(frames[0].width, 64u);
  EXPECT_EQ(frames[0].height, 64u);
}

TEST(HeadlessRendererTest, Destroy) {
  HeadlessRenderer renderer;
  auto id = renderer.createInstance({});
  EXPECT_TRUE(renderer.destroyInstance(id));
  EXPECT_EQ(renderer.getInstance(id), nullptr);
}

// ============================================================================
// VideoEncoder
// ============================================================================

TEST(VideoEncoderTest, Initialize) {
  EncoderConfig cfg;
  cfg.width = 1920;
  cfg.height = 1080;
  cfg.fps = 60.0f;
  cfg.bitrate_kbps = 5000;
  VideoEncoder encoder(cfg);

  ASSERT_TRUE(encoder.initialize());
  EXPECT_TRUE(encoder.isInitialized());
}

TEST(VideoEncoderTest, InvalidConfig) {
  EncoderConfig cfg;
  cfg.width = 0;
  cfg.height = 0;
  VideoEncoder encoder(cfg);
  EXPECT_FALSE(encoder.initialize());
}

TEST(VideoEncoderTest, EncodeFrame) {
  EncoderConfig cfg;
  cfg.width = 64;
  cfg.height = 64;
  cfg.gop_size = 10;
  VideoEncoder encoder(cfg);
  encoder.initialize();

  std::vector<uint8_t> pixels(64 * 64 * 4, 128);
  RawFrame frame;
  frame.pixels = pixels.data();
  frame.width = 64;
  frame.height = 64;
  frame.frame_number = 0;
  frame.timestamp = 0.0;

  auto encoded = encoder.encodeFrame(frame);
  EXPECT_GT(encoded.data.size(), 0u);
  EXPECT_TRUE(encoded.is_keyframe); // First frame is always keyframe
}

TEST(VideoEncoderTest, GOPKeyframePattern) {
  EncoderConfig cfg;
  cfg.width = 64;
  cfg.height = 64;
  cfg.gop_size = 4;
  VideoEncoder encoder(cfg);
  encoder.initialize();

  std::vector<uint8_t> pixels(64 * 64 * 4, 0);

  int keyframe_count = 0;
  for (uint32_t i = 0; i < 12; ++i) {
    RawFrame frame{pixels.data(), 64, 64, i, static_cast<double>(i) / 30.0};
    auto encoded = encoder.encodeFrame(frame);
    if (encoded.is_keyframe) keyframe_count++;
  }
  // With GOP=4, expect 3 keyframes in 12 frames (0, 4, 8)
  EXPECT_EQ(keyframe_count, 3);
}

TEST(VideoEncoderTest, Stats) {
  EncoderConfig cfg;
  cfg.width = 64;
  cfg.height = 64;
  VideoEncoder encoder(cfg);
  encoder.initialize();

  std::vector<uint8_t> pixels(64 * 64 * 4, 0);
  for (uint32_t i = 0; i < 10; ++i) {
    RawFrame frame{pixels.data(), 64, 64, i, 0.0};
    encoder.encodeFrame(frame);
  }

  auto& stats = encoder.getStats();
  EXPECT_EQ(stats.frames_encoded, 10u);
  EXPECT_GT(stats.total_bytes, 0u);
}

TEST(VideoEncoderTest, CodecNames) {
  EXPECT_STREQ(VideoEncoder::codecName(VideoCodec::H264), "H.264/AVC");
  EXPECT_STREQ(VideoEncoder::codecName(VideoCodec::H265), "H.265/HEVC");
  EXPECT_STREQ(VideoEncoder::hardwareName(EncoderHardware::NVENC), "NVIDIA NVENC");
}

TEST(VideoEncoderTest, BitrateEstimate) {
  uint32_t br = VideoEncoder::estimateBitrate(1920, 1080, 60.0f, 5);
  EXPECT_GT(br, 0u);

  uint32_t br_low = VideoEncoder::estimateBitrate(640, 480, 30.0f, 3);
  EXPECT_LT(br_low, br);
}

// ============================================================================
// WebRTCTransport
// ============================================================================

TEST(WebRTCTransportTest, CreateSession) {
  WebRTCTransport transport;
  auto id = transport.createSession();
  EXPECT_FALSE(id.empty());

  auto* session = transport.getSession(id);
  ASSERT_NE(session, nullptr);
  EXPECT_EQ(session->state, SessionState::Connecting);
}

TEST(WebRTCTransportTest, ConnectSession) {
  WebRTCTransport transport;
  auto id = transport.createSession();
  EXPECT_TRUE(transport.connectSession(id));

  auto* session = transport.getSession(id);
  EXPECT_EQ(session->state, SessionState::Connected);
  EXPECT_EQ(transport.getActiveSessionCount(), 1u);
}

TEST(WebRTCTransportTest, SignalingFlow) {
  WebRTCTransport transport;
  auto id = transport.createSession();

  SDPMessage offer;
  offer.type = "offer";
  offer.sdp = "v=0\r\n...";
  offer.session_id = id;

  auto answer = transport.processOffer(id, offer);
  EXPECT_EQ(answer.type, "answer");
  EXPECT_FALSE(answer.sdp.empty());
}

TEST(WebRTCTransportTest, InputCommands) {
  WebRTCTransport transport;
  auto id = transport.createSession();
  transport.connectSession(id);

  InputCommand cmd;
  cmd.type = InputCommand::Type::MouseClick;
  cmd.x = 100;
  cmd.y = 200;
  cmd.timestamp = 0.0;
  transport.pushInputCommand(id, cmd);

  auto cmds = transport.getInputCommands(id);
  EXPECT_EQ(cmds.size(), 1u);
  EXPECT_EQ(cmds[0].x, 100);

  // Queue should be empty after getting
  auto cmds2 = transport.getInputCommands(id);
  EXPECT_EQ(cmds2.size(), 0u);
}

TEST(WebRTCTransportTest, SendFrame) {
  WebRTCTransport transport;
  auto id = transport.createSession();
  transport.connectSession(id);

  std::vector<uint8_t> data(1000, 0);
  VideoFrame frame;
  frame.data = data.data();
  frame.size = 1000;
  frame.frame_number = 0;
  frame.is_keyframe = true;
  frame.timestamp = 0.0;

  EXPECT_TRUE(transport.sendFrame(id, frame));

  auto* session = transport.getSession(id);
  EXPECT_EQ(session->stats.frames_sent, 1u);
  EXPECT_GT(session->stats.bytes_sent, 0u);
}

TEST(WebRTCTransportTest, DisconnectSession) {
  WebRTCTransport transport;
  auto id = transport.createSession();
  transport.connectSession(id);
  transport.disconnectSession(id);

  EXPECT_EQ(transport.getSession(id)->state, SessionState::Disconnected);
  EXPECT_EQ(transport.getActiveSessionCount(), 0u);
}

TEST(WebRTCTransportTest, MultipleSessions) {
  WebRTCTransport transport;
  std::vector<SessionId> ids;
  for (int i = 0; i < 10; ++i) {
    auto id = transport.createSession();
    transport.connectSession(id);
    ids.push_back(id);
  }
  EXPECT_EQ(transport.getActiveSessionCount(), 10u);
  EXPECT_EQ(transport.getSessionIds().size(), 10u);
}

TEST(WebRTCTransportTest, AdaptiveBitrate) {
  WebRTCTransport transport;
  SessionConfig cfg;
  cfg.max_bitrate_kbps = 5000;
  cfg.min_bitrate_kbps = 500;
  auto id = transport.createSession(cfg);
  transport.connectSession(id);

  uint32_t br = transport.computeAdaptiveBitrate(id);
  EXPECT_GE(br, cfg.min_bitrate_kbps);
  EXPECT_LE(br, cfg.max_bitrate_kbps);
}

TEST(WebRTCTransportTest, KeyframeScheduling) {
  WebRTCTransport transport;
  SessionConfig cfg;
  cfg.keyframe_interval_sec = 2.0;
  auto id = transport.createSession(cfg);
  transport.connectSession(id);

  // At t=0 with last_keyframe_time=0, elapsed=0, should NOT need keyframe
  EXPECT_FALSE(transport.shouldSendKeyframe(id, 0.0));

  // At t=1, elapsed=1 < interval=2, should NOT need
  EXPECT_FALSE(transport.shouldSendKeyframe(id, 1.0));

  // At t=2.5, elapsed=2.5 >= interval=2, SHOULD need keyframe
  EXPECT_TRUE(transport.shouldSendKeyframe(id, 2.5));
}

TEST(WebRTCTransportTest, BandwidthTracking) {
  WebRTCTransport transport;
  auto id = transport.createSession();
  transport.connectSession(id);

  // Send some frames and update to trigger bandwidth calc
  std::vector<uint8_t> data(5000, 0);
  for (int i = 0; i < 30; ++i) {
    VideoFrame f{data.data(), 5000, static_cast<uint32_t>(i), i == 0, 0.0};
    transport.sendFrame(id, f);
    transport.update(0.033); // Simulate frame tick
  }

  // Verify frames were sent
  auto* session = transport.getSession(id);
  EXPECT_GT(session->stats.frames_sent, 0u);
  EXPECT_GT(session->stats.bytes_sent, 0u);
}
