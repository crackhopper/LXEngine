#pragma once

#include "core/offline/offline_render_profile.hpp"
#include "core/frame_graph/render_target.hpp"
#include "core/rhi/renderer.hpp"
#include "vulkan_renderer_types.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_core::backend {

struct PreparedRenderStateKey final {
  u64 graphGeneration = 0;
  u64 resourceGeneration = 0;
  u64 featureGeneration = 0;
  u64 sceneNodeGeneration = 0;
  RenderTargetDesc target;

  [[nodiscard]] bool operator==(const PreparedRenderStateKey &rhs) const {
    return graphGeneration == rhs.graphGeneration &&
           resourceGeneration == rhs.resourceGeneration &&
           featureGeneration == rhs.featureGeneration &&
           sceneNodeGeneration == rhs.sceneNodeGeneration &&
           target == rhs.target;
  }

  [[nodiscard]] bool operator!=(const PreparedRenderStateKey &rhs) const {
    return !(*this == rhs);
  }
};

struct PreparedRenderStateCacheSnapshot final {
  bool valid = false;
  PreparedRenderStateKey key;
  u64 descriptorResourceSelectionGeneration = 0;
  u64 descriptorUploadGeneration = 0;
  u64 volatileUploadGeneration = 0;
};

struct PreparedRenderStateCacheDecision final {
  bool rebuildFrameGraph = false;
  bool rebuildRenderInputs = false;
  bool rebuildDescriptorUploadPlans = false;
  bool syncUploadPlans = false;
  bool syncVolatileResources = false;
  bool touchCachedUploadResources = false;
  PreparedRenderStateCacheSnapshot nextSnapshot;
};

[[nodiscard]] PreparedRenderStateCacheDecision
evaluatePreparedRenderStateCache(
    const PreparedRenderStateCacheSnapshot &current,
    const PreparedRenderStateKey &nextKey,
    u64 nextDescriptorResourceSelectionGeneration,
    u64 nextDescriptorUploadGeneration,
    u64 nextVolatileUploadGeneration);

class VulkanRealtimeRenderer final : public gpu::Renderer {
public:
  VulkanRealtimeRenderer();
  ~VulkanRealtimeRenderer() override;

  void initialize(WindowSharedPtr window, const char *appName) override;
  void shutdown() override;
  void initScene(SceneSharedPtr scene) override;

  void uploadData() override;
  void draw() override;
  void setLiveRenderView(std::optional<gpu::LiveRenderView> view) override;
  [[nodiscard]] gpu::LiveRenderSubmissionStats
  liveRenderSubmissionStats() const override;

  void setDrawUiCallback(std::function<void()> cb);
  void setPostProcessSettings(const VulkanPostProcessSettings &settings);
  [[nodiscard]] const VulkanPostProcessSettings &postProcessSettings() const;

  [[nodiscard]] usize cachedResourceCount() const;
  [[nodiscard]] usize frameGraphItemCount() const;
  [[nodiscard]] usize compiledFrameGraphPassCount() const;
  [[nodiscard]] std::vector<std::string> compiledFrameGraphPassNames() const;
  [[nodiscard]] usize frameGraphAttachmentCount() const;
  [[nodiscard]] usize initSceneCallCount() const;
  [[nodiscard]] PreparedRenderWorkDiagnostics
  preparedRenderWorkDiagnostics() const;

  VulkanFrameGraphAttachmentDumpResult dumpFrameGraphAttachment(
      std::string_view attachmentName,
      const std::optional<std::filesystem::path> &path = std::nullopt,
      const std::optional<std::filesystem::path> &screenPath = std::nullopt);
  VulkanFrameGraphAttachmentDumpResult statsFrameGraphAttachment(
      std::string_view attachmentName);
  VulkanRealtimeProfileOutputResult generateRealtimeProfileOutput(
      SceneSharedPtr scene, const LX_core::offline::OutputProfile &output,
      const std::filesystem::path &basePath);
  VulkanDebugColorTransferExportResult exportDebugColorTransfer(
      const VulkanDebugColorTransferExportRequest &request);

private:
  class Impl;
  std::unique_ptr<Impl> p_impl;
};

using VulkanRealtimeRendererUniquePtr = std::unique_ptr<VulkanRealtimeRenderer>;

} // namespace LX_core::backend
