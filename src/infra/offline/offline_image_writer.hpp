#pragma once

#include "core/offline/offline_scene.hpp"

#include <filesystem>
#include <string>

namespace LX_infra::offline {

enum class OfflineToneMappingMode { Aces, Reinhard };

struct OfflineToneMappingSettings final {
  float exposure = 1.0f;
  float gamma = 2.2f;
  OfflineToneMappingMode mode = OfflineToneMappingMode::Aces;
};

struct OfflineImageOutputRequest final {
  LX_core::offline::OfflineRenderJob job;
  LX_core::offline::OfflineReadbackImage image;
  std::filesystem::path scenePath;
  std::string profileName;
  std::string gitCommit = "unknown";
  bool gitDirty = false;
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
