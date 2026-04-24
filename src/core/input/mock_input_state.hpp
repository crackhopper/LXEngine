#pragma once
#include "core/input/input_state.hpp"
#include <array>

namespace LX_core {

class MockInputState : public IInputState {
public:
  void setKeyDown(KeyCode k, bool down) {
    const auto index = static_cast<usize>(k);
    if (index >= m_keys.size()) {
      return;
    }
    m_keys[index] = down;
  }
  void setMouseButtonDown(MouseButton b, bool down) {
    const auto index = static_cast<usize>(b);
    if (index >= m_buttons.size()) {
      return;
    }
    m_buttons[index] = down;
  }
  void setMousePosition(Vec2f p) { m_pos = p; }
  void setMouseDelta(Vec2f d) { m_delta = d; }
  void setMouseWheelDelta(float w) { m_wheel = w; }
  void setUiCapturingMouse(bool capturing) { m_uiCaptureMouse = capturing; }
  void setUiCapturingKeyboard(bool capturing) { m_uiCaptureKeyboard = capturing; }

  bool isKeyDown(KeyCode k) const override {
    const auto index = static_cast<usize>(k);
    return index < m_keys.size() ? m_keys[index] : false;
  }
  bool isMouseButtonDown(MouseButton b) const override {
    const auto index = static_cast<usize>(b);
    return index < m_buttons.size() ? m_buttons[index] : false;
  }
  Vec2f getMousePosition() const override { return m_pos; }
  Vec2f getMouseDelta() const override { return m_delta; }
  float getMouseWheelDelta() const override { return m_wheel; }
  bool isUiCapturingMouse() const override { return m_uiCaptureMouse; }
  bool isUiCapturingKeyboard() const override { return m_uiCaptureKeyboard; }

  void nextFrame() override {
    m_delta = {0.0f, 0.0f};
    m_wheel = 0.0f;
  }

private:
  std::array<bool, static_cast<usize>(KeyCode::Count)> m_keys{};
  std::array<bool, static_cast<usize>(MouseButton::Count)> m_buttons{};
  Vec2f m_pos{0.0f, 0.0f};
  Vec2f m_delta{0.0f, 0.0f};
  float m_wheel = 0.0f;
  bool m_uiCaptureMouse = false;
  bool m_uiCaptureKeyboard = false;
};

} // namespace LX_core
