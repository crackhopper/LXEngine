#include "core/utils/env.hpp"
#ifdef USE_SDL
#include "infra/window/sdl3_input_state.hpp"
#include "core/input/key_code.hpp"
#include "core/input/mouse_button.hpp"

#include <SDL3/SDL.h>
#include <cstring>
#include <iostream>

using namespace LX_core;
using namespace LX_infra;

namespace {

int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

SDL_Event makeKeyEvent(SDL_EventType type, SDL_Scancode scancode, bool down) {
  SDL_Event e{};
  e.type = type;
  e.key.scancode = scancode;
  e.key.down = down;
  return e;
}

SDL_Event makeMouseMotionEvent(float x, float y, float xrel, float yrel) {
  SDL_Event e{};
  e.type = SDL_EVENT_MOUSE_MOTION;
  e.motion.x = x;
  e.motion.y = y;
  e.motion.xrel = xrel;
  e.motion.yrel = yrel;
  return e;
}

SDL_Event makeMouseButtonEvent(SDL_EventType type, u8 button, bool down) {
  SDL_Event e{};
  e.type = type;
  e.button.button = button;
  e.button.down = down;
  return e;
}

SDL_Event makeWheelEvent(float y) {
  SDL_Event e{};
  e.type = SDL_EVENT_MOUSE_WHEEL;
  e.wheel.y = y;
  return e;
}

void testKeyDownUp() {
  Sdl3InputState input;
  EXPECT(!input.isKeyDown(KeyCode::W), "W should start as not pressed");

  input.handleSdlEvent(makeKeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W, true));
  EXPECT(input.isKeyDown(KeyCode::W), "W should be down after key down event");

  input.handleSdlEvent(makeKeyEvent(SDL_EVENT_KEY_UP, SDL_SCANCODE_W, false));
  EXPECT(!input.isKeyDown(KeyCode::W), "W should be up after key up event");
}

void testMouseButtonDownUp() {
  Sdl3InputState input;
  EXPECT(!input.isMouseButtonDown(MouseButton::Left), "Left should start as not pressed");

  input.handleSdlEvent(makeMouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, true));
  EXPECT(input.isMouseButtonDown(MouseButton::Left), "Left should be down after button down");

  input.handleSdlEvent(makeMouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, false));
  EXPECT(!input.isMouseButtonDown(MouseButton::Left), "Left should be up after button up");
}

void testMousePosition() {
  Sdl3InputState input;
  input.handleSdlEvent(makeMouseMotionEvent(100.0f, 200.0f, 10.0f, 20.0f));
  auto pos = input.getMousePosition();
  EXPECT(pos[0] == 100.0f && pos[1] == 200.0f, "Mouse position should be {100, 200}");
}

void testMouseDeltaAccumulation() {
  Sdl3InputState input;
  input.handleSdlEvent(makeMouseMotionEvent(10.0f, 10.0f, 5.0f, 3.0f));
  input.handleSdlEvent(makeMouseMotionEvent(12.0f, 11.0f, 2.0f, 1.0f));
  auto delta = input.getMouseDelta();
  EXPECT(delta[0] == 7.0f && delta[1] == 4.0f, "Mouse delta should accumulate to {7, 4}");
}

void testWheelDeltaAccumulation() {
  Sdl3InputState input;
  input.handleSdlEvent(makeWheelEvent(1.0f));
  input.handleSdlEvent(makeWheelEvent(0.5f));
  EXPECT(input.getMouseWheelDelta() == 1.5f, "Wheel delta should accumulate to 1.5");
}

void testNextFrameClearsDeltaPreservesDown() {
  Sdl3InputState input;
  input.handleSdlEvent(makeKeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_A, true));
  input.handleSdlEvent(makeMouseMotionEvent(50.0f, 50.0f, 10.0f, 20.0f));
  input.handleSdlEvent(makeWheelEvent(2.0f));

  input.nextFrame();

  EXPECT(input.isKeyDown(KeyCode::A), "Key A should still be down after nextFrame");
  auto delta = input.getMouseDelta();
  EXPECT(delta[0] == 0.0f && delta[1] == 0.0f, "Mouse delta should be {0,0} after nextFrame");
  EXPECT(input.getMouseWheelDelta() == 0.0f, "Wheel delta should be 0 after nextFrame");
}

void testQuitEventReturnsTrue() {
  Sdl3InputState input;
  SDL_Event e{};
  e.type = SDL_EVENT_QUIT;
  bool quit = input.handleSdlEvent(e);
  EXPECT(quit, "handleSdlEvent should return true on quit");
}

void testNonQuitEventReturnsFalse() {
  Sdl3InputState input;
  bool quit = input.handleSdlEvent(makeKeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_W, true));
  EXPECT(!quit, "handleSdlEvent should return false on non-quit");
}

void testUnknownScancodeIgnored() {
  Sdl3InputState input;
  // F12 is not mapped in our KeyCode enum
  input.handleSdlEvent(makeKeyEvent(SDL_EVENT_KEY_DOWN, SDL_SCANCODE_F12, true));
  // Should not crash, Unknown key is just ignored
  EXPECT(!input.isKeyDown(KeyCode::Unknown), "Unknown key should not be tracked as down");
}

} // namespace

int main() {
  expSetEnvVK();
  testKeyDownUp();
  testMouseButtonDownUp();
  testMousePosition();
  testMouseDeltaAccumulation();
  testWheelDeltaAccumulation();
  testNextFrameClearsDeltaPreservesDown();
  testQuitEventReturnsTrue();
  testNonQuitEventReturnsFalse();
  testUnknownScancodeIgnored();

  if (failures == 0) {
    std::cout << "[PASS] All SDL input tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed.\n";
  }
  return failures == 0 ? 0 : 1;
}

#else
#include <iostream>
int main() {
  expSetEnvVK();
  std::cout << "[SKIP] SDL input tests skipped (USE_SDL not defined).\n";
  return 0;
}
#endif
