#pragma once

#include <hongtu/core/common.hpp>
#include <string>
#include <vector>
#include <functional>
#include <queue>
#include <unordered_map>
#include <chrono>

namespace hongtu::cloud {

/// WebRTC session identifier.
using SessionId = std::string;

/// Session state.
enum class SessionState { Connecting, Connected, Disconnected, Failed };

/// Input command from client.
struct InputCommand {
  enum class Type { MouseMove, MouseClick, MouseScroll, KeyDown, KeyUp, Custom };
  Type type;
  f64 timestamp;
  i32 x = 0, y = 0;        ///< Mouse position
  i32 delta = 0;            ///< Scroll delta or key code
  std::string payload;       ///< Custom command payload (JSON)
};

/// Video frame to send to client.
struct VideoFrame {
  const u8* data;
  u32 size;
  u32 frame_number;
  bool is_keyframe;
  f64 timestamp;
};

/// Session statistics.
struct SessionStats {
  f64 rtt_ms = 0.0;                 ///< Round-trip time
  f64 jitter_ms = 0.0;
  u64 bytes_sent = 0;
  u64 bytes_received = 0;
  u32 frames_sent = 0;
  u32 frames_dropped = 0;
  u32 packets_lost = 0;
  f64 bandwidth_kbps = 0.0;
  f64 uptime_sec = 0.0;
};

/// Session configuration.
struct SessionConfig {
  u32 max_bitrate_kbps = 5000;
  u32 min_bitrate_kbps = 500;
  u32 target_fps = 30;
  bool enable_adaptive_bitrate = true;
  f64 keyframe_interval_sec = 2.0;
};

/// A single WebRTC session (one client connection).
struct WebRTCSession {
  SessionId id;
  SessionState state = SessionState::Connecting;
  SessionConfig config;
  SessionStats stats;
  std::queue<InputCommand> input_queue;
  std::chrono::steady_clock::time_point connect_time;
  f64 last_keyframe_time = 0.0;
};

/// SDP (Session Description Protocol) offer/answer for signaling.
struct SDPMessage {
  std::string type;     ///< "offer" or "answer"
  std::string sdp;      ///< SDP content
  SessionId session_id;
};

/// ICE candidate for signaling.
struct ICECandidate {
  std::string candidate;
  std::string sdp_mid;
  i32 sdp_mline_index;
  SessionId session_id;
};

/// WebRTC transport: manages sessions, signaling, and frame delivery.
class WebRTCTransport {
 public:
  WebRTCTransport() = default;

  /// Create a new session. Returns session ID.
  SessionId createSession(const SessionConfig& config = {});

  /// Process an SDP offer (from client) and generate answer.
  SDPMessage processOffer(const SessionId& id, const SDPMessage& offer);

  /// Add ICE candidate from client.
  void addICECandidate(const SessionId& id, const ICECandidate& candidate);

  /// Connect a session (simulate connection establishment).
  bool connectSession(const SessionId& id);

  /// Disconnect a session.
  void disconnectSession(const SessionId& id);

  /// Send a video frame to a session.
  bool sendFrame(const SessionId& id, const VideoFrame& frame);

  /// Get pending input commands from a session.
  std::vector<InputCommand> getInputCommands(const SessionId& id);

  /// Push an input command (simulate client input).
  void pushInputCommand(const SessionId& id, const InputCommand& cmd);

  /// Update all sessions (call once per frame).
  void update(f64 delta_time);

  /// Get session info.
  const WebRTCSession* getSession(const SessionId& id) const;

  /// Get all session IDs.
  std::vector<SessionId> getSessionIds() const;

  /// Get total active sessions.
  u32 getActiveSessionCount() const;

  /// Get aggregate bandwidth usage.
  f64 getTotalBandwidthKbps() const;

  /// Should send a keyframe for this session?
  bool shouldSendKeyframe(const SessionId& id, f64 current_time) const;

  /// Compute adaptive bitrate for a session based on network conditions.
  u32 computeAdaptiveBitrate(const SessionId& id) const;

 private:
  std::unordered_map<SessionId, WebRTCSession> sessions_;
  u32 session_counter_ = 0;
};

} // namespace hongtu::cloud
