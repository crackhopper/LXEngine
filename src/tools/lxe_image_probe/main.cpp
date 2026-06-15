#include "tools/lxe_image_probe/image_probe.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Args final {
  std::filesystem::path imagePath;
  LX_tools::image_probe::InputFormat format =
      LX_tools::image_probe::InputFormat::Auto;
  u32 rawWidth = 0;
  u32 rawHeight = 0;
  LX_tools::image_probe::ProbeOptions options;
};

[[nodiscard]] constexpr const char *usageText() {
  return "usage: lxe_image_probe --image PATH [--format "
         "auto|exr|png|rawrgba32f] "
         "[--raw-width W --raw-height H] [--roi x,y,w,h] [--probe x,y ...]";
}

[[nodiscard]] std::string takeValue(const std::vector<std::string> &args,
                                    usize &index, std::string_view option) {
  if (index + 1u >= args.size()) {
    throw std::runtime_error("missing value for " + std::string(option));
  }
  ++index;
  return args[index];
}

[[nodiscard]] u32 parseU32(std::string_view text, std::string_view option) {
  try {
    usize consumed = 0;
    const unsigned long value = std::stoul(std::string(text), &consumed);
    if (consumed != text.size() || value > std::numeric_limits<u32>::max()) {
      throw std::runtime_error("");
    }
    return static_cast<u32>(value);
  } catch (const std::exception &) {
    throw std::runtime_error("invalid integer value for " +
                             std::string(option) + ": " + std::string(text));
  }
}

[[nodiscard]] std::vector<u32> parseCsvU32(std::string_view text,
                                           std::string_view option) {
  std::vector<u32> values;
  usize start = 0;
  while (start <= text.size()) {
    const usize end = text.find(',', start);
    const std::string_view token = end == std::string_view::npos
                                       ? text.substr(start)
                                       : text.substr(start, end - start);
    if (token.empty()) {
      throw std::runtime_error("empty value in " + std::string(option));
    }
    values.push_back(parseU32(token, option));
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1u;
  }
  return values;
}

[[nodiscard]] LX_tools::image_probe::ProbePoint
parseProbe(std::string_view text, std::string_view option) {
  const auto values = parseCsvU32(text, option);
  if (values.size() != 2u) {
    throw std::runtime_error("--probe expects x,y");
  }
  return LX_tools::image_probe::ProbePoint{values[0], values[1]};
}

[[nodiscard]] LX_tools::image_probe::Roi parseRoi(std::string_view text,
                                                  std::string_view option) {
  const auto values = parseCsvU32(text, option);
  if (values.size() != 4u) {
    throw std::runtime_error("--roi expects x,y,w,h");
  }
  return LX_tools::image_probe::Roi{values[0], values[1], values[2], values[3]};
}

[[nodiscard]] Args parseArgs(int argc, char **argv) {
  std::vector<std::string> args;
  args.reserve(static_cast<usize>(std::max(argc - 1, 0)));
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }
  if (std::find(args.begin(), args.end(), "--help") != args.end() ||
      std::find(args.begin(), args.end(), "-h") != args.end()) {
    std::cout << usageText() << '\n';
    std::exit(0);
  }

  Args parsed;
  for (usize i = 0; i < args.size(); ++i) {
    const std::string &arg = args[i];
    if (arg == "--image") {
      parsed.imagePath = takeValue(args, i, arg);
    } else if (arg == "--format") {
      parsed.format =
          LX_tools::image_probe::parseInputFormat(takeValue(args, i, arg));
    } else if (arg == "--raw-width") {
      parsed.rawWidth = parseU32(takeValue(args, i, arg), arg);
    } else if (arg == "--raw-height") {
      parsed.rawHeight = parseU32(takeValue(args, i, arg), arg);
    } else if (arg == "--roi") {
      parsed.options.roi = parseRoi(takeValue(args, i, arg), arg);
    } else if (arg == "--probe") {
      parsed.options.probes.push_back(parseProbe(takeValue(args, i, arg), arg));
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  if (parsed.imagePath.empty()) {
    throw std::runtime_error("--image is required");
  }
  return parsed;
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Args args = parseArgs(argc, argv);
    LX_tools::image_probe::ProbeImage image;
    if (args.format == LX_tools::image_probe::InputFormat::RawRgba32f) {
      image = LX_tools::image_probe::loadRawRgba32f(
          args.imagePath, args.rawWidth, args.rawHeight);
    } else {
      image = LX_tools::image_probe::loadImage(args.imagePath, args.format);
    }
    const auto report =
        LX_tools::image_probe::probeImage(args.imagePath, image, args.options);
    std::cout << LX_tools::image_probe::reportToJson(report);
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "lxe_image_probe error: " << error.what() << '\n';
    return 1;
  }
}
