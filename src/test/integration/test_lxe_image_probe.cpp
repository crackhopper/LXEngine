#include "infra/image/rgba_image_io.hpp"
#include "tools/lxe_image_probe/image_probe.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
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

[[nodiscard]] bool approx(double lhs, double rhs, double eps = 1.0e-6) {
  return std::abs(lhs - rhs) <= eps;
}

[[nodiscard]] std::filesystem::path tempPath(const char *name) {
  return std::filesystem::temp_directory_path() / name;
}

void testComputesStatsAndProbesFromLinearRgba() {
  LX_tools::image_probe::ProbeImage image;
  image.format = "memory-rgba32f";
  image.width = 2;
  image.height = 2;
  image.rgba = {
      0.0f,
      0.5f,
      2.0f,
      1.0f,
      -0.25f,
      std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::infinity(),
      1.0f,
      0.25f,
      0.75f,
      1.25f,
      1.0f,
      1.0f,
      0.0f,
      0.0f,
      1.0f,
  };

  LX_tools::image_probe::ProbeOptions options;
  options.probes.push_back({1, 0});
  const auto report =
      LX_tools::image_probe::probeImage("memory://stats", image, options);

  EXPECT(report.width == 2 && report.height == 2, "extent is preserved");
  EXPECT(report.channels[0].negativeCount == 1, "negative red is counted");
  EXPECT(report.channels[1].nanCount == 1, "NaN green is counted");
  EXPECT(report.channels[2].infCount == 1, "Inf blue is counted");
  EXPECT(report.channels[2].aboveOneCount == 2, "blue values above one count");
  EXPECT(approx(report.channels[0].minValue, -0.25),
         "finite red min includes negative value");
  EXPECT(approx(report.channels[0].maxValue, 1.0), "finite red max is tracked");
  EXPECT(report.probes.size() == 1, "probe is reported");
  EXPECT(approx(report.probes[0].rgba[0], -0.25),
         "probe reads requested pixel red");
  EXPECT(std::isnan(report.probes[0].rgba[1]),
         "probe preserves NaN channel value");
}

void testRoiRestrictsStats() {
  LX_tools::image_probe::ProbeImage image;
  image.format = "memory-rgba32f";
  image.width = 3;
  image.height = 1;
  image.rgba = {
      0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
  };

  LX_tools::image_probe::ProbeOptions options;
  options.roi = LX_tools::image_probe::Roi{1, 0, 1, 1};
  const auto report =
      LX_tools::image_probe::probeImage("memory://roi", image, options);

  EXPECT(report.samplePixelCount == 1, "ROI limits sample count");
  EXPECT(approx(report.channels[0].minValue, 0.5), "ROI red min");
  EXPECT(approx(report.channels[0].maxValue, 0.5), "ROI red max");
  EXPECT(approx(report.luminance.meanValue, 0.5), "ROI luminance mean");
}

void testLoadsPngWithNormalizedAndRawProbeValues() {
  const std::filesystem::path path = tempPath("lxe_image_probe_test.png");
  LX_infra::image::writeRawRgba8Png(path, 2, 1,
                                    std::vector<unsigned char>{
                                        0,
                                        128,
                                        255,
                                        255,
                                        64,
                                        32,
                                        16,
                                        255,
                                    });

  const auto image = LX_tools::image_probe::loadImage(
      path, LX_tools::image_probe::InputFormat::Auto);
  LX_tools::image_probe::ProbeOptions options;
  options.probes.push_back({0, 0});
  const auto report = LX_tools::image_probe::probeImage(path, image, options);

  EXPECT(report.format == "png-rgba8", "PNG format is reported");
  EXPECT(approx(report.channels[2].maxValue, 1.0), "PNG normalized max");
  EXPECT(report.probes[0].rgba8.has_value(), "PNG probe has raw rgba8");
  EXPECT((*report.probes[0].rgba8)[1] == 128, "PNG raw green probe");
  std::filesystem::remove(path);
}

void testLoadsExrAndRawRgba32f() {
  LX_core::offline::OfflineReadbackImage image;
  image.width = 1;
  image.height = 1;
  image.rgba = {0.25f, 0.5f, 2.0f, 1.0f};

  const std::filesystem::path exrPath = tempPath("lxe_image_probe_test.exr");
  const std::filesystem::path rawPath =
      tempPath("lxe_image_probe_test.rawrgba32f");
  LX_infra::image::writeRgba32fExr(exrPath, image);
  LX_infra::image::writeRawRgba32f(rawPath, image);

  const auto exr = LX_tools::image_probe::loadImage(
      exrPath, LX_tools::image_probe::InputFormat::Auto);
  const auto raw = LX_tools::image_probe::loadRawRgba32f(rawPath, 1, 1);

  EXPECT(exr.format == "exr-rgba32f", "EXR format is reported");
  EXPECT(raw.format == "raw-rgba32f", "raw format is reported");
  EXPECT(approx(raw.rgba[2], 2.0), "raw preserves HDR channel");
  std::filesystem::remove(exrPath);
  std::filesystem::remove(rawPath);
}

} // namespace

int main() {
  testComputesStatsAndProbesFromLinearRgba();
  testRoiRestrictsStats();
  testLoadsPngWithNormalizedAndRawProbeValues();
  testLoadsExrAndRawRgba32f();
  if (failures != 0) {
    std::cerr << "test_lxe_image_probe failed with " << failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "test_lxe_image_probe passed\n";
  return 0;
}
