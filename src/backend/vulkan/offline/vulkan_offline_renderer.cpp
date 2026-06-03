#include "backend/vulkan/offline/vulkan_offline_renderer.hpp"

#include "backend/vulkan/offline/offline_integrator.hpp"

#include <stdexcept>

namespace LX_core::backend::offline {

VulkanOfflineRenderer::VulkanOfflineRenderer() = default;

VulkanOfflineRenderer::~VulkanOfflineRenderer() = default;

LX_core::offline::OfflineReadbackImage
VulkanOfflineRenderer::render(const LX_core::offline::OfflineRenderJob &job) {
  if (!isOfflineIntegratorSupported(job.offline.integrator)) {
    throw std::runtime_error("unsupported offline integrator: " +
                             job.offline.integrator);
  }
  auto integrator = createOfflineIntegrator(job.offline.integrator);
  return integrator->render(job);
}

} // namespace LX_core::backend::offline
