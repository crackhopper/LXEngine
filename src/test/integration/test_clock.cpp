#include "core/time/clock.hpp"
#include "core/utils/env.hpp"

#include <cmath>
#include <iostream>
#include <thread>

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

void testFirstTickHasZeroDelta() {
  Clock clock;
  clock.tick();
  EXPECT(clock.deltaTime() == 0.0f, "first tick deltaTime must be 0");
  EXPECT(clock.frameCount() == 0, "first tick frameCount must be 0");
  EXPECT(clock.totalTime() == 0.0, "first tick totalTime must be 0");
}

void testSecondTickHasNonzeroDelta() {
  Clock clock;
  clock.tick();
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
  clock.tick();
  EXPECT(clock.deltaTime() > 0.0f, "second tick deltaTime must be > 0");
  EXPECT(clock.frameCount() == 1, "second tick frameCount must be 1");
}

void testTotalTimeMonotonicallyIncreases() {
  Clock clock;
  clock.tick();
  double prev = clock.totalTime();
  for (int i = 0; i < 5; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    clock.tick();
    double cur = clock.totalTime();
    EXPECT(cur >= prev, "totalTime must monotonically increase");
    prev = cur;
  }
}

void testSmoothedDeltaFallsBackToDelta() {
  Clock clock;
  clock.tick();
  // After first tick, no samples in buffer
  EXPECT(clock.smoothedDeltaTime() == clock.deltaTime(),
         "smoothedDeltaTime must equal deltaTime when no samples");
}

void testSmoothedDeltaAveragesRecentSamples() {
  Clock clock;
  clock.tick();

  // Produce 5 samples with measurable delays
  for (int i = 0; i < 5; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    clock.tick();
  }

  float smoothed = clock.smoothedDeltaTime();
  EXPECT(smoothed > 0.0f, "smoothedDeltaTime must be > 0 after samples");

  // Smoothed should be close to deltaTime average, not identical to last delta
  // Just verify it's positive and finite
  EXPECT(!std::isinf(smoothed), "smoothedDeltaTime must not be inf");
  EXPECT(!std::isnan(smoothed), "smoothedDeltaTime must not be nan");
}

} // namespace

int main() {
  expSetEnvVK();
  testFirstTickHasZeroDelta();
  testSecondTickHasNonzeroDelta();
  testTotalTimeMonotonicallyIncreases();
  testSmoothedDeltaFallsBackToDelta();
  testSmoothedDeltaAveragesRecentSamples();

  if (failures == 0) {
    std::cout << "[PASS] All clock tests passed.\n";
  } else {
    std::cerr << "[SUMMARY] " << failures << " test(s) failed.\n";
  }
  return failures == 0 ? 0 : 1;
}
