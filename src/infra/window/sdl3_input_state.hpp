#pragma once
#ifdef USE_SDL

#include "core/input/input_state.hpp"
#include <SDL3/SDL_events.h>
#include <array>

namespace LX_infra {

class Sdl3InputState : public LX_core::IInputState {
public:
  bool isKeyDown(LX_core::KeyCode code) const override;
  bool isMouseButtonDown(LX_core::MouseButton button) const override;
  LX_core::Vec2f getMousePosition() const override;
  LX_core::Vec2f getMouseDelta() const override;
  float getMouseWheelDelta() const override;
  void nextFrame() override;

  /// Consume one SDL event and update internal state.
  /// Returns true if a quit event was received.
  bool handleSdlEvent(const SDL_Event& event);

private:
  std::array<bool, static_cast<usize>(LX_core::KeyCode::Count)> m_keyDown{};
  std::array<bool, static_cast<usize>(LX_core::MouseButton::Count)> m_mouseButtonDown{};
  LX_core::Vec2f m_mousePos{0.0f, 0.0f};
  LX_core::Vec2f m_mouseDeltaAccum{0.0f, 0.0f};
  float m_wheelDeltaAccum = 0.0f;
};

} // namespace LX_infra

#endif
