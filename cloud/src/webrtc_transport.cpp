#include <hongtu/cloud/webrtc_transport.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace hongtu::cloud {

SessionId WebRTCTransport::createSession(const SessionConfig& config) {
  session_counter_++;

  // Generate a unique session ID
  std::ostringstream oss;
  oss << "session-" << session_counter_;
  SessionId id = oss.str();

  WebRTCSession session;
  session.id = id;
  session.state = SessionState::Connecting;
  session.config = config;
  session.stats = {};
  session.connect_time = std::chrono::steady_clock::now();
  session.last_keyframe_time = 0.0;

  sessions_[id] = std::move(session);
  return id;
}

SDPMessage WebRTCTransport::processOffer(const SessionId& id, const SDPMessage& offer) {
  SDPMessage answer;
  answer.session_id = id;
  answer.type = "answer";

  auto it = sessions_.find(id);
  if (it == sessions_.end()) {
    answer.sdp = "";
    return answer;
  }

  // Generate a simulated SDP answer based on the offer
  // In a real implementation, this would go through the WebRTC stack
  std::ostringstream sdp;
  sdp << "v=0\r\n";
  sdp << "o=- " << session_counter_ << " 2 IN IP4 127.0.0.1\r\n";
  sdp << "s=XNBY Cloud Render\r\n";
  sdp << "t=0 0\r\n";
  sdp << "a=group:BUNDLE 0\r\n";
  sdp << "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n";
  sdp << "c=IN IP4 0.0.0.0\r\n";
  sdp << "a=rtpmap:96 H264/90000\r\n";
  sdp << "a=fmtp:96 profile-level-id=42e01f;level-asymmetry-allowed=1\r\n";
  sdp << "a=rtcp-fb:96 nack\r\n";
  sdp << "a=rtcp-fb:96 nack pli\r\n";
  sdp << "a=sendonly\r\n";
  sdp << "a=mid:0\r\n";
  sdp << "a=setup:active\r\n";

  answer.sdp = sdp.str();
  return answer;
}

void WebRTCTransport::addICECandidate(const SessionId& id, const ICECandidate& candidate) {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) return;

  // In a real implementation, this would be passed to the ICE agent
  // For the framework, we just acknowledge receipt
  (void)candidate;
}

bool WebRTCTransport::connectSession(const SessionId& id) {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) return false;

  if (it->second.state == SessionState::Connecting) {
    it->second.state = SessionState::Connected;
    it->second.connect_time = std::chrono::steady_clock::now();
    it->second.stats = {};  // Reset stats on connect
    return true;
  }
  return false;
}

void WebRTCTransport::disconnectSession(const SessionId& id) {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) return;

  it->second.state = SessionState::Disconnected;
}

bool WebRTCTransport::sendFrame(const SessionId& id, const VideoFrame& frame) {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) return false;
  if (it->second.state != SessionState::Connected) return false;

  WebRTCSession& session = it->second;

  // Check bandwidth: estimate if we can send this frame
  f64 frame_bits = static_cast<f64>(frame.size) * 8.0;
  f64 max_bits_per_frame =
      static_cast<f64>(session.config.max_bitrate_kbps) * 1000.0 /
      static_cast<f64>(session.config.target_fps);

  if (frame_bits > max_bits_per_frame * 2.0) {
    // Frame too large, drop it
    session.stats.frames_dropped++;
    return false;
  }

  // Update stats
  session.stats.frames_sent++;
  session.stats.bytes_sent += frame.size;

  if (frame.is_keyframe) {
    session.last_keyframe_time = frame.timestamp;
  }

  return true;
}

std::vector<InputCommand> WebRTCTransport::getInputCommands(const SessionId& id) {
  std::vector<InputCommand> commands;

  auto it = sessions_.find(id);
  if (it == sessions_.end()) return commands;

  while (!it->second.input_queue.empty()) {
    commands.push_back(it->second.input_queue.front());
    it->second.input_queue.pop();
  }

  return commands;
}

void WebRTCTransport::pushInputCommand(const SessionId& id, const InputCommand& cmd) {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) return;
  if (it->second.state != SessionState::Connected) return;

  it->second.input_queue.push(cmd);
  it->second.stats.bytes_received += cmd.payload.size() + 16;  // Estimate command size
}

void WebRTCTransport::update(f64 delta_time) {
  for (auto& [id, session] : sessions_) {
    if (session.state != SessionState::Connected) continue;

    // Update uptime
    session.stats.uptime_sec += delta_time;

    // Simulate RTT measurement (in real impl, use RTCP receiver reports)
    // Model RTT with slight jitter
    if (session.stats.rtt_ms == 0.0) {
      session.stats.rtt_ms = 20.0;  // Initial estimate: 20ms
    }
    // Smooth RTT with EMA
    f64 simulated_rtt = 20.0 + std::sin(session.stats.uptime_sec * 0.5) * 5.0;
    session.stats.rtt_ms = session.stats.rtt_ms * 0.9 + simulated_rtt * 0.1;

    // Update jitter estimate
    f64 jitter_sample = std::abs(simulated_rtt - session.stats.rtt_ms);
    session.stats.jitter_ms = session.stats.jitter_ms * 0.9 + jitter_sample * 0.1;

    // Compute bandwidth
    if (session.stats.uptime_sec > 0.0) {
      session.stats.bandwidth_kbps =
          static_cast<f64>(session.stats.bytes_sent * 8) / (session.stats.uptime_sec * 1000.0);
    }
  }
}

const WebRTCSession* WebRTCTransport::getSession(const SessionId& id) const {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) return nullptr;
  return &it->second;
}

std::vector<SessionId> WebRTCTransport::getSessionIds() const {
  std::vector<SessionId> ids;
  ids.reserve(sessions_.size());
  for (const auto& [id, session] : sessions_) {
    ids.push_back(id);
  }
  return ids;
}

u32 WebRTCTransport::getActiveSessionCount() const {
  u32 count = 0;
  for (const auto& [id, session] : sessions_) {
    if (session.state == SessionState::Connected) count++;
  }
  return count;
}

f64 WebRTCTransport::getTotalBandwidthKbps() const {
  f64 total = 0.0;
  for (const auto& [id, session] : sessions_) {
    if (session.state == SessionState::Connected) {
      total += session.stats.bandwidth_kbps;
    }
  }
  return total;
}

bool WebRTCTransport::shouldSendKeyframe(const SessionId& id, f64 current_time) const {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) return false;
  if (it->second.state != SessionState::Connected) return false;

  const WebRTCSession& session = it->second;

  // Send keyframe if enough time has elapsed since the last one
  f64 elapsed = current_time - session.last_keyframe_time;
  return elapsed >= session.config.keyframe_interval_sec;
}

u32 WebRTCTransport::computeAdaptiveBitrate(const SessionId& id) const {
  auto it = sessions_.find(id);
  if (it == sessions_.end()) return 0;

  const WebRTCSession& session = it->second;
  if (!session.config.enable_adaptive_bitrate) {
    return session.config.max_bitrate_kbps;
  }

  // Adaptive bitrate based on network conditions:
  // - High RTT -> reduce bitrate
  // - High packet loss -> reduce bitrate
  // - High jitter -> reduce bitrate
  // - Good conditions -> increase toward max

  f64 rtt_factor = 1.0;
  if (session.stats.rtt_ms > 100.0) {
    rtt_factor = 100.0 / session.stats.rtt_ms;  // Linear reduction
  } else if (session.stats.rtt_ms > 50.0) {
    rtt_factor = 0.9;
  }

  f64 jitter_factor = 1.0;
  if (session.stats.jitter_ms > 20.0) {
    jitter_factor = 0.7;
  } else if (session.stats.jitter_ms > 10.0) {
    jitter_factor = 0.85;
  }

  f64 loss_factor = 1.0;
  if (session.stats.frames_sent > 0) {
    f64 loss_rate = static_cast<f64>(session.stats.packets_lost) /
                    static_cast<f64>(session.stats.frames_sent + session.stats.packets_lost);
    if (loss_rate > 0.05) {
      loss_factor = 0.5;  // Aggressive reduction on high loss
    } else if (loss_rate > 0.01) {
      loss_factor = 0.8;
    }
  }

  f64 combined_factor = rtt_factor * jitter_factor * loss_factor;
  f64 target_kbps =
      static_cast<f64>(session.config.max_bitrate_kbps) * combined_factor;

  // Clamp to configured range
  u32 result = static_cast<u32>(target_kbps);
  result = std::max(result, session.config.min_bitrate_kbps);
  result = std::min(result, session.config.max_bitrate_kbps);

  return result;
}

}  // namespace hongtu::cloud
