#include "tools/lxe_compare_exr/exr_compare_metrics.hpp"

#include <iostream>
#include <string_view>

using namespace LX_tools::compare_exr;

namespace {

int g_failures = 0;

#define EXPECT(cond, msg)                                                      \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "[FAIL] " << msg << '\n';                                   \
      ++g_failures;                                                            \
    }                                                                          \
  } while (0)

LX_core::offline::OfflineReadbackImage makeImage() {
  LX_core::offline::OfflineReadbackImage image;
  image.width = 8;
  image.height = 8;
  image.rgba.resize(image.pixelCount() * 4u, 0.0f);
  for (usize pixel = 0; pixel < image.pixelCount(); ++pixel) {
    image.rgba[pixel * 4u + 3u] = 1.0f;
  }
  return image;
}

DiagnosticCompareBuffer makeDiagnostics() {
  DiagnosticCompareBuffer buffer;
  buffer.width = 8;
  buffer.height = 8;
  const usize pixelCount = static_cast<usize>(buffer.width) * buffer.height;
  buffer.materialId.assign(pixelCount, 1);
  buffer.objectId.assign(pixelCount, 2);
  buffer.normalDepth.assign(pixelCount * 4u, 0.0f);
  buffer.directInputsHash.assign(pixelCount, 100);
  buffer.unsupportedMask.assign(pixelCount, 0);
  for (usize pixel = 0; pixel < pixelCount; ++pixel) {
    buffer.normalDepth[pixel * 4u + 2u] = 1.0f;
    buffer.normalDepth[pixel * 4u + 3u] = 0.5f;
  }
  return buffer;
}

[[nodiscard]] usize pixel(u32 x, u32 y) {
  return static_cast<usize>(y) * 8u + x;
}

void setRgb(LX_core::offline::OfflineReadbackImage &image, u32 x, u32 y,
            float r, float g, float b) {
  const usize base = pixel(x, y) * 4u;
  image.rgba[base + 0u] = r;
  image.rgba[base + 1u] = g;
  image.rgba[base + 2u] = b;
}

void testDiagnosticClassificationSeparatesDifferenceCauses() {
  auto reference = makeImage();
  auto candidate = makeImage();
  auto referenceDiag = makeDiagnostics();
  auto candidateDiag = makeDiagnostics();

  setRgb(reference, 1, 1, 0.7f, 0.7f, 0.7f);
  referenceDiag.materialId[pixel(1, 1)] = 5;
  candidateDiag.materialId[pixel(1, 1)] = 6;

  setRgb(reference, 3, 1, 0.4f, 0.4f, 0.4f);
  setRgb(candidate, 3, 1, 0.6f, 0.4f, 0.4f);
  referenceDiag.directInputsHash[pixel(3, 1)] = 101;
  candidateDiag.directInputsHash[pixel(3, 1)] = 202;

  setRgb(reference, 5, 1, 0.4f, 0.4f, 0.4f);
  setRgb(candidate, 5, 1, 0.45f, 0.4f, 0.4f);

  setRgb(reference, 1, 5, 0.2f, 0.2f, 0.2f);
  setRgb(candidate, 1, 5, 0.8f, 0.2f, 0.2f);
  candidateDiag.unsupportedMask[pixel(1, 5)] = 1;

  const DiagnosticCompareReport report = classifyDiagnosticDifferences(
      reference, candidate, referenceDiag, candidateDiag, 8);

  EXPECT(report.edgeOrCoveragePixels >= 1,
         "material/coverage differences should classify as edge/coverage");
  EXPECT(report.inputMismatchPixels == 1,
         "direct input hash changes should classify as input mismatch");
  EXPECT(report.brdfMismatchPixels == 1,
         "remaining same-input color differences should classify as BRDF");
  EXPECT(report.unsupportedOrDisabledPixels == 1,
         "unsupported mask should classify as unsupported/disabled");
  EXPECT(!report.suspiciousSamples.empty(),
         "report should include suspicious samples");
  EXPECT(diagnosticDifferenceCategoryName(
             DiagnosticDifferenceCategory::InputMismatch) ==
             std::string_view("input-mismatch"),
         "category names should be stable for diagnostics output");
}

} // namespace

int main() {
  testDiagnosticClassificationSeparatesDifferenceCauses();
  if (g_failures != 0) {
    std::cerr << g_failures << " diagnostic compare checks failed\n";
    return 1;
  }
  return 0;
}
