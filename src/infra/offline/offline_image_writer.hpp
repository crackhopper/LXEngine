#pragma once

#include "core/frame_graph/frame_graph_executor.hpp"
#include "core/image/tone_mapping.hpp"
#include "core/offline/offline_render_profile.hpp"
#include "core/offline/offline_render_result.hpp"

#include <filesystem>
#include <string>

namespace LX_infra::offline {

using OfflineToneMappingMode = LX_core::image::ToneMappingMode;
using OfflineToneMappingSettings = LX_core::image::ToneMappingSettings;

struct OfflineImageOutputRequest final {
  LX_core::offline::OutputProfile output;
  LX_core::offline::OfflineRenderSettings offline;
  std::string profileName;
  std::filesystem::path outputPath;
  LX_core::FrameGraphExecutionPayload payload;
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

[[nodiscard]] LX_core::offline::OfflineReadbackImage
offlineImageFromPayload(const LX_core::FrameGraphExecutionPayload &payload);

[[nodiscard]] unsigned char toneMapLinearToSrgb8(float value,
                                                 const OfflineToneMappingSettings &settings);

} // namespace LX_infra::offline
