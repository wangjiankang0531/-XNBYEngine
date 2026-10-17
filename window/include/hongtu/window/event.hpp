#pragma once

#include <cstdint>
#include <string>

namespace hongtu::window {

/// Event kind. Values intentionally decoupled from SDL_EventType so the
/// engine doesn't leak the SDL2 vocabulary to network / Qt / future
/// backends. Each adapter (SdlEventPump / NetworkEventPump / etc) maps
/// its native source to this set.
enum class EventKind {
  None,
  Quit,
  KeyDown,
  KeyUp,
  MouseDown,
  MouseUp,
  MouseMove,
  MouseWheel,
  Resize,
  Text,        // raw text input (utf-8); future SDF-font input use
};

enum class MouseButton : uint8_t {
  Left   = 0,
  Middle = 1,
  Right  = 2,
};

/// Symbolic key code. Values are stable across SDL versions and any
/// alternative input source (cloud WebSocket / Qt / future backends).
enum class Key : uint16_t {
  Unknown = 0,
  Escape, Return, Space, Tab, Backspace,
  ArrowUp, ArrowDown, ArrowLeft, ArrowRight,
  // Letters
  A, B, C, D, E, F, G, H, I, J, K, L, M,
  N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  // Digits
  Digit0, Digit1, Digit2, Digit3, Digit4,
  Digit5, Digit6, Digit7, Digit8, Digit9,
  // Function
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  // Modifiers (also reported as KeyMod bits when present in `mods`)
  LCtrl, RCtrl, LShift, RShift, LAlt, RAlt,
};

/// Bitmask of modifier keys held when an event occurred.
enum KeyMod : uint16_t {
  ModNone  = 0,
  ModCtrl  = 1 << 0,
  ModShift = 1 << 1,
  ModAlt   = 1 << 2,
};

/// One input event in engine-native format. Every field is optional —
/// only those relevant to `kind` are meaningful. Adapters MUST zero-init
/// fields they don't fill.
struct InputEvent {
  EventKind  kind = EventKind::None;
  // Mouse
  int          mouse_x     = 0;
  int          mouse_y     = 0;
  int          wheel_delta = 0;
  MouseButton  button      = MouseButton::Left;
  // Keyboard
  Key          key  = Key::Unknown;
  uint16_t     mods = ModNone;
  // Resize
  int          new_w = 0;
  int          new_h = 0;
  // Text input (utf-8)
  std::string  text;
};

}  // namespace hongtu::window
