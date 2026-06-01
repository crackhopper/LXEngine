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

} // namespace LX_core::backend
