#include "backend/vulkan/offline/software_compute_offline_integrator.hpp"

#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/device_resources/buffer.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
#include "backend/vulkan/offline/offline_compute_shader.hpp"
#include "backend/vulkan/offline/offline_render_graph_executor.hpp"
#include "core/frame_graph/render_validation_contract.hpp"
#include "core/frame_graph/render_work_build_context.hpp"
#include "core/frame_graph/render_work_compiler.hpp"
#include "core/offline/offline_render_validation.hpp"
#include "core/offline/offline_render_work_graph.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"

#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace LX_core::backend::offline {
namespace {

[[nodiscard]] std::string debugString(StringID value) {
  return value.id == 0 ? std::string("<none>")
                       : GlobalStringTable::get().toDebugString(value);
}

} // namespace

void validatePreparedOfflineRenderDescs(
    const std::vector<std::vector<RenderInputDesc>> &passDescs) {
  std::ostringstream details;
  bool failed = false;

  for (const std::vector<RenderInputDesc> &descs : passDescs) {
    const RenderInputValidationResult validation =
        validatePreparedRenderInputs(descs);
    if (validation.ok) {
      continue;
    }
    failed = true;
    for (const RenderInputDiagnostic &diagnostic :
         validation.diagnostics) {
      details << "\n- pass=" << debugString(diagnostic.pass)
              << " debugId=" << debugString(diagnostic.debugId)
              << " message=" << diagnostic.message;
    }
    for (const RenderInputDesc &desc : descs) {
      if (desc.accepted() || !desc.diagnostics.empty()) {
        continue;
      }
      details << "\n- rejected prepared offline render desc pass="
              << debugString(desc.pass)
              << " debugId=" << debugString(desc.debugId);
    }
  }

  if (failed) {
    throw std::runtime_error("offline prepared render input validation failed" +
                             details.str());
  }
}

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
  render(LX_core::offline::OfflineRenderJob &job) {
    LX_core::offline::validateOfflineRenderJob(job);
    const IShaderSharedPtr offlineShader = job.offlineShader;
    if (!offlineShader) {
      throw std::runtime_error(
          "offline render requires an offline shader provider");
    }
    FrameGraph renderGraph =
        LX_core::offline::createOfflineRenderFrameGraph(job.output);
    const CompiledFrameGraph compiledGraph = renderGraph.compile();
    if (!compiledGraph.isValid()) {
      throw std::runtime_error(compiledGraph.errorText());
    }

    std::vector<std::vector<std::unique_ptr<RenderInput>>> passInputs(
        renderGraph.getPasses().size());
    std::vector<std::vector<RenderInputDesc>> passDescs(
        renderGraph.getPasses().size());
    RenderWorkCompiler compiler;
    for (usize passIndex = 0; passIndex < renderGraph.getPasses().size();
         ++passIndex) {
      const FramePass &pass = renderGraph.getPasses()[passIndex];
      const RenderWorkBuildContext context =
          LX_core::RenderWorkBuildContext::offline(job);
      compiler.buildInputs(pass, context, passInputs[passIndex]);
      passDescs[passIndex] =
          compiler.prepare(pass, context, passInputs[passIndex]);
    }
    validatePreparedOfflineRenderDescs(passDescs);

    std::unordered_set<PipelineKey, PipelineKey::Hash> seenPipelines;
    std::vector<PipelineBuildDesc> pipelineDescs;
    for (const auto &descs : passDescs) {
      for (const RenderInputDesc &desc : descs) {
        if (desc.accepted() &&
            seenPipelines.insert(desc.pipelineBuildDesc.key).second) {
          pipelineDescs.push_back(desc.pipelineBuildDesc);
        }
      }
    }
    resourceManager->preloadPipelines(pipelineDescs);
    OfflineRenderGraphExecutor executor(*device, *commandManager,
                                        *resourceManager);
    const OfflineGraphExecutionResult execution =
        executor.execute(renderGraph, compiledGraph, passInputs, passDescs);

    LX_core::offline::OfflineReadbackImage image;
    image.width = job.output.width;
    image.height = job.output.height;
    image.rgba.resize(image.pixelCount() * 4);
    auto outputBuffer = resourceManager->getBuffer(
        execution.outputPixels.getBackendCacheIdentity());
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
    LX_core::offline::OfflineRenderJob &job) {
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
