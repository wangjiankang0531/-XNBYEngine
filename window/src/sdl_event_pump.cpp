#include <hongtu/window/sdl_event_pump.hpp>

#include <SDL.h>

namespace hongtu::window {

namespace {

Key mapKeycode(SDL_Keycode k) {
  switch (k) {
    case SDLK_ESCAPE:    return Key::Escape;
    case SDLK_RETURN:    return Key::Return;
    case SDLK_KP_ENTER:  return Key::Return;
    case SDLK_SPACE:     return Key::Space;
    case SDLK_TAB:       return Key::Tab;
    case SDLK_BACKSPACE: return Key::Backspace;
    case SDLK_UP:        return Key::ArrowUp;
    case SDLK_DOWN:      return Key::ArrowDown;
    case SDLK_LEFT:      return Key::ArrowLeft;
    case SDLK_RIGHT:     return Key::ArrowRight;
    case SDLK_LCTRL:     return Key::LCtrl;
    case SDLK_RCTRL:     return Key::RCtrl;
    case SDLK_LSHIFT:    return Key::LShift;
    case SDLK_RSHIFT:    return Key::RShift;
    case SDLK_LALT:      return Key::LAlt;
    case SDLK_RALT:      return Key::RAlt;
    default: break;
  }
  if (k >= SDLK_a && k <= SDLK_z) {
    return static_cast<Key>(static_cast<int>(Key::A) + (k - SDLK_a));
  }
  if (k >= SDLK_0 && k <= SDLK_9) {
    return static_cast<Key>(static_cast<int>(Key::Digit0) + (k - SDLK_0));
  }
  if (k >= SDLK_F1 && k <= SDLK_F12) {
    return static_cast<Key>(static_cast<int>(Key::F1) + (k - SDLK_F1));
  }
  return Key::Unknown;
}

uint16_t mapMods(uint16_t sdl_mod) {
  uint16_t out = ModNone;
  if (sdl_mod & KMOD_CTRL)  out |= ModCtrl;
  if (sdl_mod & KMOD_SHIFT) out |= ModShift;
  if (sdl_mod & KMOD_ALT)   out |= ModAlt;
  return out;
}

MouseButton mapButton(uint8_t b) {
  switch (b) {
    case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
    case SDL_BUTTON_RIGHT:  return MouseButton::Right;
    case SDL_BUTTON_LEFT:
    default:                return MouseButton::Left;
  }
}

}  // namespace

bool translateSdlEvent(const SDL_Event& in, InputEvent& out) {
  out = InputEvent{};
  switch (in.type) {
    case SDL_QUIT:
      out.kind = EventKind::Quit;
      return true;
    case SDL_KEYDOWN:
      out.kind = EventKind::KeyDown;
      out.key  = mapKeycode(in.key.keysym.sym);
      out.mods = mapMods(in.key.keysym.mod);
      return true;
    case SDL_KEYUP:
      out.kind = EventKind::KeyUp;
      out.key  = mapKeycode(in.key.keysym.sym);
      out.mods = mapMods(in.key.keysym.mod);
      return true;
    case SDL_MOUSEBUTTONDOWN:
      out.kind    = EventKind::MouseDown;
      out.mouse_x = in.button.x;
      out.mouse_y = in.button.y;
      out.button  = mapButton(in.button.button);
      return true;
    case SDL_MOUSEBUTTONUP:
      out.kind    = EventKind::MouseUp;
      out.mouse_x = in.button.x;
      out.mouse_y = in.button.y;
      out.button  = mapButton(in.button.button);
      return true;
    case SDL_MOUSEMOTION:
      out.kind    = EventKind::MouseMove;
      out.mouse_x = in.motion.x;
      out.mouse_y = in.motion.y;
      return true;
    case SDL_MOUSEWHEEL:
      out.kind        = EventKind::MouseWheel;
      out.wheel_delta = in.wheel.y;
      return true;
    case SDL_WINDOWEVENT:
      if (in.window.event == SDL_WINDOWEVENT_RESIZED ||
          in.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        out.kind  = EventKind::Resize;
        out.new_w = in.window.data1;
        out.new_h = in.window.data2;
        return true;
      }
      return false;
    case SDL_TEXTINPUT:
      out.kind = EventKind::Text;
      out.text = in.text.text;
      return true;
    default:
      return false;
  }
}

bool SdlEventPump::poll(InputEvent& out) {
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    if (translateSdlEvent(ev, out)) return true;
    // unmapped event — keep polling for the next mappable one
  }
  return false;
}

}  // namespace hongtu::window
