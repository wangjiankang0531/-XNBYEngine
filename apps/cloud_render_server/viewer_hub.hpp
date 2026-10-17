// ViewerHub — WebRTC viewer sessions for the cloud-render server.
//
// One PeerConnection per browser tab: an H.264 SendOnly track fed from the
// VTEncoder's Annex-B output, plus an "input" datachannel carrying camera
// control JSON back from the page.
//
// Signaling direction: the SERVER creates the offer (same as libdatachannel's
// streamer example) — that way our H264/PT96 m-line and the datachannel are
// authoritative and the browser just answers. (Browser-initiated offers broke
// on m-line order/codec reconciliation.) No trickle — we wait for ICE
// gathering so the offer carries all candidates; plenty on a LAN.
#pragma once

#include <rtc/rtc.hpp>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace crs {

class ViewerHub {
 public:
  using InputHandler    = std::function<void(const nlohmann::json&)>;
  using KeyframeRequest = std::function<void()>;

  void setInputHandler(InputHandler h) { on_input_ = std::move(h); }
  void setKeyframeRequest(KeyframeRequest h) { request_keyframe_ = std::move(h); }

  /// 注入 STUN/TURN 服务器:跨网段/NAT/防火墙隔离时,实例侧才能生成
  /// srflx/relay 候选,浏览器才连得上(否则只有内网 host 候选)。
  void setIceServers(std::vector<rtc::IceServer> servers) { ice_servers_ = std::move(servers); }

  /// 强制只用 TURN 中继候选(诊断/完全隔离环境);需先 setIceServers 配 TURN。
  void setRelayOnly(bool on) { relay_only_ = on; }

  /// Create a viewer session: server-side offer with the H264 track + the
  /// "input" datachannel. Blocks until ICE gathering completes (≤5 s).
  /// Returns {session_id, offer_sdp}; id 0 = failure.
  /// can_control=false 时这一路只读:input datachannel 的相机指令被忽略(观看模式)。
  std::pair<uint64_t, std::string> createSession(bool can_control = true) {
    auto viewer = std::make_shared<Viewer>();
    viewer->can_control = can_control;

    rtc::Configuration cfg;
    cfg.iceServers = ice_servers_;  // 空 = 仅 host 候选(同网段/localhost)
    if (relay_only_) cfg.iceTransportPolicy = rtc::TransportPolicy::Relay;
    viewer->pc = std::make_shared<rtc::PeerConnection>(cfg);

    // 候选日志注册必须最先做:createDataChannel() 会立刻触发自动协商,
    // loopback 上 gathering 毫秒级完成,晚注册的回调会整个错过(实测竞态)。
    viewer->pc->onLocalCandidate([](rtc::Candidate cand) {
      spdlog::debug("[hub] local candidate: {}", std::string(cand));
    });

    const uint32_t ssrc = 42;
    rtc::Description::Video media("video", rtc::Description::Direction::SendOnly);
    media.addH264Codec(96);
    media.addSSRC(ssrc, "video-stream");
    viewer->track = viewer->pc->addTrack(media);

    viewer->rtp_cfg = std::make_shared<rtc::RtpPacketizationConfig>(
        ssrc, "video-stream", 96, rtc::H264RtpPacketizer::ClockRate);
    auto packetizer = std::make_shared<rtc::H264RtpPacketizer>(
        rtc::NalUnit::Separator::LongStartSequence, viewer->rtp_cfg);
    packetizer->addToChain(std::make_shared<rtc::RtcpSrReporter>(viewer->rtp_cfg));
    packetizer->addToChain(std::make_shared<rtc::RtcpNackResponder>());
    viewer->track->setMediaHandler(packetizer);

    std::weak_ptr<Viewer> wv = viewer;
    viewer->track->onOpen([this, wv] {
      if (auto v = wv.lock()) {
        v->open = true;
        spdlog::info("[hub] video track open — forcing keyframe");
        if (request_keyframe_) request_keyframe_();
      }
    });

    // Server-created datachannel — browser receives it via ondatachannel.
    viewer->input = viewer->pc->createDataChannel("input");
    viewer->input->onMessage([this, wv](rtc::message_variant msg) {
      auto v = wv.lock();
      if (!v || !v->can_control) return;  // 只读 viewer:忽略相机指令
      if (!std::holds_alternative<std::string>(msg) || !on_input_) return;
      try {
        on_input_(nlohmann::json::parse(std::get<std::string>(msg)));
      } catch (const std::exception& e) {
        spdlog::warn("[hub] bad input message: {}", e.what());
      }
    });

    viewer->pc->onStateChange([this, wv](rtc::PeerConnection::State s) {
      if (s == rtc::PeerConnection::State::Closed ||
          s == rtc::PeerConnection::State::Failed ||
          s == rtc::PeerConnection::State::Disconnected) {
        if (auto v = wv.lock()) v->open = false;
        spdlog::info("[hub] peer connection ended");
      }
    });

    // Non-trickle: the offer must carry the gathered candidates. Gathering
    // already started when createDataChannel() auto-negotiated, and on
    // loopback it completes in milliseconds — callback-based waiting races
    // registration (bit us in production), so POLL the gathering state.
    try {
      viewer->pc->setLocalDescription();  // no-op if auto-negotiation已生成
    } catch (const std::exception& e) {
      spdlog::error("[hub] offer creation failed: {}", e.what());
      return {0, {}};
    }
    bool complete = false;
    for (int i = 0; i < 100; ++i) {  // ≤5 s
      if (viewer->pc->gatheringState() == rtc::PeerConnection::GatheringState::Complete) {
        complete = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!complete) {
      // VPN/utun 接口可能拖慢 Complete;有 description 就继续(host 候选
      // 早就有了),一个候选都没有的极端情况由浏览器连接失败兜底。
      spdlog::warn("[hub] ICE gathering still incomplete after 5s — proceeding anyway");
    }
    auto desc = viewer->pc->localDescription();
    if (!desc) return {0, {}};

    uint64_t id = 0;
    {
      std::lock_guard<std::mutex> lock(m_);
      // Drop dead sessions; every new page load is a new viewer.
      viewers_.erase(std::remove_if(viewers_.begin(), viewers_.end(),
                                    [](const std::shared_ptr<Viewer>& v) {
                                      return v->pc->state() == rtc::PeerConnection::State::Closed ||
                                             v->pc->state() == rtc::PeerConnection::State::Failed;
                                    }),
                     viewers_.end());
      id = next_id_++;
      viewer->id = id;
      viewers_.push_back(viewer);
    }
    spdlog::info("[hub] session {} created ({} total)", id, viewerCount());
    return {id, std::string(*desc)};
  }

  /// Apply the browser's SDP answer to the session.
  bool acceptAnswer(uint64_t id, const std::string& answer_sdp) {
    std::shared_ptr<Viewer> viewer;
    {
      std::lock_guard<std::mutex> lock(m_);
      for (auto& v : viewers_) {
        if (v->id == id) { viewer = v; break; }
      }
    }
    if (!viewer) return false;
    try {
      viewer->pc->setRemoteDescription(rtc::Description(answer_sdp, "answer"));
    } catch (const std::exception& e) {
      spdlog::error("[hub] answer rejected: {}", e.what());
      return false;
    }
    return true;
  }

  /// Push one encoded Annex-B frame to all open viewers.
  /// Called from the VideoToolbox callback thread — libdatachannel send is
  /// thread-safe.
  void sendFrame(const std::vector<uint8_t>& annexb, bool /*keyframe*/, uint64_t pts_90khz) {
    std::lock_guard<std::mutex> lock(m_);
    for (auto& v : viewers_) {
      if (!v->open || !v->track || !v->track->isOpen()) continue;
      v->rtp_cfg->timestamp = v->rtp_cfg->startTimestamp + static_cast<uint32_t>(pts_90khz);
      try {
        v->track->send(reinterpret_cast<const std::byte*>(annexb.data()), annexb.size());
      } catch (const std::exception& e) {
        spdlog::warn("[hub] send failed: {}", e.what());
        v->open = false;
      }
    }
  }

  /// Broadcast a text (JSON) message to every viewer's input datachannel.
  /// Enables request/response over the control channel (e.g. screen-pick
  /// results) — the requesting page correlates replies by an echoed id.
  void sendInput(const std::string& text) {
    std::lock_guard<std::mutex> lock(m_);
    for (auto& v : viewers_) {
      if (v->input && v->input->isOpen()) {
        try {
          v->input->send(text);
        } catch (const std::exception& e) {
          spdlog::warn("[hub] input send failed: {}", e.what());
        }
      }
    }
  }

  bool hasViewer() {
    std::lock_guard<std::mutex> lock(m_);
    for (auto& v : viewers_) {
      if (v->open) return true;
    }
    return false;
  }

  size_t viewerCount() {
    std::lock_guard<std::mutex> lock(m_);
    return viewers_.size();
  }

  size_t openViewerCount() {
    std::lock_guard<std::mutex> lock(m_);
    size_t n = 0;
    for (auto& v : viewers_) {
      if (v->open) ++n;
    }
    return n;
  }

 private:
  struct Viewer {
    uint64_t                                      id = 0;
    std::shared_ptr<rtc::PeerConnection>          pc;
    std::shared_ptr<rtc::Track>                   track;
    std::shared_ptr<rtc::RtpPacketizationConfig>  rtp_cfg;
    std::shared_ptr<rtc::DataChannel>             input;
    std::atomic<bool>                             open{false};
    bool                                          can_control = true;
  };

  std::mutex                           m_;
  uint64_t                             next_id_ = 1;
  std::vector<std::shared_ptr<Viewer>> viewers_;
  std::vector<rtc::IceServer>          ice_servers_;
  bool                                 relay_only_ = false;
  InputHandler                         on_input_;
  KeyframeRequest                      request_keyframe_;
};

}  // namespace crs
