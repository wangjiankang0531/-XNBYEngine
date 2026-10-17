// Pure unit tests for translateSdlEvent — construct SDL_Event values by hand
// (no SDL_Init needed) and verify they map to the right InputEvent fields.

#include <gtest/gtest.h>
#include <hongtu/window/sdl_event_pump.hpp>

#include <SDL.h>

using hongtu::window::EventKind;
using hongtu::window::InputEvent;
using hongtu::window::Key;
using hongtu::window::KeyMod;
using hongtu::window::ModCtrl;
using hongtu::window::ModShift;
using hongtu::window::MouseButton;
using hongtu::window::translateSdlEvent;

TEST(SdlEventPumpTest, MapsQuit) {
  SDL_Event in{};
  in.type = SDL_QUIT;
  InputEvent out;
  ASSERT_TRUE(translateSdlEvent(in, out));
  EXPECT_EQ(out.kind, EventKind::Quit);
}

TEST(SdlEventPumpTest, MapsEscapeKeyDown) {
  SDL_Event in{};
  in.type            = SDL_KEYDOWN;
  in.key.keysym.sym  = SDLK_ESCAPE;
  in.key.keysym.mod  = KMOD_NONE;
  InputEvent out;
  ASSERT_TRUE(translateSdlEvent(in, out));
  EXPECT_EQ(out.kind, EventKind::KeyDown);
  EXPECT_EQ(out.key,  Key::Escape);
}

TEST(SdlEventPumpTest, MapsLetterKey) {
  SDL_Event in{};
  in.type           = SDL_KEYDOWN;
  in.key.keysym.sym = SDLK_z;
  InputEvent out;
  ASSERT_TRUE(translateSdlEvent(in, out));
  EXPECT_EQ(out.key, Key::Z);
}

TEST(SdlEventPumpTest, MapsModifiers) {
  SDL_Event in{};
  in.type           = SDL_KEYDOWN;
  in.key.keysym.sym = SDLK_z;
  in.key.keysym.mod = KMOD_LCTRL | KMOD_LSHIFT;
  InputEvent out;
  ASSERT_TRUE(translateSdlEvent(in, out));
  EXPECT_TRUE(out.mods & ModCtrl);
  EXPECT_TRUE(out.mods & ModShift);
}

TEST(SdlEventPumpTest, MapsMouseButtonDown) {
  SDL_Event in{};
  in.type          = SDL_MOUSEBUTTONDOWN;
  in.button.x      = 123;
  in.button.y      = 456;
  in.button.button = SDL_BUTTON_RIGHT;
  InputEvent out;
  ASSERT_TRUE(translateSdlEvent(in, out));
  EXPECT_EQ(out.kind,    EventKind::MouseDown);
  EXPECT_EQ(out.mouse_x, 123);
  EXPECT_EQ(out.mouse_y, 456);
  EXPECT_EQ(out.button,  MouseButton::Right);
}

TEST(SdlEventPumpTest, MapsMouseMotion) {
  SDL_Event in{};
  in.type     = SDL_MOUSEMOTION;
  in.motion.x = 50;
  in.motion.y = 75;
  InputEvent out;
  ASSERT_TRUE(translateSdlEvent(in, out));
  EXPECT_EQ(out.kind,    EventKind::MouseMove);
  EXPECT_EQ(out.mouse_x, 50);
  EXPECT_EQ(out.mouse_y, 75);
}

TEST(SdlEventPumpTest, MapsMouseWheel) {
  SDL_Event in{};
  in.type    = SDL_MOUSEWHEEL;
  in.wheel.y = 3;
  InputEvent out;
  ASSERT_TRUE(translateSdlEvent(in, out));
  EXPECT_EQ(out.kind,        EventKind::MouseWheel);
  EXPECT_EQ(out.wheel_delta, 3);
}

TEST(SdlEventPumpTest, MapsResize) {
  SDL_Event in{};
  in.type         = SDL_WINDOWEVENT;
  in.window.event = SDL_WINDOWEVENT_RESIZED;
  in.window.data1 = 1280;
  in.window.data2 = 720;
  InputEvent out;
  ASSERT_TRUE(translateSdlEvent(in, out));
  EXPECT_EQ(out.kind,  EventKind::Resize);
  EXPECT_EQ(out.new_w, 1280);
  EXPECT_EQ(out.new_h, 720);
}

TEST(SdlEventPumpTest, IgnoresUnmappedEvent) {
  SDL_Event in{};
  in.type = SDL_JOYAXISMOTION;     // not mapped
  InputEvent out;
  EXPECT_FALSE(translateSdlEvent(in, out));
}
