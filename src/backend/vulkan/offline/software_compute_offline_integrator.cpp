#include "backend/vulkan/offline/software_compute_offline_integrator.hpp"

#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/device_resources/buffer.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
#include "backend/vulkan/offline/offline_compute_shader.hpp"
#include "backend/vulkan/offline/offline_render_graph_executor.hpp"
#include "core/offline/offline_render_validation.hpp"
#include "core/offline/offline_render_work_graph.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace LX_core::backend::offline {

struct SoftwareComputeOfflineIntegrator::Impl final {
  Impl() {
    expSetEnvVK();
    if (!initializeRuntimeAssetRoot()) {
      throw std::runtime_error("failed to initialize runtime asset root");
    }
    device = VulkanDevice::create();
    device->initializeHeadless("lxe_offline_render");
    commandManager = VulkanCommandBufferManager::create(
        *device, 1, device->getGraphicsQueueFamilyIndex());
    resourceManager = VulkanResourceManager::create(*device);
  }

  ~Impl() {
    if (device) {
      device->waitIdle();
    }
    resourceManager.reset();
    commandManager.reset();
    device.reset();
  }

  [[nodiscard]] LX_core::offline::OfflineReadbackImage
  render(const LX_core::offline::OfflineRenderJob &job) {
    LX_core::offline::validateOfflineRenderJob(job);
    const IShaderSharedPtr offlineShader =
        createOfflineComputeShader(job.offline.shaderMode);
    FrameGraph renderGraph =
        LX_core::offline::createOfflineRenderFrameGraph(job.output);
    renderGraph.build(
        LX_core::RenderWorkBuildContext::offline(job, offlineShader));
    const CompiledFrameGraph compiledGraph = renderGraph.compile();
    if (!compiledGraph.isValid()) {
      throw std::runtime_error(compiledGraph.errorText());
    }

    resourceManager->preloadPipelines(
        renderGraph.collectAllPipelineBuildDescs());
    OfflineRenderGraphExecutor executor(*device, *commandManager,
                                        *resourceManager);
    const OfflineGraphExecutionResult execution =
        executor.execute(renderGraph, compiledGraph);

    LX_core::offline::OfflineReadbackImage image;
    image.width = job.output.width;
    image.height = job.output.height;
    image.rgba.resize(image.pixelCount() * 4);
    const IGpuResourceSharedPtr outputPixelsResource = execution.outputPixels;
    auto outputBuffer = resourceManager->getBuffer(
        outputPixelsResource->getBackendCacheIdentity());
    if (!outputBuffer.has_value()) {
      throw std::runtime_error(
          "offline output storage buffer was not uploaded");
    }
    void *mapped = outputBuffer->get().map();
    std::memcpy(image.rgba.data(), mapped,
                static_cast<usize>(image.rgba.size() * sizeof(float)));
    outputBuffer->get().unmap();
    return image;
  }

  VulkanDeviceUniquePtr device;
  VulkanCommandBufferManagerUniquePtr commandManager;
  VulkanResourceManagerUniquePtr resourceManager;
};

SoftwareComputeOfflineIntegrator::SoftwareComputeOfflineIntegrator()
    : m_impl(std::make_unique<Impl>()) {}

SoftwareComputeOfflineIntegrator::~SoftwareComputeOfflineIntegrator() = default;

LX_core::offline::OfflineReadbackImage SoftwareComputeOfflineIntegrator::render(
    const LX_core::offline::OfflineRenderJob &job) {
  return m_impl->render(job);
}

bool isOfflineIntegratorSupported(const std::string &name) {
  return name == "software-compute";
}

std::unique_ptr<OfflineIntegrator>
createOfflineIntegrator(const std::string &name) {
  if (name == "software-compute") {
    return std::make_unique<SoftwareComputeOfflineIntegrator>();
  }
  throw std::runtime_error("unsupported offline integrator: " + name);
}

} // namespace LX_core::backend::offline
