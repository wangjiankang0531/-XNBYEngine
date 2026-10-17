#pragma once

#include <hongtu/window/event.hpp>

namespace hongtu::window {

/// Abstract source of input events. Different backends implement this:
///   - `SdlEventPump`     — wraps SDL_PollEvent (for SDL2-based demos)
///   - `NetworkEventPump` — receives InputEvents from a transport
///                          (cloud_server's WebSocket handler)
///
/// Engine main loops should call `poll(out)` in their event-drain step
/// without knowing which adapter is installed.
class EventPump {
 public:
  virtual ~EventPump() = default;

  /// Pull one event into `out`. Returns false if the queue is empty
  /// (non-blocking).
  virtual bool poll(InputEvent& out) = 0;
};

}  // namespace hongtu::window
