#pragma once

#include "core/image/tone_mapping.hpp"
#include "core/offline/offline_scene.hpp"

#include <filesystem>
#include <string>

namespace LX_infra::offline {

using OfflineToneMappingMode = LX_core::image::ToneMappingMode;
using OfflineToneMappingSettings = LX_core::image::ToneMappingSettings;

struct OfflineImageOutputRequest final {
  LX_core::offline::OfflineRenderJob job;
  LX_core::offline::OfflineReadbackImage image;
  std::filesystem::path scenePath;
  std::string buildInfo = "unknown";
  OfflineToneMappingSettings toneMapping;
  bool writeRawRgba32f = true;
};

struct OfflineImageOutputResult final {
  std::filesystem::path exrPath;
  std::filesystem::path pngPath;
  std::filesystem::path metadataPath;
  std::filesystem::path rawPath;
};

[[nodiscard]] OfflineImageOutputResult
writeOfflineImageOutputs(const OfflineImageOutputRequest &request);

[[nodiscard]] unsigned char toneMapLinearToSrgb8(float value,
                                                 const OfflineToneMappingSettings &settings);

} // namespace LX_infra::offline
