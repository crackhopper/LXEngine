#include "core/input/mock_input_state.hpp"
#include "core/scene/freefly_camera_controller.hpp"
#include "core/utils/env.hpp"

#include <cmath>
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

constexpr float kEps = 1e-2f;

bool approx(float a, float b) { return std::abs(a - b) < kEps; }

void testWKeyMovesForward() {
  // yaw=180, pitch=0 → forward is (0, 0, -1)
  FreeFlyCameraController ctrl({0, 0, 0}, 180.0f, 0.0f);
  Camera cam;
  MockInputState input;

  input.setKeyDown(KeyCode::W, true);
  ctrl.update(cam, input, 1.0f);

  // Should move in -Z direction by moveSpeedPerSecond
  EXPECT(approx(ctrl.getPosition().x, 0.0f), "x should stay ~0");
  EXPECT(approx(ctrl.getPosition().y, 0.0f), "y should stay ~0");
  EXPECT(ctrl.getPosition().z < -3.0f, "z should decrease significantly");
  EXPECT(approx(ctrl.getPosition().z, -ctrl.moveSpeedPerSecond),
         "z should move by -moveSpeedPerSecond");
}

void testDefaultYawFacesTowardOrigin() {
  FreeFlyCameraController ctrl;
  Camera cam;
  MockInputState input;

  ctrl.update(cam, input, 0.016f);

  EXPECT(approx(cam.position.x, 0.0f), "default x should be ~0");
  EXPECT(approx(cam.position.y, 0.0f), "default y should be ~0");
  EXPECT(approx(cam.position.z, 5.0f), "default z should be ~5");
  EXPECT(approx(cam.target.x, 0.0f), "default target.x should be ~0");
  EXPECT(approx(cam.target.y, 0.0f), "default target.y should be ~0");
  EXPECT(cam.target.z < cam.position.z,
         "default target should point toward negative Z");
  EXPECT(approx(cam.target.z, 4.0f),
         "default target should be one unit forward along -Z");
}

void testMouseLookOnlyWithRightButton() {
  FreeFlyCameraController ctrl({0, 0, 0}, 0.0f, 0.0f);
  Camera cam;
  MockInputState input;

  // Without right button — mouse delta should NOT affect yaw/pitch
  input.setMouseDelta({100.0f, 100.0f});
  ctrl.update(cam, input, 0.016f);

  EXPECT(approx(ctrl.getYawDeg(), 0.0f), "yaw should not change without RMB");
  EXPECT(approx(ctrl.getPitchDeg(), 0.0f),
         "pitch should not change without RMB");

  // With right button — mouse delta should affect yaw/pitch
  input.setMouseButtonDown(MouseButton::Right, true);
  input.setMouseDelta({10.0f, 0.0f});
  ctrl.update(cam, input, 0.016f);

  float expectedYaw = 0.0f - 10.0f * ctrl.lookSpeedDegPerPixel;
  EXPECT(approx(ctrl.getYawDeg(), expectedYaw),
         "yaw should change with RMB held");
}

void testDiagonalMovementNormalized() {
  FreeFlyCameraController ctrl({0, 0, 0}, 0.0f, 0.0f);
  Camera cam;
  MockInputState input;

  // Single axis: W only
  input.setKeyDown(KeyCode::W, true);
  ctrl.update(cam, input, 1.0f);
  Vec3f singlePos = ctrl.getPosition();
  float singleDist =
      std::sqrt(singlePos.x * singlePos.x + singlePos.y * singlePos.y +
                singlePos.z * singlePos.z);

  // Reset and try diagonal: W + D
  ctrl.setPosition({0, 0, 0});
  input.setKeyDown(KeyCode::D, true);
  ctrl.update(cam, input, 1.0f);
  Vec3f diagPos = ctrl.getPosition();
  float diagDist = std::sqrt(diagPos.x * diagPos.x + diagPos.y * diagPos.y +
                             diagPos.z * diagPos.z);

  EXPECT(approx(singleDist, diagDist),
         "diagonal distance should equal single-axis distance");
  EXPECT(approx(diagDist, ctrl.moveSpeedPerSecond),
         "distance should equal moveSpeedPerSecond");
}

void testBoostMultipliesSpeed() {
  FreeFlyCameraController ctrl({0, 0, 0}, 0.0f, 0.0f);
  Camera cam;
  MockInputState input;

  input.setKeyDown(KeyCode::W, true);
  input.setKeyDown(KeyCode::LCtrl, true);
  ctrl.update(cam, input, 1.0f);

  float dist = ctrl.getPosition().length();
  float expected = ctrl.moveSpeedPerSecond * ctrl.boostMultiplier;
  EXPECT(approx(dist, expected), "boosted distance should be speed * multiplier");
}

void testPitchIsClamped() {
  FreeFlyCameraController ctrl({0, 0, 0}, 180.0f, 80.0f);
  Camera cam;
  MockInputState input;

  input.setMouseButtonDown(MouseButton::Right, true);
  // Large upward look (negative y → pitch increases via -= negative = +)
  input.setMouseDelta({0.0f, -10000.0f});
  ctrl.update(cam, input, 0.016f);

  EXPECT(approx(ctrl.getPitchDeg(), ctrl.maxPitchDeg),
         "pitch should be clamped to maxPitchDeg");

  // Large downward look
  input.setMouseDelta({0.0f, 10000.0f});
  ctrl.update(cam, input, 0.016f);

  EXPECT(approx(ctrl.getPitchDeg(), ctrl.minPitchDeg),
         "pitch should be clamped to minPitchDeg");
}

} // namespace

int main() {
  expSetEnvVK();
  testWKeyMovesForward();
  testDefaultYawFacesTowardOrigin();
  testMouseLookOnlyWithRightButton();
  testDiagonalMovementNormalized();
  testBoostMultipliesSpeed();
  testPitchIsClamped();

  if (failures == 0) {
    std::cout << "[PASS] All freefly camera controller tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed.\n";
  }
  return failures == 0 ? 0 : 1;
}
