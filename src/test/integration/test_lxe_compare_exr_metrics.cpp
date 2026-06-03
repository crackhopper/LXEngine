#include "tools/lxe_compare_exr/exr_compare_metrics.hpp"

#include <cmath>
#include <iostream>

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

[[nodiscard]] bool approx(double lhs, double rhs, double tolerance) {
  return std::abs(lhs - rhs) <= tolerance;
}

void testLinearL1SimilarityRatiosUsePerPixelRgbDistance() {
  LX_core::offline::OfflineReadbackImage reference;
  reference.width = 2;
  reference.height = 1;
  reference.rgba = {
      0.0f, 0.0f, 0.0f, 1.0f,
      1.0f, 1.0f, 1.0f, 1.0f,
  };

  LX_core::offline::OfflineReadbackImage candidate = reference;
  candidate.rgba = {
      0.05f, 0.04f, 0.03f, 1.0f,
      1.7f, 1.1f, 1.0f, 1.0f,
  };

  LX_tools::compare_exr::CompareSettings settings;
  settings.linearL1Thresholds = {0.1, 0.4, 1.6};
  settings.srgbL1Thresholds = {};

  const auto metrics =
      LX_tools::compare_exr::compareImages(reference, candidate, settings);

  EXPECT(metrics.pixelCount == 2, "two pixels participate");
  EXPECT(metrics.linearL1.similarPixelRatios.size() == 3,
         "all linear thresholds are reported");
  EXPECT(approx(metrics.linearL1.mean, 0.46, 1.0e-6),
         "linear mean L1 uses per-pixel RGB L1");
  EXPECT(approx(metrics.linearL1.max, 0.8, 1.0e-6),
         "linear max L1 tracks worst pixel");
  EXPECT(approx(metrics.linearL1.similarPixelRatios[0].ratio, 0.0, 1.0e-6),
         "threshold 0.1 matches no pixel");
  EXPECT(approx(metrics.linearL1.similarPixelRatios[1].ratio, 0.5, 1.0e-6),
         "threshold 0.4 matches one pixel");
  EXPECT(approx(metrics.linearL1.similarPixelRatios[2].ratio, 1.0, 1.0e-6),
         "threshold 1.6 matches both pixels");
}

void testSrgbL1SimilarityRatiosUseSharedToneMapping() {
  LX_core::offline::OfflineReadbackImage reference;
  reference.width = 2;
  reference.height = 1;
  reference.rgba = {
      0.0f, 0.0f, 0.0f, 1.0f,
      1.0f, 1.0f, 1.0f, 1.0f,
  };

  LX_core::offline::OfflineReadbackImage candidate = reference;
  candidate.rgba = {
      0.0f, 0.0f, 0.0f, 1.0f,
      1.1f, 1.1f, 1.1f, 1.0f,
  };

  LX_tools::compare_exr::CompareSettings settings;
  settings.linearL1Thresholds = {};
  settings.srgbL1Thresholds = {0.01, 0.2};

  const auto metrics =
      LX_tools::compare_exr::compareImages(reference, candidate, settings);

  EXPECT(metrics.srgbL1.similarPixelRatios.size() == 2,
         "all sRGB thresholds are reported");
  EXPECT(approx(metrics.srgbL1.similarPixelRatios[0].ratio, 0.5, 1.0e-6),
         "tight sRGB threshold only matches black pixel");
  EXPECT(approx(metrics.srgbL1.similarPixelRatios[1].ratio, 1.0, 1.0e-6),
         "looser sRGB threshold matches both pixels");
}

} // namespace

int main() {
  testLinearL1SimilarityRatiosUsePerPixelRgbDistance();
  testSrgbL1SimilarityRatiosUseSharedToneMapping();
  if (failures != 0) {
    std::cerr << "test_lxe_compare_exr_metrics failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_lxe_compare_exr_metrics passed\n";
  return 0;
}
