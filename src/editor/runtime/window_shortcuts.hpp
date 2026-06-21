#pragma once

#include "core/input/input_state.hpp"
#include "core/platform/window.hpp"

namespace LX_demo::lxe_editor {

class WindowShortcutController {
public:
  void update(LX_core::Window &window, const LX_core::IInputState &input,
              bool keyboardCaptured);

private:
  bool m_f11WasDown = false;
};

} // namespace LX_demo::lxe_editor
