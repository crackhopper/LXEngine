#pragma once

#include "vulkan_renderer_foundation.hpp"
#include <memory>

namespace LX_core::backend {

class VulkanOfflineRenderer final {
  struct Token {};

public:
  explicit VulkanOfflineRenderer(Token);
  ~VulkanOfflineRenderer();

  [[nodiscard]] static std::unique_ptr<VulkanOfflineRenderer>
  create(const char *appName = "LXEngine Offline Renderer");

  VulkanOfflineRenderer(const VulkanOfflineRenderer &) = delete;
  VulkanOfflineRenderer &operator=(const VulkanOfflineRenderer &) = delete;

  [[nodiscard]] bool isInitialized() const;
  [[nodiscard]] bool isHeadless() const;
  [[nodiscard]] VulkanRendererFoundation &foundation();
  [[nodiscard]] const VulkanRendererFoundation &foundation() const;

private:
  void initialize(const char *appName);

  VulkanRendererFoundationUniquePtr m_foundation = nullptr;
};

using VulkanOfflineRendererUniquePtr = std::unique_ptr<VulkanOfflineRenderer>;

} // namespace LX_core::backend
