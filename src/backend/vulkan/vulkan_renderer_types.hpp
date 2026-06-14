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
  double minValue = 0.0;
  double maxValue = 0.0;
  double meanValue = 0.0;
  double nonZeroRatio = 0.0;
};

struct VulkanRealtimeRenderBatchStats final {
  usize compilerBatchCountConsumed = 0;
  usize boundBatchGeometryCount = 0;
  usize submittedDirectIndexedDrawCount = 0;
  usize submittedIndexedIndirectCommandCount = 0;
  usize submittedIndirectBatchCount = 0;
  usize submittedIndirectDrawCount = 0;
  u32 firstCommandOffset = 0;
  u32 lastCommandOffset = 0;
  usize fallbackObservedCount = 0;
};

struct VulkanRealtimeProfileOutputResult final {
  std::filesystem::path linearExrPath;
  std::filesystem::path cpuSrgbPngPath;
  std::filesystem::path pipelineSrgbPngPath;
  std::filesystem::path depthDebugPath;
  std::filesystem::path metadataPath;
  VulkanRealtimeRenderBatchStats renderBatchStats;
  u32 width = 0;
  u32 height = 0;
};

} // namespace LX_core::backend
