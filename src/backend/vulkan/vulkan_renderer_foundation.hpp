#pragma once

#include "core/platform/window.hpp"
#include "details/commands/command_buffer_manager.hpp"
#include "details/device.hpp"
#include "details/resource_manager.hpp"
#include <memory>

namespace LX_core::backend {

class VulkanRendererFoundation final {
  struct Token {};

public:
  explicit VulkanRendererFoundation(Token);
  ~VulkanRendererFoundation();

  VulkanRendererFoundation(const VulkanRendererFoundation &) = delete;
  VulkanRendererFoundation &
  operator=(const VulkanRendererFoundation &) = delete;

  [[nodiscard]] static std::unique_ptr<VulkanRendererFoundation>
  createRealtime(WindowSharedPtr window, const char *appName);

  [[nodiscard]] static std::unique_ptr<VulkanRendererFoundation>
  createHeadless(const char *appName);

  void shutdown();

  [[nodiscard]] VulkanDevice &device() { return *m_device; }
  [[nodiscard]] const VulkanDevice &device() const { return *m_device; }
  [[nodiscard]] VulkanCommandBufferManager &commandBufferManager() {
    return *m_cmdBufferMgr;
  }
  [[nodiscard]] const VulkanCommandBufferManager &commandBufferManager() const {
    return *m_cmdBufferMgr;
  }
  [[nodiscard]] VulkanResourceManager &resourceManager() {
    return *m_resourceManager;
  }
  [[nodiscard]] const VulkanResourceManager &resourceManager() const {
    return *m_resourceManager;
  }
  [[nodiscard]] bool isInitialized() const { return m_device != nullptr; }
  [[nodiscard]] bool isHeadless() const { return m_headless; }

private:
  void initializeRealtime(WindowSharedPtr window, const char *appName);
  void initializeHeadless(const char *appName);
  void createSharedManagers();

  VulkanDeviceUniquePtr m_device = nullptr;
  VulkanCommandBufferManagerUniquePtr m_cmdBufferMgr = nullptr;
  VulkanResourceManagerUniquePtr m_resourceManager = nullptr;
  bool m_headless = false;
};

using VulkanRendererFoundationUniquePtr =
    std::unique_ptr<VulkanRendererFoundation>;

} // namespace LX_core::backend
