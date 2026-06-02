#include "core/image/tone_mapping.hpp"

#include <cmath>
#include <iostream>
#include <limits>

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

void testAcesSamples() {
  LX_core::image::ToneMappingSettings settings;
  settings.mode = LX_core::image::ToneMappingMode::Aces;
  settings.exposure = 1.0f;
  settings.gamma = 2.2f;

  EXPECT(LX_core::image::toneMapLinearToSrgb8(0.0f, settings) == 0,
         "black stays black");
  const auto mid = LX_core::image::toneMapLinearToSrgb8(0.18f, settings);
  EXPECT(mid == 140, "middle gray preserves old rounded preview byte");
  const auto white = LX_core::image::toneMapLinearToSrgb8(1.0f, settings);
  EXPECT(white > 220 && white < 240, "white sample matches previous ACES");
}

void testNonFiniteBecomesBlack() {
  LX_core::image::ToneMappingSettings settings;
  const auto value = LX_core::image::toneMapLinearToSrgb8(
      std::numeric_limits<float>::quiet_NaN(), settings);
  EXPECT(value == 0, "NaN is clamped to black");
}

void testNonFiniteGammaUsesSafeClamp() {
  LX_core::image::ToneMappingSettings settings;
  settings.gamma = std::numeric_limits<float>::quiet_NaN();
  const auto value = LX_core::image::toneMapLinearToSrgb8(1.0f, settings);
  EXPECT(value == 0, "NaN gamma uses deterministic safe clamp");
}
} // namespace

int main() {
  testAcesSamples();
  testNonFiniteBecomesBlack();
  testNonFiniteGammaUsesSafeClamp();
  if (failures != 0) {
    std::cerr << "test_tone_mapping failed with " << failures << " failure(s)\n";
    return 1;
  }
  std::cout << "test_tone_mapping passed\n";
  return 0;
}
