#include "tools/lxe_compare_exr/exr_compare_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

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

} // namespace LX_tools::compare_exr
