#pragma once
#include "core/offline/offline_render_profile.hpp"
#include "core/rhi/renderer.hpp"
#include "core/platform/types.hpp"
#include "vulkan_renderer_types.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LX_core::backend {
class VulkanRealtimeRenderer;
class VulkanRenderer;
using VulkanRendererUniquePtr = std::unique_ptr<VulkanRenderer>;
class VulkanRenderer : public gpu::Renderer {
public:
  using PostProcessSettings = VulkanPostProcessSettings;
  using FrameGraphAttachmentDumpResult = VulkanFrameGraphAttachmentDumpResult;

  struct Token {};
  explicit VulkanRenderer(Token token);
  ~VulkanRenderer() override;
  static VulkanRendererUniquePtr create(Token token) {
    return std::make_unique<VulkanRenderer>(token);
  }

  void initialize(WindowSharedPtr window, const char *appName) override;
  void shutdown() override;
  void initScene(SceneSharedPtr scene) override;

  void uploadData() override;
  void draw() override;

  // Register a callback invoked every frame inside the swapchain render pass,
  // between Gui::beginFrame() and scene draw calls. Replace semantics; pass
  // an empty std::function to clear. Not lifted to the gpu::Renderer base.
  void setDrawUiCallback(std::function<void()> cb);
  void setPostProcessSettings(const PostProcessSettings &settings);
  [[nodiscard]] const PostProcessSettings &postProcessSettings() const;

  [[nodiscard]] usize cachedResourceCount() const;
  [[nodiscard]] usize frameGraphItemCount() const;
  [[nodiscard]] usize compiledFrameGraphPassCount() const;
  [[nodiscard]] std::vector<std::string> compiledFrameGraphPassNames() const;
  [[nodiscard]] usize frameGraphAttachmentCount() const;
  [[nodiscard]] usize initSceneCallCount() const;
  FrameGraphAttachmentDumpResult dumpFrameGraphAttachment(
      std::string_view attachmentName,
      const std::optional<std::filesystem::path> &path = std::nullopt,
      const std::optional<std::filesystem::path> &screenPath = std::nullopt);
  FrameGraphAttachmentDumpResult dumpDebugRenderTarget(
      std::string_view passName,
      const std::optional<std::string> &cameraPath = std::nullopt,
      const std::optional<std::filesystem::path> &path = std::nullopt);
  VulkanRealtimeProfileOutputResult generateRealtimeProfileOutput(
      SceneSharedPtr scene, const LX_core::offline::OutputProfile &output,
      const std::filesystem::path &basePath);

private:
  std::unique_ptr<VulkanRealtimeRenderer> p_realtime;
};

} // namespace LX_core::backend
