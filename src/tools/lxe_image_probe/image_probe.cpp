#include "tools/lxe_image_probe/image_probe.hpp"

#include "infra/image/rgba_image_io.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include <stb/stb_image.h>

namespace LX_tools::image_probe {
namespace {

[[nodiscard]] usize pixelCount(u32 width, u32 height) {
  return static_cast<usize>(width) * static_cast<usize>(height);
}

void validateRgbaPayload(const ProbeImage &image) {
  if (image.width == 0 || image.height == 0) {
    throw std::runtime_error("image probe input has empty extent");
  }
  const usize expected = pixelCount(image.width, image.height) * 4u;
  if (image.rgba.size() != expected) {
    throw std::runtime_error("image probe input has invalid RGBA payload");
  }
}

[[nodiscard]] std::string lowerExtension(const std::filesystem::path &path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return ext;
}

[[nodiscard]] ProbeImage loadPng(const std::filesystem::path &path) {
  int width = 0;
  int height = 0;
  int channels = 0;
  unsigned char *pixels =
      stbi_load(path.string().c_str(), &width, &height, &channels, 4);
  if (pixels == nullptr || width <= 0 || height <= 0) {
    throw std::runtime_error("failed to read PNG " + path.string());
  }

  ProbeImage image;
  image.format = "png-rgba8";
  image.width = static_cast<u32>(width);
  image.height = static_cast<u32>(height);
  const usize count = pixelCount(image.width, image.height) * 4u;
  image.rgba8 = std::vector<unsigned char>(pixels, pixels + count);
  image.rgba.resize(count);
  for (usize i = 0; i < count; ++i) {
    image.rgba[i] = static_cast<float>((*image.rgba8)[i]) / 255.0f;
  }
  stbi_image_free(pixels);
  return image;
}

void accumulate(ScalarStats &stats, double value) {
  if (std::isnan(value)) {
    ++stats.nanCount;
    return;
  }
  if (!std::isfinite(value)) {
    ++stats.infCount;
    return;
  }
  if (stats.finiteCount == 0) {
    stats.minValue = value;
    stats.maxValue = value;
  } else {
    stats.minValue = std::min(stats.minValue, value);
    stats.maxValue = std::max(stats.maxValue, value);
  }
  stats.meanValue += value;
  ++stats.finiteCount;
  if (value != 0.0) {
    stats.nonZeroRatio += 1.0;
  }
  if (value < 0.0) {
    ++stats.negativeCount;
  }
  if (value > 1.0) {
    ++stats.aboveOneCount;
  }
}

void finish(ScalarStats &stats) {
  if (stats.finiteCount == 0) {
    stats.minValue = 0.0;
    stats.maxValue = 0.0;
    stats.meanValue = 0.0;
    stats.nonZeroRatio = 0.0;
    return;
  }
  const double finite = static_cast<double>(stats.finiteCount);
  stats.meanValue /= finite;
  stats.nonZeroRatio /= finite;
}

[[nodiscard]] std::string jsonEscape(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const unsigned char c : text) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if (c < 0x20u) {
        std::ostringstream oss;
        oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
            << static_cast<int>(c);
        out += oss.str();
      } else {
        out.push_back(static_cast<char>(c));
      }
      break;
    }
  }
  return out;
}

void writeStatsJson(std::ostringstream &out, const ScalarStats &stats) {
  out << "{\"min\":" << stats.minValue << ",\"max\":" << stats.maxValue
      << ",\"mean\":" << stats.meanValue
      << ",\"nonZeroRatio\":" << stats.nonZeroRatio
      << ",\"finiteCount\":" << stats.finiteCount
      << ",\"nanCount\":" << stats.nanCount
      << ",\"infCount\":" << stats.infCount
      << ",\"negativeCount\":" << stats.negativeCount
      << ",\"aboveOneCount\":" << stats.aboveOneCount << "}";
}

} // namespace

InputFormat parseInputFormat(std::string_view text) {
  if (text == "auto") {
    return InputFormat::Auto;
  }
  if (text == "exr") {
    return InputFormat::Exr;
  }
  if (text == "png") {
    return InputFormat::Png;
  }
  if (text == "rawrgba32f") {
    return InputFormat::RawRgba32f;
  }
  throw std::runtime_error("unknown image probe format: " + std::string(text));
}

ProbeImage loadRawRgba32f(const std::filesystem::path &path, u32 width,
                          u32 height) {
  if (width == 0 || height == 0) {
    throw std::runtime_error("--raw-width and --raw-height must be positive");
  }
  const usize expectedByteCount =
      pixelCount(width, height) * 4u * sizeof(float);
  const auto actualByteCount = std::filesystem::file_size(path);
  if (actualByteCount != expectedByteCount) {
    throw std::runtime_error(
        "raw RGBA32F byte size does not match extent for " + path.string());
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    throw std::runtime_error("failed to open raw RGBA32F image " +
                             path.string());
  }
  ProbeImage image;
  image.format = "raw-rgba32f";
  image.width = width;
  image.height = height;
  image.rgba.resize(pixelCount(width, height) * 4u);
  stream.read(reinterpret_cast<char *>(image.rgba.data()),
              static_cast<std::streamsize>(image.rgba.size() * sizeof(float)));
  if (!stream) {
    throw std::runtime_error("failed to read raw RGBA32F image " +
                             path.string());
  }
  return image;
}

ProbeImage loadImage(const std::filesystem::path &path, InputFormat format) {
  if (format == InputFormat::Auto) {
    const std::string ext = lowerExtension(path);
    if (ext == ".exr") {
      format = InputFormat::Exr;
    } else if (ext == ".png") {
      format = InputFormat::Png;
    } else if (ext == ".rawrgba32f" || ext == ".rgba32f") {
      throw std::runtime_error(
          "raw RGBA32F input requires --format rawrgba32f --raw-width "
          "--raw-height");
    } else {
      throw std::runtime_error("unable to infer image format from " +
                               path.string());
    }
  }

  if (format == InputFormat::Exr) {
    const auto exr = LX_infra::image::readRgba32fExr(path);
    ProbeImage image;
    image.format = "exr-rgba32f";
    image.width = exr.width;
    image.height = exr.height;
    image.rgba = exr.rgba;
    validateRgbaPayload(image);
    return image;
  }
  if (format == InputFormat::Png) {
    ProbeImage image = loadPng(path);
    validateRgbaPayload(image);
    return image;
  }
  throw std::runtime_error(
      "raw RGBA32F input requires --raw-width and --raw-height");
}

ImageProbeReport probeImage(const std::filesystem::path &path,
                            const ProbeImage &image,
                            const ProbeOptions &options) {
  validateRgbaPayload(image);
  Roi roi{0, 0, image.width, image.height};
  if (options.roi.has_value()) {
    roi = *options.roi;
    if (roi.width == 0 || roi.height == 0 || roi.x >= image.width ||
        roi.y >= image.height || roi.width > image.width - roi.x ||
        roi.height > image.height - roi.y) {
      throw std::runtime_error("image probe ROI is outside image bounds");
    }
  }

  ImageProbeReport report;
  report.path = path;
  report.format = image.format;
  report.width = image.width;
  report.height = image.height;
  report.roi = roi;
  report.samplePixelCount = pixelCount(roi.width, roi.height);

  for (u32 y = roi.y; y < roi.y + roi.height; ++y) {
    for (u32 x = roi.x; x < roi.x + roi.width; ++x) {
      const usize base = (static_cast<usize>(y) * image.width + x) * 4u;
      for (usize c = 0; c < 4u; ++c) {
        accumulate(report.channels[c], image.rgba[base + c]);
      }
      const double r = image.rgba[base + 0u];
      const double g = image.rgba[base + 1u];
      const double b = image.rgba[base + 2u];
      accumulate(report.luminance, 0.2126 * r + 0.7152 * g + 0.0722 * b);
    }
  }
  for (ScalarStats &stats : report.channels) {
    finish(stats);
  }
  finish(report.luminance);

  for (const ProbePoint &probe : options.probes) {
    if (probe.x >= image.width || probe.y >= image.height) {
      throw std::runtime_error("image probe point is outside image bounds");
    }
    const usize base =
        (static_cast<usize>(probe.y) * image.width + probe.x) * 4u;
    PixelProbe out;
    out.x = probe.x;
    out.y = probe.y;
    for (usize c = 0; c < 4u; ++c) {
      out.rgba[c] = image.rgba[base + c];
    }
    if (image.rgba8.has_value()) {
      out.rgba8 = std::array<u32, 4>{
          static_cast<u32>((*image.rgba8)[base + 0u]),
          static_cast<u32>((*image.rgba8)[base + 1u]),
          static_cast<u32>((*image.rgba8)[base + 2u]),
          static_cast<u32>((*image.rgba8)[base + 3u]),
      };
    }
    report.probes.push_back(out);
  }
  return report;
}

std::string reportToJson(const ImageProbeReport &report) {
  std::ostringstream out;
  out << std::setprecision(10);
  out << "{\"path\":\"" << jsonEscape(report.path.generic_string())
      << "\",\"format\":\"" << jsonEscape(report.format)
      << "\",\"width\":" << report.width << ",\"height\":" << report.height
      << ",\"roi\":{\"x\":" << report.roi.x << ",\"y\":" << report.roi.y
      << ",\"width\":" << report.roi.width
      << ",\"height\":" << report.roi.height
      << "},\"samplePixelCount\":" << report.samplePixelCount
      << ",\"channels\":{";
  static constexpr const char *kNames[] = {"r", "g", "b", "a"};
  for (usize i = 0; i < 4u; ++i) {
    if (i != 0) {
      out << ',';
    }
    out << "\"" << kNames[i] << "\":";
    writeStatsJson(out, report.channels[i]);
  }
  out << "},\"luminance\":";
  writeStatsJson(out, report.luminance);
  out << ",\"probes\":[";
  for (usize i = 0; i < report.probes.size(); ++i) {
    if (i != 0) {
      out << ',';
    }
    const PixelProbe &probe = report.probes[i];
    out << "{\"x\":" << probe.x << ",\"y\":" << probe.y << ",\"rgba\":["
        << probe.rgba[0] << "," << probe.rgba[1] << "," << probe.rgba[2] << ","
        << probe.rgba[3] << "]";
    if (probe.rgba8.has_value()) {
      out << ",\"rgba8\":[" << (*probe.rgba8)[0] << "," << (*probe.rgba8)[1]
          << "," << (*probe.rgba8)[2] << "," << (*probe.rgba8)[3] << "]";
    }
    out << "}";
  }
  out << "]}\n";
  return out.str();
}

} // namespace LX_tools::image_probe
