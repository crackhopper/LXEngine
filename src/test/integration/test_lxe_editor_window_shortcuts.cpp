#include "core/input/mock_input_state.hpp"
#include "core/platform/window.hpp"
#include "editor/runtime/window_shortcuts.hpp"

#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace demo = LX_demo::lxe_editor;

namespace {

bool g_failed = false;

void expect(bool condition, const char *message) {
  if (!condition) {
    g_failed = true;
    std::cerr << "[FAIL] " << message << "\n";
  }
}

class FakeWindow final : public LX_core::Window {
public:
  int getWidth() const override { return m_placement.width; }
  int getHeight() const override { return m_placement.height; }
  void updateSize(bool *closed, int *width, int *height) override {
    *closed = false;
    *width = getWidth();
    *height = getHeight();
  }
  void getRequiredExtensions(std::vector<const char *> &) const override {}
  LX_core::WindowGraphicsHandle
  createGraphicsHandle(GraphicsAPI,
                       LX_core::GraphicsInstanceHandle) const override {
    return nullptr;
  }
  void destroyGraphicsHandle(GraphicsAPI,
                             LX_core::GraphicsInstanceHandle,
                             LX_core::WindowGraphicsHandle) const override {}
  LX_core::InputStateSharedPtr getInputState() const override {
    return m_input;
  }
  LX_core::WindowPlacement getPlacement() const override {
    return m_placement;
  }
  LX_core::WindowUsableBounds getUsableBounds() const override {
    return LX_core::WindowUsableBounds{.x = 0,
                                       .y = 0,
                                       .width = 1920,
                                       .height = 1080};
  }
  void applyPlacement(const LX_core::WindowPlacement &placement) override {
    m_placement = placement;
    ++m_applyPlacementCount;
  }
  void setMaximized(bool maximized) override {
    m_placement.maximized = maximized;
    ++m_setMaximizedCount;
  }
  void *getNativeHandle() const override { return nullptr; }
  void onClose(std::function<bool()>) override {}
  bool shouldClose() override { return false; }

  LX_core::MockInputState &input() { return *m_input; }
  int setMaximizedCount() const { return m_setMaximizedCount; }

private:
  std::shared_ptr<LX_core::MockInputState> m_input =
      std::make_shared<LX_core::MockInputState>();
  LX_core::WindowPlacement m_placement{
      .x = 100, .y = 80, .width = 1280, .height = 720, .maximized = false};
  int m_applyPlacementCount = 0;
  int m_setMaximizedCount = 0;
};

void testF11TogglesMaximizeAndRestoreOnKeyEdges() {
  FakeWindow window;
  demo::WindowShortcutController shortcuts;

  window.input().setKeyDown(LX_core::KeyCode::F11, true);
  shortcuts.update(window, window.input(), false);
  expect(window.getPlacement().maximized,
         "F11 key down should maximize a restored window");
  expect(window.setMaximizedCount() == 1,
         "F11 should toggle once on the first key-down edge");

  shortcuts.update(window, window.input(), false);
  expect(window.getPlacement().maximized,
         "holding F11 should not immediately restore the window");
  expect(window.setMaximizedCount() == 1,
         "holding F11 should not repeatedly toggle");

  window.input().setKeyDown(LX_core::KeyCode::F11, false);
  shortcuts.update(window, window.input(), false);
  window.input().setKeyDown(LX_core::KeyCode::F11, true);
  shortcuts.update(window, window.input(), false);
  expect(!window.getPlacement().maximized,
         "second F11 key-down edge should restore a maximized window");
  expect(window.setMaximizedCount() == 2,
         "second F11 key-down edge should toggle exactly once");
}

void testF11DoesNotToggleWhenKeyboardIsCaptured() {
  FakeWindow window;
  demo::WindowShortcutController shortcuts;

  window.input().setKeyDown(LX_core::KeyCode::F11, true);
  shortcuts.update(window, window.input(), true);
  expect(!window.getPlacement().maximized,
         "F11 should not toggle while UI captures keyboard input");
  expect(window.setMaximizedCount() == 0,
         "keyboard capture should suppress the F11 shortcut");
}

} // namespace

int main() {
  testF11TogglesMaximizeAndRestoreOnKeyEdges();
  testF11DoesNotToggleWhenKeyboardIsCaptured();
  return g_failed ? 1 : 0;
}
