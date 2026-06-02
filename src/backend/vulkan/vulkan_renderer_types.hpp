#pragma once

#include "core/platform/types.hpp"
#include <filesystem>
#include <string>

namespace LX_core::backend {

struct VulkanPostProcessSettings final {
  bool bloomEnabled = true;
  float bloomIntensity = 0.25f;
  float bloomThreshold = 1.0f;
  float bloomSoftKnee = 0.5f;
};

struct VulkanFrameGraphAttachmentDumpResult final {
  std::filesystem::path path;
  std::filesystem::path screenPath;
  u32 width = 0;
  u32 height = 0;
  std::string format;
};

struct VulkanRealtimeProfileOutputResult final {
  std::filesystem::path linearExrPath;
  std::filesystem::path cpuSrgbPngPath;
  std::filesystem::path pipelineSrgbPngPath;
  std::filesystem::path depthDebugPath;
  std::filesystem::path metadataPath;
  u32 width = 0;
  u32 height = 0;
};

} // namespace LX_core::backend
