#pragma once

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

class VulkanRealtimeRenderer final : public gpu::Renderer {
public:
  VulkanRealtimeRenderer();
  ~VulkanRealtimeRenderer() override;

  void initialize(WindowSharedPtr window, const char *appName) override;
  void shutdown() override;
  void initScene(SceneSharedPtr scene) override;

  void uploadData() override;
  void draw() override;

  void setDrawUiCallback(std::function<void()> cb);
  void setPostProcessSettings(const VulkanPostProcessSettings &settings);
  [[nodiscard]] const VulkanPostProcessSettings &postProcessSettings() const;

  [[nodiscard]] usize cachedResourceCount() const;
  [[nodiscard]] usize frameGraphItemCount() const;
  [[nodiscard]] usize compiledFrameGraphPassCount() const;
  [[nodiscard]] std::vector<std::string> compiledFrameGraphPassNames() const;
  [[nodiscard]] usize frameGraphAttachmentCount() const;
  [[nodiscard]] usize initSceneCallCount() const;

  VulkanFrameGraphAttachmentDumpResult dumpFrameGraphAttachment(
      std::string_view attachmentName,
      const std::optional<std::filesystem::path> &path = std::nullopt,
      const std::optional<std::filesystem::path> &screenPath = std::nullopt);
  VulkanFrameGraphAttachmentDumpResult dumpDebugRenderTarget(
      std::string_view passName,
      const std::optional<std::string> &cameraPath = std::nullopt,
      const std::optional<std::filesystem::path> &path = std::nullopt);

private:
  class Impl;
  std::unique_ptr<Impl> p_impl;
};

using VulkanRealtimeRendererUniquePtr = std::unique_ptr<VulkanRealtimeRenderer>;

} // namespace LX_core::backend
