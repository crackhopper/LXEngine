#include "tools/lxe_compare_exr/exr_compare_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace LX_tools::compare_exr {
namespace {

struct MaskStats final {
  usize count = 0;
  u32 minX = 0;
  u32 minY = 0;
  u32 maxX = 0;
  u32 maxY = 0;
  double sumX = 0.0;
  double sumY = 0.0;
};

void includeMaskPixel(MaskStats &stats, const u32 x, const u32 y) {
  if (stats.count == 0) {
    stats.minX = x;
    stats.maxX = x;
    stats.minY = y;
    stats.maxY = y;
  } else {
    stats.minX = std::min(stats.minX, x);
    stats.maxX = std::max(stats.maxX, x);
    stats.minY = std::min(stats.minY, y);
    stats.maxY = std::max(stats.maxY, y);
  }
  ++stats.count;
  stats.sumX += static_cast<double>(x);
  stats.sumY += static_cast<double>(y);
}

[[nodiscard]] L1Metrics makeL1Metrics(const std::vector<double> &distances,
                                      const std::vector<double> &thresholds) {
  L1Metrics metrics;
  if (distances.empty()) {
    return metrics;
  }

  double sum = 0.0;
  for (const double distance : distances) {
    sum += distance;
    metrics.max = std::max(metrics.max, distance);
  }
  metrics.mean = sum / static_cast<double>(distances.size());
  metrics.similarPixelRatios.reserve(thresholds.size());
  for (const double threshold : thresholds) {
    usize similarCount = 0;
    for (const double distance : distances) {
      if (distance <= threshold) {
        ++similarCount;
      }
    }
    SimilarPixelRatio ratio;
    ratio.threshold = threshold;
    ratio.similarPixelCount = similarCount;
    ratio.ratio =
        static_cast<double>(similarCount) / static_cast<double>(distances.size());
    metrics.similarPixelRatios.push_back(ratio);
  }
  return metrics;
}

[[nodiscard]] double srgbL1(
    const LX_core::offline::OfflineReadbackImage &reference,
    const LX_core::offline::OfflineReadbackImage &candidate,
    const usize base, const LX_core::image::ToneMappingSettings &settings) {
  double distance = 0.0;
  for (usize channel = 0; channel < 3u; ++channel) {
    const u8 lhs = LX_core::image::toneMapLinearToSrgb8(
        reference.rgba[base + channel], settings);
    const u8 rhs = LX_core::image::toneMapLinearToSrgb8(
        candidate.rgba[base + channel], settings);
    distance +=
        std::abs(static_cast<double>(rhs) - static_cast<double>(lhs)) / 255.0;
  }
  return distance;
}

void validateDiagnosticBuffer(const DiagnosticCompareBuffer &buffer,
                              u32 width, u32 height,
                              const char *label) {
  if (buffer.width != width || buffer.height != height) {
    throw std::runtime_error(std::string(label) +
                             " diagnostic dimensions differ");
  }
  const usize pixelCount = static_cast<usize>(width) * height;
  if (buffer.materialId.size() != pixelCount ||
      buffer.objectId.size() != pixelCount ||
      buffer.directInputsHash.size() != pixelCount ||
      buffer.unsupportedMask.size() != pixelCount ||
      buffer.normalDepth.size() != pixelCount * 4u) {
    throw std::runtime_error(std::string(label) +
                             " diagnostic buffer size mismatch");
  }
}

[[nodiscard]] double pixelLinearL1(
    const LX_core::offline::OfflineReadbackImage &reference,
    const LX_core::offline::OfflineReadbackImage &candidate, usize pixel) {
  const usize base = pixel * 4u;
  double diff = 0.0;
  for (usize channel = 0; channel < 3u; ++channel) {
    diff += std::abs(static_cast<double>(candidate.rgba[base + channel]) -
                     static_cast<double>(reference.rgba[base + channel]));
  }
  return diff;
}

[[nodiscard]] double luminanceAt(
    const LX_core::offline::OfflineReadbackImage &image, usize pixel) {
  const usize base = pixel * 4u;
  return 0.2126 * static_cast<double>(image.rgba[base + 0u]) +
         0.7152 * static_cast<double>(image.rgba[base + 1u]) +
         0.0722 * static_cast<double>(image.rgba[base + 2u]);
}

[[nodiscard]] bool normalDepthDiscontinuity(
    const DiagnosticCompareBuffer &buffer, usize pixel, usize other) {
  const usize base = pixel * 4u;
  const usize otherBase = other * 4u;
  double normalDelta = 0.0;
  for (usize channel = 0; channel < 3u; ++channel) {
    normalDelta += std::abs(static_cast<double>(buffer.normalDepth[base + channel]) -
                            static_cast<double>(
                                buffer.normalDepth[otherBase + channel]));
  }
  const double depthDelta =
      std::abs(static_cast<double>(buffer.normalDepth[base + 3u]) -
               static_cast<double>(buffer.normalDepth[otherBase + 3u]));
  return normalDelta > 0.25 || depthDelta > 0.01;
}

[[nodiscard]] bool isEdgePixel(const DiagnosticCompareBuffer &reference,
                               const DiagnosticCompareBuffer &candidate,
                               u32 x, u32 y) {
  const auto checkBuffer = [x, y](const DiagnosticCompareBuffer &buffer) {
    const usize center = static_cast<usize>(y) * buffer.width + x;
    const int minY = std::max(0, static_cast<int>(y) - 1);
    const int maxY =
        std::min(static_cast<int>(buffer.height) - 1, static_cast<int>(y) + 1);
    const int minX = std::max(0, static_cast<int>(x) - 1);
    const int maxX =
        std::min(static_cast<int>(buffer.width) - 1, static_cast<int>(x) + 1);
    for (int ny = minY; ny <= maxY; ++ny) {
      for (int nx = minX; nx <= maxX; ++nx) {
        const usize other =
            static_cast<usize>(ny) * buffer.width + static_cast<u32>(nx);
        if (other == center) {
          continue;
        }
        if (buffer.materialId[other] != buffer.materialId[center] ||
            normalDepthDiscontinuity(buffer, center, other)) {
          return true;
        }
      }
    }
    return false;
  };
  return checkBuffer(reference) || checkBuffer(candidate);
}

void addSuspiciousSample(DiagnosticCompareReport &report,
                         DiagnosticPixel sample, usize maxSamples) {
  if (sample.category == DiagnosticDifferenceCategory::Match ||
      maxSamples == 0) {
    return;
  }
  auto &samples = report.suspiciousSamples;
  samples.push_back(std::move(sample));
  std::stable_sort(samples.begin(), samples.end(),
                   [](const DiagnosticPixel &lhs,
                      const DiagnosticPixel &rhs) {
                     return lhs.diff > rhs.diff;
                   });
  if (samples.size() > maxSamples) {
    samples.resize(maxSamples);
  }
}

} // namespace

CompareMetrics compareImages(
    const LX_core::offline::OfflineReadbackImage &reference,
    const LX_core::offline::OfflineReadbackImage &candidate,
    const CompareSettings &settings) {
  if (reference.width != candidate.width ||
      reference.height != candidate.height) {
    throw std::runtime_error("EXR dimensions differ");
  }
  const usize expected = reference.pixelCount() * 4u;
  if (reference.rgba.size() != expected || candidate.rgba.size() != expected) {
    throw std::runtime_error("EXR RGBA buffer size does not match dimensions");
  }

  CompareMetrics metrics;
  metrics.width = reference.width;
  metrics.height = reference.height;
  metrics.pixelCount = reference.pixelCount();
  MaskStats referenceMask;
  MaskStats candidateMask;
  double absSum = 0.0;
  double sqSum = 0.0;
  std::vector<double> linearL1Distances;
  std::vector<double> srgbL1Distances;
  linearL1Distances.reserve(metrics.pixelCount);
  srgbL1Distances.reserve(metrics.pixelCount);

  for (usize pixel = 0; pixel < metrics.pixelCount; ++pixel) {
    const usize base = pixel * 4u;
    const u32 x = static_cast<u32>(pixel % metrics.width);
    const u32 y = static_cast<u32>(pixel / metrics.width);
    double pixelLinearL1 = 0.0;
    for (usize channel = 0; channel < 3u; ++channel) {
      const float lhs = reference.rgba[base + channel];
      const float rhs = candidate.rgba[base + channel];
      if (!std::isfinite(lhs) || !std::isfinite(rhs)) {
        throw std::runtime_error("EXR comparison encountered non-finite RGB");
      }
      const double delta = static_cast<double>(rhs) - static_cast<double>(lhs);
      const double absDelta = std::abs(delta);
      pixelLinearL1 += absDelta;
      absSum += absDelta;
      sqSum += delta * delta;
      if (absDelta > metrics.maxAbsError) {
        metrics.maxAbsError = absDelta;
        metrics.worstX = x;
        metrics.worstY = y;
        metrics.worstChannel = static_cast<u32>(channel);
        for (usize c = 0; c < 3u; ++c) {
          metrics.worstReferenceRgb[c] =
              static_cast<double>(reference.rgba[base + c]);
          metrics.worstCandidateRgb[c] =
              static_cast<double>(candidate.rgba[base + c]);
          metrics.worstDeltaRgb[c] =
              metrics.worstCandidateRgb[c] - metrics.worstReferenceRgb[c];
        }
      }
    }
    linearL1Distances.push_back(pixelLinearL1);
    srgbL1Distances.push_back(
        srgbL1(reference, candidate, base, settings.toneMapping));

    const double referenceLuminance =
        0.2126 * static_cast<double>(reference.rgba[base + 0u]) +
        0.7152 * static_cast<double>(reference.rgba[base + 1u]) +
        0.0722 * static_cast<double>(reference.rgba[base + 2u]);
    const double candidateLuminance =
        0.2126 * static_cast<double>(candidate.rgba[base + 0u]) +
        0.7152 * static_cast<double>(candidate.rgba[base + 1u]) +
        0.0722 * static_cast<double>(candidate.rgba[base + 2u]);
    metrics.referenceLuminanceSum += referenceLuminance;
    metrics.candidateLuminanceSum += candidateLuminance;
    metrics.referenceMaxLuminance =
        std::max(metrics.referenceMaxLuminance, referenceLuminance);
    metrics.candidateMaxLuminance =
        std::max(metrics.candidateMaxLuminance, candidateLuminance);
    constexpr double litThreshold = 1.0e-4;
    const bool referenceLit = referenceLuminance > litThreshold;
    const bool candidateLit = candidateLuminance > litThreshold;
    if (referenceLit) {
      includeMaskPixel(referenceMask, x, y);
    }
    if (candidateLit) {
      includeMaskPixel(candidateMask, x, y);
    }
    if (referenceLit && !candidateLit) {
      ++metrics.referenceOnlyLitPixels;
    }
    if (candidateLit && !referenceLit) {
      ++metrics.candidateOnlyLitPixels;
    }
  }

  const double sampleCount = static_cast<double>(metrics.pixelCount * 3u);
  metrics.meanAbsError = absSum / sampleCount;
  metrics.rmse = std::sqrt(sqSum / sampleCount);
  metrics.linearL1 =
      makeL1Metrics(linearL1Distances, settings.linearL1Thresholds);
  metrics.srgbL1 = makeL1Metrics(srgbL1Distances, settings.srgbL1Thresholds);
  metrics.referenceLitPixels = referenceMask.count;
  metrics.candidateLitPixels = candidateMask.count;
  if (referenceMask.count > 0) {
    metrics.referenceMinX = referenceMask.minX;
    metrics.referenceMinY = referenceMask.minY;
    metrics.referenceMaxX = referenceMask.maxX;
    metrics.referenceMaxY = referenceMask.maxY;
    metrics.referenceCentroidX =
        referenceMask.sumX / static_cast<double>(referenceMask.count);
    metrics.referenceCentroidY =
        referenceMask.sumY / static_cast<double>(referenceMask.count);
  }
  if (candidateMask.count > 0) {
    metrics.candidateMinX = candidateMask.minX;
    metrics.candidateMinY = candidateMask.minY;
    metrics.candidateMaxX = candidateMask.maxX;
    metrics.candidateMaxY = candidateMask.maxY;
    metrics.candidateCentroidX =
        candidateMask.sumX / static_cast<double>(candidateMask.count);
    metrics.candidateCentroidY =
        candidateMask.sumY / static_cast<double>(candidateMask.count);
  }
  return metrics;
}

DiagnosticCompareReport classifyDiagnosticDifferences(
    const LX_core::offline::OfflineReadbackImage &reference,
    const LX_core::offline::OfflineReadbackImage &candidate,
    const DiagnosticCompareBuffer &referenceDiagnostics,
    const DiagnosticCompareBuffer &candidateDiagnostics, usize maxSamples) {
  if (reference.width != candidate.width ||
      reference.height != candidate.height) {
    throw std::runtime_error("diagnostic comparison image dimensions differ");
  }
  const usize expected = reference.pixelCount() * 4u;
  if (reference.rgba.size() != expected || candidate.rgba.size() != expected) {
    throw std::runtime_error(
        "diagnostic comparison RGBA buffer size does not match dimensions");
  }
  validateDiagnosticBuffer(referenceDiagnostics, reference.width,
                           reference.height, "reference");
  validateDiagnosticBuffer(candidateDiagnostics, candidate.width,
                           candidate.height, "candidate");

  DiagnosticCompareReport report;
  constexpr double litThreshold = 1.0e-4;
  constexpr double diffThreshold = 1.0e-5;
  for (usize pixel = 0; pixel < reference.pixelCount(); ++pixel) {
    const double diff = pixelLinearL1(reference, candidate, pixel);
    const bool referenceLit = luminanceAt(reference, pixel) > litThreshold;
    const bool candidateLit = luminanceAt(candidate, pixel) > litThreshold;
    if (diff <= diffThreshold && referenceLit == candidateLit &&
        referenceDiagnostics.directInputsHash[pixel] ==
            candidateDiagnostics.directInputsHash[pixel] &&
        referenceDiagnostics.unsupportedMask[pixel] == 0u &&
        candidateDiagnostics.unsupportedMask[pixel] == 0u) {
      continue;
    }

    const u32 x = static_cast<u32>(pixel % reference.width);
    const u32 y = static_cast<u32>(pixel / reference.width);
    DiagnosticPixel sample;
    sample.x = x;
    sample.y = y;
    sample.materialId = referenceDiagnostics.materialId[pixel];
    sample.objectId = referenceDiagnostics.objectId[pixel];
    sample.diff = diff;

    if (referenceDiagnostics.unsupportedMask[pixel] != 0u ||
        candidateDiagnostics.unsupportedMask[pixel] != 0u) {
      ++report.unsupportedOrDisabledPixels;
      sample.category = DiagnosticDifferenceCategory::UnsupportedOrDisabled;
      sample.reason = "unsupported-or-disabled";
    } else if (referenceLit != candidateLit ||
               referenceDiagnostics.materialId[pixel] !=
                   candidateDiagnostics.materialId[pixel] ||
               isEdgePixel(referenceDiagnostics, candidateDiagnostics, x, y)) {
      ++report.edgeOrCoveragePixels;
      sample.category = DiagnosticDifferenceCategory::EdgeOrCoverage;
      sample.reason = "edge-or-coverage";
    } else if (referenceDiagnostics.directInputsHash[pixel] !=
               candidateDiagnostics.directInputsHash[pixel]) {
      ++report.inputMismatchPixels;
      sample.category = DiagnosticDifferenceCategory::InputMismatch;
      sample.reason = "direct-inputs";
    } else if (diff > diffThreshold) {
      ++report.brdfMismatchPixels;
      sample.category = DiagnosticDifferenceCategory::BrdfMismatch;
      sample.reason = "brdf";
    }

    addSuspiciousSample(report, std::move(sample), maxSamples);
  }
  return report;
}

const char *diagnosticDifferenceCategoryName(
    DiagnosticDifferenceCategory category) {
  switch (category) {
  case DiagnosticDifferenceCategory::Match:
    return "match";
  case DiagnosticDifferenceCategory::EdgeOrCoverage:
    return "edge-or-coverage";
  case DiagnosticDifferenceCategory::InputMismatch:
    return "input-mismatch";
  case DiagnosticDifferenceCategory::BrdfMismatch:
    return "brdf-mismatch";
  case DiagnosticDifferenceCategory::UnsupportedOrDisabled:
    return "unsupported-or-disabled";
  }
  return "unknown";
}

} // namespace LX_tools::compare_exr
