#include "core/input/dummy_input_state.hpp"
#include "core/input/key_code.hpp"
#include "core/input/mock_input_state.hpp"
#include "core/input/mouse_button.hpp"
#include "core/utils/env.hpp"

#include <iostream>

using namespace LX_core;

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

void testDummyKeyDown() {
  DummyInputState input;
  EXPECT(!input.isKeyDown(KeyCode::W), "DummyInputState key W must be false");
  EXPECT(!input.isKeyDown(KeyCode::Escape), "DummyInputState key Escape must be false");
  EXPECT(!input.isKeyDown(KeyCode::Space), "DummyInputState key Space must be false");
}

void testDummyMouseButton() {
  DummyInputState input;
  EXPECT(!input.isMouseButtonDown(MouseButton::Left), "DummyInputState mouse left must be false");
  EXPECT(!input.isMouseButtonDown(MouseButton::Right), "DummyInputState mouse right must be false");
  EXPECT(!input.isMouseButtonDown(MouseButton::Middle), "DummyInputState mouse middle must be false");
}

void testDummyMousePosition() {
  DummyInputState input;
  auto pos = input.getMousePosition();
  EXPECT(pos[0] == 0.0f && pos[1] == 0.0f, "DummyInputState mouse position must be {0,0}");
}

void testDummyMouseDelta() {
  DummyInputState input;
  auto delta = input.getMouseDelta();
  EXPECT(delta[0] == 0.0f && delta[1] == 0.0f, "DummyInputState mouse delta must be {0,0}");
}

void testDummyWheelDelta() {
  DummyInputState input;
  EXPECT(input.getMouseWheelDelta() == 0.0f, "DummyInputState wheel delta must be 0");
}

void testDummyNextFrame() {
  DummyInputState input;
  input.nextFrame();
  EXPECT(!input.isKeyDown(KeyCode::A), "After nextFrame, key A must still be false");
  EXPECT(input.getMouseWheelDelta() == 0.0f, "After nextFrame, wheel delta must still be 0");
  auto pos = input.getMousePosition();
  EXPECT(pos[0] == 0.0f && pos[1] == 0.0f, "After nextFrame, mouse pos must still be {0,0}");
}

void testEnumBoundaries() {
  EXPECT(static_cast<uint8_t>(MouseButton::Count) == 3, "MouseButton::Count must be 3");
  EXPECT(static_cast<uint16_t>(KeyCode::Count) > 0, "KeyCode::Count must be positive");
  EXPECT(static_cast<uint16_t>(KeyCode::Unknown) == 0, "KeyCode::Unknown must be 0");
}

void testMockInputStateGuardsSentinels() {
  MockInputState input;

  input.setKeyDown(KeyCode::Count, true);
  input.setMouseButtonDown(MouseButton::Count, true);

  EXPECT(!input.isKeyDown(KeyCode::Count),
         "MockInputState must ignore KeyCode::Count sentinel");
  EXPECT(!input.isMouseButtonDown(MouseButton::Count),
         "MockInputState must ignore MouseButton::Count sentinel");
}

} // namespace

int main() {
  expSetEnvVK();
  testDummyKeyDown();
  testDummyMouseButton();
  testDummyMousePosition();
  testDummyMouseDelta();
  testDummyWheelDelta();
  testDummyNextFrame();
  testEnumBoundaries();
  testMockInputStateGuardsSentinels();

  if (failures == 0) {
    std::cout << "[PASS] All input state tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed.\n";
  }
  return failures == 0 ? 0 : 1;
}
