#ifdef USE_SDL
#include "sdl3_input_state.hpp"
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>

namespace LX_infra {

using LX_core::KeyCode;
using LX_core::MouseButton;
using LX_core::Vec2f;

static KeyCode mapSdlScancode(SDL_Scancode sc) {
  switch (sc) {
  case SDL_SCANCODE_A: return KeyCode::A;
  case SDL_SCANCODE_B: return KeyCode::B;
  case SDL_SCANCODE_C: return KeyCode::C;
  case SDL_SCANCODE_D: return KeyCode::D;
  case SDL_SCANCODE_E: return KeyCode::E;
  case SDL_SCANCODE_F: return KeyCode::F;
  case SDL_SCANCODE_G: return KeyCode::G;
  case SDL_SCANCODE_H: return KeyCode::H;
  case SDL_SCANCODE_I: return KeyCode::I;
  case SDL_SCANCODE_J: return KeyCode::J;
  case SDL_SCANCODE_K: return KeyCode::K;
  case SDL_SCANCODE_L: return KeyCode::L;
  case SDL_SCANCODE_M: return KeyCode::M;
  case SDL_SCANCODE_N: return KeyCode::N;
  case SDL_SCANCODE_O: return KeyCode::O;
  case SDL_SCANCODE_P: return KeyCode::P;
  case SDL_SCANCODE_Q: return KeyCode::Q;
  case SDL_SCANCODE_R: return KeyCode::R;
  case SDL_SCANCODE_S: return KeyCode::S;
  case SDL_SCANCODE_T: return KeyCode::T;
  case SDL_SCANCODE_U: return KeyCode::U;
  case SDL_SCANCODE_V: return KeyCode::V;
  case SDL_SCANCODE_W: return KeyCode::W;
  case SDL_SCANCODE_X: return KeyCode::X;
  case SDL_SCANCODE_Y: return KeyCode::Y;
  case SDL_SCANCODE_Z: return KeyCode::Z;
  case SDL_SCANCODE_0: return KeyCode::Num0;
  case SDL_SCANCODE_1: return KeyCode::Num1;
  case SDL_SCANCODE_2: return KeyCode::Num2;
  case SDL_SCANCODE_3: return KeyCode::Num3;
  case SDL_SCANCODE_4: return KeyCode::Num4;
  case SDL_SCANCODE_5: return KeyCode::Num5;
  case SDL_SCANCODE_6: return KeyCode::Num6;
  case SDL_SCANCODE_7: return KeyCode::Num7;
  case SDL_SCANCODE_8: return KeyCode::Num8;
  case SDL_SCANCODE_9: return KeyCode::Num9;
  case SDL_SCANCODE_ESCAPE: return KeyCode::Escape;
  case SDL_SCANCODE_SPACE:  return KeyCode::Space;
  case SDL_SCANCODE_LSHIFT: return KeyCode::LShift;
  case SDL_SCANCODE_RSHIFT: return KeyCode::RShift;
  case SDL_SCANCODE_LCTRL:  return KeyCode::LCtrl;
  case SDL_SCANCODE_RCTRL:  return KeyCode::RCtrl;
  case SDL_SCANCODE_LALT:   return KeyCode::LAlt;
  case SDL_SCANCODE_RALT:   return KeyCode::RAlt;
  case SDL_SCANCODE_RETURN: return KeyCode::Enter;
  case SDL_SCANCODE_TAB:    return KeyCode::Tab;
  case SDL_SCANCODE_LEFT:   return KeyCode::Left;
  case SDL_SCANCODE_RIGHT:  return KeyCode::Right;
  case SDL_SCANCODE_UP:     return KeyCode::Up;
  case SDL_SCANCODE_DOWN:   return KeyCode::Down;
  case SDL_SCANCODE_F1:     return KeyCode::F1;
  case SDL_SCANCODE_F2:     return KeyCode::F2;
  case SDL_SCANCODE_F3:     return KeyCode::F3;
  case SDL_SCANCODE_F4:     return KeyCode::F4;
  default: return KeyCode::Unknown;
  }
}

static MouseButton mapSdlMouseButton(u8 btn) {
  switch (btn) {
  case SDL_BUTTON_LEFT:   return MouseButton::Left;
  case SDL_BUTTON_RIGHT:  return MouseButton::Right;
  case SDL_BUTTON_MIDDLE: return MouseButton::Middle;
  default: return MouseButton::Count; // sentinel for unmapped
  }
}

bool Sdl3InputState::isKeyDown(KeyCode code) const {
  auto idx = static_cast<usize>(code);
  if (idx >= m_keyDown.size()) return false;
  return m_keyDown[idx];
}

bool Sdl3InputState::isMouseButtonDown(MouseButton button) const {
  auto idx = static_cast<usize>(button);
  if (idx >= m_mouseButtonDown.size()) return false;
  return m_mouseButtonDown[idx];
}

Vec2f Sdl3InputState::getMousePosition() const {
  return m_mousePos;
}

Vec2f Sdl3InputState::getMouseDelta() const {
  return m_mouseDeltaAccum;
}

float Sdl3InputState::getMouseWheelDelta() const {
  return m_wheelDeltaAccum;
}

void Sdl3InputState::nextFrame() {
  m_mouseDeltaAccum = {0.0f, 0.0f};
  m_wheelDeltaAccum = 0.0f;
}

bool Sdl3InputState::handleSdlEvent(const SDL_Event& event) {
  switch (event.type) {
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP: {
    auto kc = mapSdlScancode(event.key.scancode);
    if (kc != KeyCode::Unknown) {
      m_keyDown[static_cast<usize>(kc)] = event.key.down;
    }
    break;
  }
  case SDL_EVENT_MOUSE_MOTION:
    m_mousePos = {event.motion.x, event.motion.y};
    m_mouseDeltaAccum[0] += event.motion.xrel;
    m_mouseDeltaAccum[1] += event.motion.yrel;
    break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP: {
    auto mb = mapSdlMouseButton(event.button.button);
    if (mb != MouseButton::Count) {
      m_mouseButtonDown[static_cast<usize>(mb)] = event.button.down;
    }
    break;
  }
  case SDL_EVENT_MOUSE_WHEEL:
    m_wheelDeltaAccum += event.wheel.y;
    break;
  case SDL_EVENT_QUIT:
    return true;
  default:
    break;
  }
  return false;
}

} // namespace LX_infra

#endif
