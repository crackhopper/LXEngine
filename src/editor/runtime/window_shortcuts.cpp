#include "editor/runtime/window_shortcuts.hpp"

namespace LX_demo::lxe_editor {

void WindowShortcutController::update(LX_core::Window &window,
                                      const LX_core::IInputState &input,
                                      const bool keyboardCaptured) {
  const bool f11Down = input.isKeyDown(LX_core::KeyCode::F11);
  if (!keyboardCaptured && f11Down && !m_f11WasDown) {
    window.setMaximized(!window.getPlacement().maximized);
  }
  m_f11WasDown = f11Down;
}

} // namespace LX_demo::lxe_editor
