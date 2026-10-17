#pragma once

#include <hongtu/window/event_pump.hpp>

// Forward-declare SDL types so callers don't need <SDL.h> in headers.
union SDL_Event;

namespace hongtu::window {

/// Pure translation: SDL_Event → InputEvent. Exposed at API surface so
/// tests can hand-construct SDL_Event and verify mapping without
/// initializing SDL's video subsystem.
///
/// Returns true if `in` is a kind we map (and `out` was filled);
/// false for unhandled SDL event types (e.g., joystick / audio events).
bool translateSdlEvent(const SDL_Event& in, InputEvent& out);

/// `EventPump` adapter over `SDL_PollEvent`. Skips events that don't
/// map to an `InputEvent` (returns next mappable one).
class SdlEventPump : public EventPump {
 public:
  bool poll(InputEvent& out) override;
};

}  // namespace hongtu::window
