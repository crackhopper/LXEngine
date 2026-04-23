#include "core/input/mock_input_state.hpp"
#include "core/scene/orbit_camera_controller.hpp"
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

constexpr float kEps = 1e-3f;

bool approx(float a, float b) { return std::abs(a - b) < kEps; }

void testDefaultPositionInFrontOfTarget() {
  OrbitCameraController ctrl({0, 0, 0}, 5.0f, 0.0f, 0.0f);
  Camera cam;
  MockInputState input;

  ctrl.update(cam, input, 0.016f);

  // yaw=0, pitch=0 → camera at (0, 0, 5)
  EXPECT(approx(cam.position.x, 0.0f), "cam.position.x should be ~0");
  EXPECT(approx(cam.position.y, 0.0f), "cam.position.y should be ~0");
  EXPECT(approx(cam.position.z, 5.0f), "cam.position.z should be ~5");
  EXPECT(approx(cam.target.x, 0.0f), "cam.target should be origin");
  EXPECT(approx(cam.target.y, 0.0f), "cam.target should be origin");
  EXPECT(approx(cam.target.z, 0.0f), "cam.target should be origin");
}

void testLeftDragRotatesCamera() {
  OrbitCameraController ctrl({0, 0, 0}, 5.0f, 0.0f, 0.0f);
  Camera cam;
  MockInputState input;

  input.setMouseButtonDown(MouseButton::Left, true);
  input.setMouseDelta({10.0f, 0.0f});

  float expectedYaw = 10.0f * ctrl.rotateSpeedDegPerPixel;
  ctrl.update(cam, input, 0.016f);

  EXPECT(approx(ctrl.getYawDeg(), expectedYaw),
         "yaw should increase by delta.x * rotateSpeed");
  EXPECT(approx(ctrl.getPitchDeg(), 0.0f), "pitch should not change");

  // Camera should have moved off the Z axis
  EXPECT(std::abs(cam.position.x) > 0.01f,
         "camera x should be non-zero after yaw rotation");
}

void testPitchIsClamped() {
  OrbitCameraController ctrl({0, 0, 0}, 5.0f, 0.0f, 80.0f);
  Camera cam;
  MockInputState input;

  input.setMouseButtonDown(MouseButton::Left, true);
  // Large upward drag (negative y → pitch increases)
  input.setMouseDelta({0.0f, -1000.0f});

  ctrl.update(cam, input, 0.016f);

  EXPECT(approx(ctrl.getPitchDeg(), ctrl.maxPitchDeg),
         "pitch should be clamped to maxPitchDeg");

  // Now drag down heavily
  input.setMouseDelta({0.0f, 10000.0f});
  ctrl.update(cam, input, 0.016f);

  EXPECT(approx(ctrl.getPitchDeg(), ctrl.minPitchDeg),
         "pitch should be clamped to minPitchDeg");
}

void testWheelClampsDistance() {
  OrbitCameraController ctrl({0, 0, 0}, 5.0f, 0.0f, 0.0f);
  Camera cam;
  MockInputState input;

  // Zoom in heavily
  input.setMouseWheelDelta(1000.0f);
  ctrl.update(cam, input, 0.016f);

  EXPECT(approx(ctrl.getDistance(), ctrl.minDistance),
         "distance should be clamped to minDistance");

  // Reset to mid distance then zoom out heavily
  ctrl.setDistance(100.0f);
  input.setMouseWheelDelta(-1000.0f);
  ctrl.update(cam, input, 0.016f);

  EXPECT(approx(ctrl.getDistance(), ctrl.maxDistance),
         "distance should be clamped to maxDistance");
}

void testRightDragPansTarget() {
  OrbitCameraController ctrl({0, 0, 0}, 5.0f, 0.0f, 0.0f);
  Camera cam;
  MockInputState input;

  Vec3f origTarget = ctrl.getTarget();

  input.setMouseButtonDown(MouseButton::Right, true);
  input.setMouseDelta({10.0f, 0.0f});

  ctrl.update(cam, input, 0.016f);

  Vec3f newTarget = ctrl.getTarget();
  float dist = (newTarget - origTarget).length();
  EXPECT(dist > 1e-5f, "target should have moved after right-drag");
}

void testNextFrameClearsDelta() {
  MockInputState input;
  input.setMouseDelta({5.0f, 3.0f});
  input.setMouseWheelDelta(1.0f);
  input.setMouseButtonDown(MouseButton::Left, true);

  input.nextFrame();

  auto delta = input.getMouseDelta();
  EXPECT(approx(delta.x, 0.0f) && approx(delta.y, 0.0f),
         "mouse delta should be cleared after nextFrame");
  EXPECT(approx(input.getMouseWheelDelta(), 0.0f),
         "wheel delta should be cleared after nextFrame");
  EXPECT(input.isMouseButtonDown(MouseButton::Left),
         "button state should be preserved after nextFrame");
}

} // namespace

int main() {
  expSetEnvVK();
  testDefaultPositionInFrontOfTarget();
  testLeftDragRotatesCamera();
  testPitchIsClamped();
  testWheelClampsDistance();
  testRightDragPansTarget();
  testNextFrameClearsDelta();

  if (failures == 0) {
    std::cout << "[PASS] All orbit camera controller tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed.\n";
  }
  return failures == 0 ? 0 : 1;
}
