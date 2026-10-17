#pragma once

#include <hongtu/window/event_pump.hpp>

#include <deque>
#include <mutex>

namespace hongtu::cloud {

/// `EventPump` driven by externally-enqueued events.
///
/// In a cloud_server scenario:
///   1. WebSocket onMessage handler parses JSON like
///      `{"type":"mouseDown","x":100,"y":200,"button":"left"}`
///   2. Translates to `hongtu::window::InputEvent`
///   3. Calls `pump.enqueue(ev)` (thread-safe)
///   4. Engine main loop calls `pump.poll(out)` — gets events in FIFO order
///
/// Thread-safety: `enqueue` and `poll` may be called from different threads
/// (worker thread vs render thread) — internal mutex guards.
class NetworkEventPump : public hongtu::window::EventPump {
 public:
  /// Push an event onto the queue. Returns false if pump has been closed
  /// (currently always returns true — close support reserved for future).
  bool enqueue(hongtu::window::InputEvent ev);

  /// Pull one event. Returns false if queue is empty.
  bool poll(hongtu::window::InputEvent& out) override;

  std::size_t pending() const;
  void        clear();

 private:
  mutable std::mutex                          mtx_;
  std::deque<hongtu::window::InputEvent>     queue_;
};

}  // namespace hongtu::cloud
