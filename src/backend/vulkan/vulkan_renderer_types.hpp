#pragma once

#include "core/platform/types.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace LX_core::backend {

enum class VulkanPostProcessOutputEncoding : u32 {
  Linear = 0,
  Srgb = 1,
};

struct VulkanPostProcessSettings final {
  float exposure = 1.0f;
  bool bloomEnabled = false;
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

struct PreparedRenderWorkDiagnostics final {
  u64 frameGraphCompileCount = 0;
  u64 renderInputBuildCount = 0;
  u64 renderInputPrepareCount = 0;
  u64 descriptorUploadPlanBuildCount = 0;
  u64 uploadPlanSyncCount = 0;
  u64 volatileUploadSyncCount = 0;
  u64 cachedUploadResourceTouchCount = 0;
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

struct VulkanDebugColorTransferTargetRecord final {
  std::string name;
  std::filesystem::path path;
  std::string format;
  u32 width = 0;
  u32 height = 0;
  double minValue = 0.0;
  double maxValue = 0.0;
  double meanValue = 0.0;
  double nonZeroRatio = 0.0;
};

struct VulkanDebugColorTransferProbeRecord final {
  std::string target;
  std::string label;
  u32 x = 0;
  u32 y = 0;
  u32 red = 0;
  u32 green = 0;
  u32 blue = 0;
  u32 expected = 0;
};

struct VulkanDebugColorTransferFormatFacts final {
  std::string surfaceFormat;
  std::string surfaceColorSpace;
  std::string swapchainImageViewFormat;
  std::string swapchainTargetFormat;
};

struct VulkanDebugColorTransferPreviewTransform final {
  std::string kind;
  std::string toneMappingMode;
  float exposure = 1.0f;
  float gamma = 2.2f;
};

struct VulkanDebugColorTransferPassRecord final {
  std::string pass;
  std::string target;
  std::string shader;
  std::string attachmentFormat;
  std::string pipelineColorFormat;
  std::string outputEncodingMode;
};

struct VulkanDebugColorTransferExportRequest final {
  std::optional<std::string> cameraPath;
  std::filesystem::path outputDirectory;
  u32 width = 0;
  u32 height = 0;
};

struct VulkanDebugColorTransferExportResult final {
  std::string graphUri;
  std::optional<std::string> cameraPath;
  std::filesystem::path manifestPath;
  std::filesystem::path outputDirectory;
  VulkanDebugColorTransferFormatFacts formatFacts;
  VulkanDebugColorTransferPreviewTransform previewTransform;
  std::vector<VulkanDebugColorTransferTargetRecord> targets;
  std::vector<VulkanDebugColorTransferPassRecord> passes;
  std::vector<VulkanDebugColorTransferProbeRecord> probes;
};

} // namespace LX_core::backend
