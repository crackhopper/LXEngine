#include "core/editor/gizmo_adapter.hpp"

#include <cmath>
#include <iostream>

namespace {

constexpr float kEps = 1e-3f;
int failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__ << " " << msg  \
                << " (" #cond ")\n";                                           \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

bool nearlyEqual(float a, float b) { return std::fabs(a - b) <= kEps; }

void testFloat16RoundTrip() {
  LX_core::Mat4f matrix = LX_core::Mat4f::translate({1.0f, 2.0f, 3.0f}) *
                          LX_core::Mat4f::scale({2.0f, 3.0f, 4.0f});
  float packed[16] = {};
  LX_core::GizmoAdapter::toFloat16(matrix, packed);
  const LX_core::Mat4f roundTrip = LX_core::GizmoAdapter::fromFloat16(packed);
  EXPECT(nearlyEqual(roundTrip(0, 3), 1.0f), "translation x round-trips");
  EXPECT(nearlyEqual(roundTrip(1, 3), 2.0f), "translation y round-trips");
  EXPECT(nearlyEqual(roundTrip(2, 3), 3.0f), "translation z round-trips");
}

void testComposeDecompose() {
  LX_core::GizmoTransformComponents components;
  components.translation = {4.0f, 5.0f, 6.0f};
  components.rotationEulerDegrees = {0.0f, 90.0f, 0.0f};
  components.scale = {1.5f, 2.5f, 3.5f};

  const LX_core::Mat4f matrix = LX_core::GizmoAdapter::compose(components);
  const auto roundTrip = LX_core::GizmoAdapter::decompose(matrix);
  EXPECT(nearlyEqual(roundTrip.translation.x, 4.0f), "compose/decompose keeps tx");
  EXPECT(nearlyEqual(roundTrip.translation.y, 5.0f), "compose/decompose keeps ty");
  EXPECT(nearlyEqual(roundTrip.translation.z, 6.0f), "compose/decompose keeps tz");
}

} // namespace

int main() {
  testFloat16RoundTrip();
  testComposeDecompose();
  if (failures == 0) {
    std::cout << "[PASS] gizmo_adapter tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed\n";
  }
  return failures == 0 ? 0 : 1;
}
