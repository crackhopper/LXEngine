#pragma once

#include "core/platform/types.hpp"
#include <filesystem>
#include <string>

namespace LX_core::backend {

enum class VulkanPostProcessOutputEncoding : u32 {
  Linear = 0,
  Srgb = 1,
};

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
  double minValue = 0.0;
  double maxValue = 0.0;
  double meanValue = 0.0;
  double nonZeroRatio = 0.0;
};

struct VulkanRealtimeRenderInputStats final {
  usize compilerInputCount = 0;
  usize acceptedInputCount = 0;
  usize rejectedInputCount = 0;
  usize submittedDrawCount = 0;
  usize submittedDispatchCount = 0;
  usize fallbackObservedCount = 0;
  usize descPipelineLookupCount = 0;
  usize descBoundInputCount = 0;
  usize descExecutedInputCount = 0;
};

struct VulkanRealtimeProfileOutputResult final {
  std::filesystem::path linearExrPath;
  std::filesystem::path cpuSrgbPngPath;
  std::filesystem::path pipelineSrgbPngPath;
  std::filesystem::path depthDebugPath;
  std::filesystem::path metadataPath;
  VulkanRealtimeRenderInputStats renderInputStats;
  u32 width = 0;
  u32 height = 0;
};

} // namespace LX_core::backend
