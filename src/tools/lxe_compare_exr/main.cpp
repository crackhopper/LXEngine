#include "infra/image/rgba_image_io.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct CompareArgs final {
  std::filesystem::path referencePath;
  std::filesystem::path candidatePath;
  std::optional<double> meanThreshold;
  std::optional<double> maxThreshold;
  std::optional<double> rmseThreshold;
  u32 diagnosticRadius = 1;
};

struct CompareMetrics final {
  u32 width = 0;
  u32 height = 0;
  usize pixelCount = 0;
  double meanAbsError = 0.0;
  double maxAbsError = 0.0;
  double rmse = 0.0;
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

struct MaskStats final {
  usize count = 0;
  u32 minX = 0;
  u32 minY = 0;
  u32 maxX = 0;
  u32 maxY = 0;
  double sumX = 0.0;
  double sumY = 0.0;
};

[[nodiscard]] constexpr const char *usageText() {
  return "usage: lxe_compare_exr --reference REF.exr --candidate CAND.exr "
         "[--mean-threshold V] [--max-threshold V] [--rmse-threshold V] "
         "[--diagnostic-radius N]";
}

[[nodiscard]] std::optional<std::string>
takeValue(const std::vector<std::string> &args, usize &index,
          std::string_view option) {
  if (index + 1u >= args.size()) {
    throw std::runtime_error("missing value for " + std::string(option));
  }
  ++index;
  return args[index];
}

[[nodiscard]] double parseDouble(std::string_view text,
                                 std::string_view option) {
  try {
    usize consumed = 0;
    const double value = std::stod(std::string(text), &consumed);
    if (consumed != text.size() || !std::isfinite(value)) {
      throw std::runtime_error("");
    }
    return value;
  } catch (const std::exception &) {
    throw std::runtime_error("invalid numeric value for " +
                             std::string(option) + ": " + std::string(text));
  }
}

[[nodiscard]] u32 parseU32(std::string_view text, std::string_view option) {
  try {
    usize consumed = 0;
    const unsigned long value = std::stoul(std::string(text), &consumed);
    if (consumed != text.size() || value > 8u) {
      throw std::runtime_error("");
    }
    return static_cast<u32>(value);
  } catch (const std::exception &) {
    throw std::runtime_error("invalid integer value for " +
                             std::string(option) + ": " + std::string(text));
  }
}

[[nodiscard]] CompareArgs parseArgs(int argc, char **argv) {
  std::vector<std::string> args;
  args.reserve(static_cast<usize>(std::max(argc - 1, 0)));
  for (int i = 1; i < argc; ++i) {
    args.push_back(argv[i]);
  }
  if (std::find(args.begin(), args.end(), "--help") != args.end() ||
      std::find(args.begin(), args.end(), "-h") != args.end()) {
    std::cout << usageText() << '\n';
    std::exit(0);
  }

  CompareArgs parsed;
  for (usize i = 0; i < args.size(); ++i) {
    const std::string &arg = args[i];
    if (arg == "--reference") {
      parsed.referencePath = *takeValue(args, i, arg);
    } else if (arg == "--candidate") {
      parsed.candidatePath = *takeValue(args, i, arg);
    } else if (arg == "--mean-threshold") {
      parsed.meanThreshold = parseDouble(*takeValue(args, i, arg), arg);
    } else if (arg == "--max-threshold") {
      parsed.maxThreshold = parseDouble(*takeValue(args, i, arg), arg);
    } else if (arg == "--rmse-threshold") {
      parsed.rmseThreshold = parseDouble(*takeValue(args, i, arg), arg);
    } else if (arg == "--diagnostic-radius") {
      parsed.diagnosticRadius = parseU32(*takeValue(args, i, arg), arg);
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  if (parsed.referencePath.empty() || parsed.candidatePath.empty()) {
    throw std::runtime_error("both --reference and --candidate are required");
  }
  return parsed;
}

[[nodiscard]] CompareMetrics compareImages(
    const LX_core::offline::OfflineReadbackImage &reference,
    const LX_core::offline::OfflineReadbackImage &candidate) {
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
  auto includeMaskPixel = [](MaskStats &stats, const u32 x, const u32 y) {
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
  };
  double absSum = 0.0;
  double sqSum = 0.0;
  for (usize pixel = 0; pixel < metrics.pixelCount; ++pixel) {
    const usize base = pixel * 4u;
    const u32 x = static_cast<u32>(pixel % metrics.width);
    const u32 y = static_cast<u32>(pixel / metrics.width);
    for (usize channel = 0; channel < 3u; ++channel) {
      const float lhs = reference.rgba[base + channel];
      const float rhs = candidate.rgba[base + channel];
      if (!std::isfinite(lhs) || !std::isfinite(rhs)) {
        throw std::runtime_error("EXR comparison encountered non-finite RGB");
      }
      const double delta = static_cast<double>(rhs) - static_cast<double>(lhs);
      const double absDelta = std::abs(delta);
      absSum += absDelta;
      sqSum += delta * delta;
      if (absDelta > metrics.maxAbsError) {
        metrics.maxAbsError = absDelta;
        metrics.worstX = static_cast<u32>(pixel % metrics.width);
        metrics.worstY = static_cast<u32>(pixel / metrics.width);
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

void printDiagnosticWindow(
    const LX_core::offline::OfflineReadbackImage &reference,
    const LX_core::offline::OfflineReadbackImage &candidate,
    const CompareMetrics &metrics, const u32 radius) {
  const i32 minX =
      std::max<i32>(0, static_cast<i32>(metrics.worstX) - static_cast<i32>(radius));
  const i32 maxX = std::min<i32>(
      static_cast<i32>(metrics.width) - 1,
      static_cast<i32>(metrics.worstX) + static_cast<i32>(radius));
  const i32 minY =
      std::max<i32>(0, static_cast<i32>(metrics.worstY) - static_cast<i32>(radius));
  const i32 maxY = std::min<i32>(
      static_cast<i32>(metrics.height) - 1,
      static_cast<i32>(metrics.worstY) + static_cast<i32>(radius));

  std::cerr << "worst-pixel window (candidate - reference), RGB:\n";
  for (i32 y = minY; y <= maxY; ++y) {
    for (i32 x = minX; x <= maxX; ++x) {
      const usize base =
          (static_cast<usize>(y) * metrics.width + static_cast<usize>(x)) * 4u;
      std::cerr << "(" << x << "," << y << "):["
                << candidate.rgba[base + 0u] - reference.rgba[base + 0u]
                << "," << candidate.rgba[base + 1u] - reference.rgba[base + 1u]
                << "," << candidate.rgba[base + 2u] - reference.rgba[base + 2u]
                << "] ";
    }
    std::cerr << '\n';
  }
}

[[nodiscard]] bool thresholdFailed(const CompareArgs &args,
                                   const CompareMetrics &metrics) {
  return (args.meanThreshold && metrics.meanAbsError > *args.meanThreshold) ||
         (args.maxThreshold && metrics.maxAbsError > *args.maxThreshold) ||
         (args.rmseThreshold && metrics.rmse > *args.rmseThreshold);
}

void printJson(const CompareMetrics &metrics, bool passed) {
  std::cout << std::setprecision(10);
  std::cout << "{"
            << "\"width\":" << metrics.width << ","
            << "\"height\":" << metrics.height << ","
            << "\"pixelCount\":" << metrics.pixelCount << ","
            << "\"meanAbsError\":" << metrics.meanAbsError << ","
            << "\"maxAbsError\":" << metrics.maxAbsError << ","
            << "\"rmse\":" << metrics.rmse << ","
            << "\"referenceMeanLuminance\":"
            << metrics.referenceLuminanceSum /
                   static_cast<double>(metrics.pixelCount)
            << ","
            << "\"candidateMeanLuminance\":"
            << metrics.candidateLuminanceSum /
                   static_cast<double>(metrics.pixelCount)
            << ","
            << "\"referenceMaxLuminance\":" << metrics.referenceMaxLuminance
            << ","
            << "\"candidateMaxLuminance\":" << metrics.candidateMaxLuminance
            << ","
            << "\"coverage\":{\"referenceLitPixels\":"
            << metrics.referenceLitPixels << ",\"candidateLitPixels\":"
            << metrics.candidateLitPixels << ",\"referenceOnlyLitPixels\":"
            << metrics.referenceOnlyLitPixels << ",\"candidateOnlyLitPixels\":"
            << metrics.candidateOnlyLitPixels
            << ",\"referenceBBox\":{\"minX\":" << metrics.referenceMinX
            << ",\"minY\":" << metrics.referenceMinY
            << ",\"maxX\":" << metrics.referenceMaxX << ",\"maxY\":"
            << metrics.referenceMaxY << "},\"candidateBBox\":{\"minX\":"
            << metrics.candidateMinX << ",\"minY\":" << metrics.candidateMinY
            << ",\"maxX\":" << metrics.candidateMaxX << ",\"maxY\":"
            << metrics.candidateMaxY << "},\"referenceCentroid\":["
            << metrics.referenceCentroidX << "," << metrics.referenceCentroidY
            << "],\"candidateCentroid\":[" << metrics.candidateCentroidX
            << "," << metrics.candidateCentroidY << "]},"
            << "\"worstPixel\":{\"x\":" << metrics.worstX
            << ",\"y\":" << metrics.worstY
            << ",\"channel\":" << metrics.worstChannel
            << ",\"referenceRgb\":[" << metrics.worstReferenceRgb[0] << ","
            << metrics.worstReferenceRgb[1] << ","
            << metrics.worstReferenceRgb[2] << "],\"candidateRgb\":["
            << metrics.worstCandidateRgb[0] << ","
            << metrics.worstCandidateRgb[1] << ","
            << metrics.worstCandidateRgb[2] << "],\"deltaRgb\":["
            << metrics.worstDeltaRgb[0] << "," << metrics.worstDeltaRgb[1]
            << "," << metrics.worstDeltaRgb[2] << "]},"
            << "\"passed\":" << (passed ? "true" : "false") << "}\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    const CompareArgs args = parseArgs(argc, argv);
    const auto reference =
        LX_infra::image::readRgba32fExr(args.referencePath);
    const auto candidate =
        LX_infra::image::readRgba32fExr(args.candidatePath);
    const CompareMetrics metrics = compareImages(reference, candidate);
    const bool passed = !thresholdFailed(args, metrics);
    printJson(metrics, passed);
    std::cout.flush();
    if (!passed) {
      printDiagnosticWindow(reference, candidate, metrics,
                            args.diagnosticRadius);
    }
    return passed ? 0 : 2;
  } catch (const std::exception &error) {
    std::cerr << "lxe_compare_exr error: " << error.what() << '\n';
    return 1;
  }
}
