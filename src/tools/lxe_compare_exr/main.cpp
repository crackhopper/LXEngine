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
};

struct CompareMetrics final {
  u32 width = 0;
  u32 height = 0;
  usize pixelCount = 0;
  double meanAbsError = 0.0;
  double maxAbsError = 0.0;
  double rmse = 0.0;
};

[[nodiscard]] constexpr const char *usageText() {
  return "usage: lxe_compare_exr --reference REF.exr --candidate CAND.exr "
         "[--mean-threshold V] [--max-threshold V] [--rmse-threshold V]";
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
  double absSum = 0.0;
  double sqSum = 0.0;
  for (usize pixel = 0; pixel < metrics.pixelCount; ++pixel) {
    const usize base = pixel * 4u;
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
      metrics.maxAbsError = std::max(metrics.maxAbsError, absDelta);
    }
  }
  const double sampleCount = static_cast<double>(metrics.pixelCount * 3u);
  metrics.meanAbsError = absSum / sampleCount;
  metrics.rmse = std::sqrt(sqSum / sampleCount);
  return metrics;
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
    return passed ? 0 : 2;
  } catch (const std::exception &error) {
    std::cerr << "lxe_compare_exr error: " << error.what() << '\n';
    return 1;
  }
}
