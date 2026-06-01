#include "vulkan_offline_renderer.hpp"
#include <stdexcept>

namespace LX_core::backend {

VulkanOfflineRenderer::VulkanOfflineRenderer(Token) {}

VulkanOfflineRenderer::~VulkanOfflineRenderer() = default;

std::unique_ptr<VulkanOfflineRenderer>
VulkanOfflineRenderer::create(const char *appName) {
  auto renderer = std::make_unique<VulkanOfflineRenderer>(Token{});
  renderer->initialize(appName);
  return renderer;
}

void VulkanOfflineRenderer::initialize(const char *appName) {
  m_foundation = VulkanRendererFoundation::createHeadless(appName);
}

bool VulkanOfflineRenderer::isInitialized() const {
  return m_foundation && m_foundation->isInitialized();
}

bool VulkanOfflineRenderer::isHeadless() const {
  return m_foundation && m_foundation->isHeadless();
}

VulkanRendererFoundation &VulkanOfflineRenderer::foundation() {
  if (!m_foundation) {
    throw std::runtime_error("VulkanOfflineRenderer is not initialized");
  }
  return *m_foundation;
}

const VulkanRendererFoundation &VulkanOfflineRenderer::foundation() const {
  if (!m_foundation) {
    throw std::runtime_error("VulkanOfflineRenderer is not initialized");
  }
  return *m_foundation;
}

} // namespace LX_core::backend
