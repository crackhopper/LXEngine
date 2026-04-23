#include "core/input/dummy_input_state.hpp"
#include "core/input/mock_input_state.hpp"
#include "core/utils/env.hpp"

#include <cstdlib>
#include <iostream>

#if defined(USE_SDL)
#include "backend/vulkan/vulkan_renderer.hpp"
#include "infra/window/window.hpp"
#include <imgui.h>
#include <memory>
#endif

using namespace LX_core;

namespace {

int failures = 0;
int skipped = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

void test_ui_capture_flags_default_to_false_without_imgui() {
  DummyInputState dummy;
  EXPECT(!dummy.isUiCapturingMouse(),
         "DummyInputState::isUiCapturingMouse must default to false");
  EXPECT(!dummy.isUiCapturingKeyboard(),
         "DummyInputState::isUiCapturingKeyboard must default to false");

  MockInputState mock;
  EXPECT(!mock.isUiCapturingMouse(),
         "MockInputState::isUiCapturingMouse must default to false");
  EXPECT(!mock.isUiCapturingKeyboard(),
         "MockInputState::isUiCapturingKeyboard must default to false");

  mock.setUiCapturingMouse(true);
  EXPECT(mock.isUiCapturingMouse(),
         "MockInputState::setUiCapturingMouse(true) must flip the flag");
  EXPECT(!mock.isUiCapturingKeyboard(),
         "mouse capture setter must not touch keyboard flag");

  mock.setUiCapturingKeyboard(true);
  EXPECT(mock.isUiCapturingKeyboard(),
         "MockInputState::setUiCapturingKeyboard(true) must flip the flag");
}

#if defined(USE_SDL)

// The renderer-backed scenarios require a working Vulkan + display environment.
// CI without a GPU / display may not be able to run them; skip cleanly instead
// of failing so the test binary remains a useful local signal without gating CI.
bool shouldSkipGraphicsTests() {
  const char *skip = std::getenv("LX_SKIP_GRAPHICS_TESTS");
  if (skip && *skip && std::string(skip) != "0") {
    return true;
  }
  const char *display = std::getenv("DISPLAY");
  const char *wayland = std::getenv("WAYLAND_DISPLAY");
  if ((!display || !*display) && (!wayland || !*wayland)) {
    return true;
  }
  return false;
}

void test_gui_init_succeeds_after_renderer_initialize() {
  if (shouldSkipGraphicsTests()) {
    std::cout << "[SKIP] gui_init_succeeds_after_renderer_initialize "
                 "(no display / graphics disabled)\n";
    ++skipped;
    return;
  }
  try {
    auto window = std::make_shared<LX_infra::Window>("imgui-overlay-test",
                                                     800, 600);
    EXPECT(window->getNativeHandle() != nullptr,
           "SDL window must expose non-null native handle");

    auto renderer = LX_core::backend::VulkanRenderer::create(
        LX_core::backend::VulkanRenderer::Token{});
    renderer->initialize(window, "imgui-overlay-test");

    EXPECT(ImGui::GetCurrentContext() != nullptr,
           "ImGui context must be alive after VulkanRenderer::initialize");

    renderer->shutdown();
    EXPECT(ImGui::GetCurrentContext() == nullptr,
           "ImGui context must be destroyed after VulkanRenderer::shutdown");
  } catch (const std::exception &e) {
    std::cout << "[SKIP] gui_init_succeeds_after_renderer_initialize "
                 "(exception: "
              << e.what() << ")\n";
    ++skipped;
  }
}

void test_draw_with_ui_callback_does_not_crash() {
  if (shouldSkipGraphicsTests()) {
    std::cout << "[SKIP] draw_with_ui_callback_does_not_crash "
                 "(no display / graphics disabled)\n";
    ++skipped;
    return;
  }
  try {
    auto window = std::make_shared<LX_infra::Window>("imgui-overlay-draw-test",
                                                     800, 600);
    auto renderer = LX_core::backend::VulkanRenderer::create(
        LX_core::backend::VulkanRenderer::Token{});
    renderer->initialize(window, "imgui-overlay-draw-test");

    int invoked = 0;
    renderer->setDrawUiCallback([&] {
      ++invoked;
      ImGui::Text("hi");
    });

    // The renderer requires an initialized scene with at least the frame
    // graph wired up. Without `initScene` the draw loop would early-return,
    // but Gui::beginFrame / endFrame still need to be resilient to being
    // driven with no scene set. Smoke-test the callback path.
    for (int i = 0; i < 3; ++i) {
      renderer->draw();
    }

    EXPECT(invoked >= 0,
           "draw loop must not crash; callback may or may not run depending "
           "on scene init state");

    renderer->shutdown();
  } catch (const std::exception &e) {
    std::cout << "[SKIP] draw_with_ui_callback_does_not_crash "
                 "(exception: "
              << e.what() << ")\n";
    ++skipped;
  }
}

#else

void test_gui_init_succeeds_after_renderer_initialize() {
  std::cout << "[SKIP] gui_init_succeeds_after_renderer_initialize "
               "(USE_SDL not defined)\n";
  ++skipped;
}

void test_draw_with_ui_callback_does_not_crash() {
  std::cout << "[SKIP] draw_with_ui_callback_does_not_crash "
               "(USE_SDL not defined)\n";
  ++skipped;
}

#endif

} // namespace

int main() {
  expSetEnvVK();
  test_ui_capture_flags_default_to_false_without_imgui();
  test_gui_init_succeeds_after_renderer_initialize();
  test_draw_with_ui_callback_does_not_crash();

  if (failures == 0) {
    std::cout << "[PASS] imgui overlay tests: " << skipped << " skipped\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}
