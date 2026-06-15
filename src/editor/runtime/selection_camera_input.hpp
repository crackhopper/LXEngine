#pragma once

#include "core/input/input_state.hpp"

namespace LX_demo::lxe_editor {

enum class SelectionNavigationMode { Orbit, FreeFly };

class SelectionCameraInput final : public LX_core::IInputState {
public:
  SelectionCameraInput(const LX_core::IInputState& base,
                       const SelectionNavigationMode mode)
      : m_base(base), m_mode(mode) {}

  bool isKeyDown(const LX_core::KeyCode code) const override {
    if (m_mode != SelectionNavigationMode::FreeFly || !rightHeld()) {
      return false;
    }
    return m_base.isKeyDown(code);
  }

  bool isMouseButtonDown(const LX_core::MouseButton button) const override {
    switch (m_mode) {
    case SelectionNavigationMode::Orbit:
      if (button == LX_core::MouseButton::Left) {
        return rightHeld();
      }
      return false;
    case SelectionNavigationMode::FreeFly:
      if (button == LX_core::MouseButton::Right) {
        return rightHeld();
      }
      return false;
    }
    return false;
  }

  LX_core::Vec2f getMousePosition() const override {
    return m_base.getMousePosition();
  }

  LX_core::Vec2f getMouseDelta() const override {
    return rightHeld() ? m_base.getMouseDelta() : LX_core::Vec2f{0.0f, 0.0f};
  }

  float getMouseWheelDelta() const override {
    return m_mode == SelectionNavigationMode::Orbit ? m_base.getMouseWheelDelta()
                                                    : 0.0f;
  }

  void nextFrame() override {}

  bool isUiCapturingMouse() const override {
    return m_base.isUiCapturingMouse();
  }

  bool isUiCapturingKeyboard() const override {
    return m_base.isUiCapturingKeyboard();
  }

private:
  [[nodiscard]] bool rightHeld() const {
    return m_base.isMouseButtonDown(LX_core::MouseButton::Right);
  }

  const LX_core::IInputState& m_base;
  SelectionNavigationMode m_mode = SelectionNavigationMode::Orbit;
};

} // namespace LX_demo::lxe_editor
