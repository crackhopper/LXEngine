#pragma once

#include "core/image/tone_mapping.hpp"
#include "core/offline/offline_render_result.hpp"

#include <string>
#include <vector>

namespace LX_tools::compare_exr {

struct SimilarPixelRatio final {
  double threshold = 0.0;
  usize similarPixelCount = 0;
  double ratio = 0.0;
};

struct L1Metrics final {
  double mean = 0.0;
  double max = 0.0;
  std::vector<SimilarPixelRatio> similarPixelRatios;
};

struct CompareSettings final {
  std::vector<double> linearL1Thresholds{0.01, 0.05, 0.1, 0.4, 1.6};
  std::vector<double> srgbL1Thresholds{0.01, 0.03, 0.05, 0.1, 0.2};
  LX_core::image::ToneMappingSettings toneMapping;
};

struct CompareMetrics final {
  u32 width = 0;
  u32 height = 0;
  usize pixelCount = 0;
  double meanAbsError = 0.0;
  double maxAbsError = 0.0;
  double rmse = 0.0;
  L1Metrics linearL1;
  L1Metrics srgbL1;
  u32 worstX = 0;
  u32 worstY = 0;
  u32 worstChannel = 0;
  double worstReferenceRgb[3]{0.0, 0.0, 0.0};
  double worstCandidateRgb[3]{0.0, 0.0, 0.0};
  double worstDeltaRgb[3]{0.0, 0.0, 0.0};
  double referenceLuminanceSum = 0.0;
  double candidateLuminanceSum = 0.0;
  double referenceMaxLuminance = 0.0;
  double candidateMaxLuminance = 0.0;
  usize referenceLitPixels = 0;
  usize candidateLitPixels = 0;
  usize referenceOnlyLitPixels = 0;
  usize candidateOnlyLitPixels = 0;
  u32 referenceMinX = 0;
  u32 referenceMinY = 0;
  u32 referenceMaxX = 0;
  u32 referenceMaxY = 0;
  u32 candidateMinX = 0;
  u32 candidateMinY = 0;
  u32 candidateMaxX = 0;
  u32 candidateMaxY = 0;
  double referenceCentroidX = 0.0;
  double referenceCentroidY = 0.0;
  double candidateCentroidX = 0.0;
  double candidateCentroidY = 0.0;
};

enum class DiagnosticDifferenceCategory {
  Match,
  EdgeOrCoverage,
  InputMismatch,
  BrdfMismatch,
  UnsupportedOrDisabled,
};

struct DiagnosticPixel final {
  u32 x = 0;
  u32 y = 0;
  u32 materialId = 0;
  u32 objectId = 0;
  double diff = 0.0;
  DiagnosticDifferenceCategory category = DiagnosticDifferenceCategory::Match;
  std::string reason;
};

struct DiagnosticCompareBuffer final {
  u32 width = 0;
  u32 height = 0;
  std::vector<u32> materialId;
  std::vector<u32> objectId;
  std::vector<float> normalDepth; // x, y, z, depth per pixel
  std::vector<u64> directInputsHash;
  std::vector<u8> unsupportedMask;
};

struct DiagnosticCompareReport final {
  usize edgeOrCoveragePixels = 0;
  usize inputMismatchPixels = 0;
  usize brdfMismatchPixels = 0;
  usize unsupportedOrDisabledPixels = 0;
  std::vector<DiagnosticPixel> suspiciousSamples;
};

[[nodiscard]] CompareMetrics
compareImages(const LX_core::offline::OfflineReadbackImage &reference,
              const LX_core::offline::OfflineReadbackImage &candidate,
              const CompareSettings &settings = CompareSettings{});

[[nodiscard]] DiagnosticCompareReport classifyDiagnosticDifferences(
    const LX_core::offline::OfflineReadbackImage &reference,
    const LX_core::offline::OfflineReadbackImage &candidate,
    const DiagnosticCompareBuffer &referenceDiagnostics,
    const DiagnosticCompareBuffer &candidateDiagnostics,
    usize maxSamples = 16);

[[nodiscard]] const char *
diagnosticDifferenceCategoryName(DiagnosticDifferenceCategory category);

} // namespace LX_tools::compare_exr
