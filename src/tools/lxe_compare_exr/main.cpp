#include "infra/image/rgba_image_io.hpp"
#include "tools/lxe_compare_exr/exr_compare_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct CompareArgs final {
  std::filesystem::path referencePath;
  std::filesystem::path candidatePath;
  LX_tools::compare_exr::CompareSettings settings;
  u32 diagnosticRadius = 0;
  bool deprecatedThresholdsProvided = false;
};

[[nodiscard]] constexpr const char *usageText() {
  return "usage: lxe_compare_exr --reference REF.exr --candidate CAND.exr "
         "[--linear-l1-thresholds CSV] [--srgb-l1-thresholds CSV] "
         "[--diagnostic-radius N]";
}

[[nodiscard]] std::string takeValue(const std::vector<std::string> &args,
                                    usize &index, std::string_view option) {
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
    if (consumed != text.size() || !std::isfinite(value) || value < 0.0) {
      throw std::runtime_error("");
    }
    return value;
  } catch (const std::exception &) {
    throw std::runtime_error("invalid numeric value for " +
                             std::string(option) + ": " + std::string(text));
  }
}

[[nodiscard]] std::vector<double> parseThresholdList(const std::string &text,
                                                     std::string_view option) {
  std::vector<double> values;
  usize start = 0;
  while (start <= text.size()) {
    const usize end = text.find(',', start);
    const std::string token = text.substr(
        start, end == std::string::npos ? std::string::npos : end - start);
    if (token.empty()) {
      throw std::runtime_error("empty threshold in " + std::string(option));
    }
    values.push_back(parseDouble(token, option));
    if (end == std::string::npos) {
      break;
    }
    start = end + 1u;
  }
  return values;
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
      parsed.referencePath = takeValue(args, i, arg);
    } else if (arg == "--candidate") {
      parsed.candidatePath = takeValue(args, i, arg);
    } else if (arg == "--linear-l1-thresholds") {
      parsed.settings.linearL1Thresholds =
          parseThresholdList(takeValue(args, i, arg), arg);
    } else if (arg == "--srgb-l1-thresholds") {
      parsed.settings.srgbL1Thresholds =
          parseThresholdList(takeValue(args, i, arg), arg);
    } else if (arg == "--diagnostic-radius") {
      parsed.diagnosticRadius = parseU32(takeValue(args, i, arg), arg);
    } else if (arg == "--mean-threshold" || arg == "--max-threshold" ||
               arg == "--rmse-threshold") {
      (void)parseDouble(takeValue(args, i, arg), arg);
      parsed.deprecatedThresholdsProvided = true;
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  if (parsed.referencePath.empty() || parsed.candidatePath.empty()) {
    throw std::runtime_error("both --reference and --candidate are required");
  }
  return parsed;
}

void printDiagnosticWindow(
    const LX_core::offline::OfflineReadbackImage &reference,
    const LX_core::offline::OfflineReadbackImage &candidate,
    const LX_tools::compare_exr::CompareMetrics &metrics, const u32 radius) {
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

void printRatioArray(
    const std::vector<LX_tools::compare_exr::SimilarPixelRatio> &ratios) {
  std::cout << "[";
  for (usize i = 0; i < ratios.size(); ++i) {
    if (i != 0) {
      std::cout << ",";
    }
    std::cout << "{\"threshold\":" << ratios[i].threshold
              << ",\"similarPixelCount\":" << ratios[i].similarPixelCount
              << ",\"ratio\":" << ratios[i].ratio << "}";
  }
  std::cout << "]";
}

void printL1Metrics(const LX_tools::compare_exr::L1Metrics &metrics) {
  std::cout << "{\"mean\":" << metrics.mean << ",\"max\":" << metrics.max
            << ",\"similarPixelRatios\":";
  printRatioArray(metrics.similarPixelRatios);
  std::cout << "}";
}

void printJson(const LX_tools::compare_exr::CompareMetrics &metrics,
               const bool deprecatedThresholdsProvided) {
  std::cout << std::setprecision(10);
  std::cout << "{"
            << "\"width\":" << metrics.width << ","
            << "\"height\":" << metrics.height << ","
            << "\"pixelCount\":" << metrics.pixelCount << ","
            << "\"meanAbsError\":" << metrics.meanAbsError << ","
            << "\"maxAbsError\":" << metrics.maxAbsError << ","
            << "\"rmse\":" << metrics.rmse << ","
            << "\"linearL1\":";
  printL1Metrics(metrics.linearL1);
  std::cout << ",\"srgbL1\":";
  printL1Metrics(metrics.srgbL1);
  std::cout << ",\"referenceMeanLuminance\":"
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
            << "\"deprecatedThresholdsProvided\":"
            << (deprecatedThresholdsProvided ? "true" : "false") << "}\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    const CompareArgs args = parseArgs(argc, argv);
    const auto reference =
        LX_infra::image::readRgba32fExr(args.referencePath);
    const auto candidate =
        LX_infra::image::readRgba32fExr(args.candidatePath);
    const auto metrics =
        LX_tools::compare_exr::compareImages(reference, candidate, args.settings);
    printJson(metrics, args.deprecatedThresholdsProvided);
    std::cout.flush();
    if (args.diagnosticRadius > 0) {
      printDiagnosticWindow(reference, candidate, metrics, args.diagnosticRadius);
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "lxe_compare_exr error: " << error.what() << '\n';
    return 1;
  }
}
