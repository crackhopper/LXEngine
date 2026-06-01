#include "vulkan_renderer_foundation.hpp"

namespace LX_core::backend {

namespace {
constexpr u32 kFoundationMaxFramesInFlight = 3;
}

VulkanRendererFoundation::VulkanRendererFoundation(Token) {}

VulkanRendererFoundation::~VulkanRendererFoundation() { shutdown(); }

std::unique_ptr<VulkanRendererFoundation>
VulkanRendererFoundation::createRealtime(WindowSharedPtr window,
                                         const char *appName) {
  auto foundation = std::make_unique<VulkanRendererFoundation>(Token{});
  foundation->initializeRealtime(std::move(window), appName);
  return foundation;
}

std::unique_ptr<VulkanRendererFoundation>
VulkanRendererFoundation::createHeadless(const char *appName) {
  auto foundation = std::make_unique<VulkanRendererFoundation>(Token{});
  foundation->initializeHeadless(appName);
  return foundation;
}

void VulkanRendererFoundation::initializeRealtime(WindowSharedPtr window,
                                                 const char *appName) {
  shutdown();
  m_headless = false;
  m_device = VulkanDevice::create();
  m_device->initialize(std::move(window), appName);
  createSharedManagers();
}

void VulkanRendererFoundation::initializeHeadless(const char *appName) {
  shutdown();
  m_headless = true;
  m_device = VulkanDevice::create();
  m_device->initializeHeadless(appName);
  createSharedManagers();
}

void VulkanRendererFoundation::createSharedManagers() {
  m_cmdBufferMgr = VulkanCommandBufferManager::create(
      *m_device, kFoundationMaxFramesInFlight,
      m_device->getGraphicsQueueFamilyIndex());
  m_resourceManager = VulkanResourceManager::create(*m_device);
}

void VulkanRendererFoundation::shutdown() {
  if (m_device) {
    m_device->waitIdle();
  }
  m_resourceManager.reset();
  m_cmdBufferMgr.reset();
  m_device.reset();
}

} // namespace LX_core::backend
