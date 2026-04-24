#pragma once
#include "core/rhi/renderer.hpp"
#include <functional>
#include <memory>
#include <vector>

namespace LX_core::backend {
class VulkanRendererImpl;
class VulkanRenderer;
using VulkanRendererUniquePtr = std::unique_ptr<VulkanRenderer>;
class VulkanRenderer : public gpu::Renderer {
public:
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

private:
  std::unique_ptr<VulkanRendererImpl> p_impl;
};

} // namespace LX_core::backend
