#pragma once

#include "core/platform/types.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace LX_tools::image_probe {

enum class InputFormat {
  Auto,
  Exr,
  Png,
  RawRgba32f,
};

struct Roi final {
  u32 x = 0;
  u32 y = 0;
  u32 width = 0;
  u32 height = 0;
};

struct ProbePoint final {
  u32 x = 0;
  u32 y = 0;
};

struct ProbeOptions final {
  std::optional<Roi> roi;
  std::vector<ProbePoint> probes;
};

struct ProbeImage final {
  std::string format;
  u32 width = 0;
  u32 height = 0;
  std::vector<float> rgba;
  std::optional<std::vector<unsigned char>> rgba8;
};

struct ScalarStats final {
  double minValue = 0.0;
  double maxValue = 0.0;
  double meanValue = 0.0;
  double nonZeroRatio = 0.0;
  usize finiteCount = 0;
  usize nanCount = 0;
  usize infCount = 0;
  usize negativeCount = 0;
  usize aboveOneCount = 0;
};

struct PixelProbe final {
  u32 x = 0;
  u32 y = 0;
  std::array<float, 4> rgba{};
  std::optional<std::array<u32, 4>> rgba8;
};

struct ImageProbeReport final {
  std::filesystem::path path;
  std::string format;
  u32 width = 0;
  u32 height = 0;
  Roi roi;
  usize samplePixelCount = 0;
  std::array<ScalarStats, 4> channels;
  ScalarStats luminance;
  std::vector<PixelProbe> probes;
};

[[nodiscard]] ProbeImage loadImage(const std::filesystem::path &path,
                                   InputFormat format);
[[nodiscard]] ProbeImage loadRawRgba32f(const std::filesystem::path &path,
                                        u32 width, u32 height);
[[nodiscard]] ImageProbeReport probeImage(const std::filesystem::path &path,
                                          const ProbeImage &image,
                                          const ProbeOptions &options);
[[nodiscard]] std::string reportToJson(const ImageProbeReport &report);
[[nodiscard]] InputFormat parseInputFormat(std::string_view text);

} // namespace LX_tools::image_probe
