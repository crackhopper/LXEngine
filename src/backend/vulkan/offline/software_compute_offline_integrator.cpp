#include "backend/vulkan/offline/software_compute_offline_integrator.hpp"

#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/device_resources/buffer.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
#include "core/frame_graph/render_upload_plan.hpp"
#include "core/offline/offline_render_work_graph.hpp"
#include "core/offline/offline_render_validation.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace LX_core::backend::offline {
namespace {

[[nodiscard]] std::vector<char> loadComputeShader() {
  constexpr const char *shaderFile = "offline_primary_ray.comp.spv";
  std::string shaderPath;
  std::filesystem::path probe = std::filesystem::current_path();
  for (int i = 0; i < 8 && shaderPath.empty(); ++i) {
    const std::filesystem::path buildShaderPath =
        probe / "build" / "assets" / "shaders" / "glsl" / shaderFile;
    if (std::filesystem::exists(buildShaderPath)) {
      shaderPath = buildShaderPath.string();
      break;
    }
    const auto parent = probe.parent_path();
    if (parent == probe) {
      break;
    }
    probe = parent;
  }
  if (shaderPath.empty()) {
    shaderPath = getShaderPath("offline_primary_ray", "comp.spv");
  }
  if (shaderPath.empty()) {
    throw std::runtime_error("failed to find offline compute shader SPIR-V");
  }
  return readFile(shaderPath);
}

[[nodiscard]] VkShaderModule createShaderModule(VkDevice device,
                                                const std::vector<char> &code) {
  VkShaderModuleCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = code.size();
  // Vulkan requires pCode as uint32_t-aligned SPIR-V words.
  info.pCode = reinterpret_cast<const u32 *>(code.data());
  VkShaderModule module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
    throw std::runtime_error("failed to create offline compute shader module");
  }
  return module;
}

[[nodiscard]] std::vector<u32> toSpirvWords(const std::vector<char> &code) {
  if (code.size() % sizeof(u32) != 0) {
    throw std::runtime_error("offline compute shader SPIR-V has invalid size");
  }
  std::vector<u32> words(code.size() / sizeof(u32));
  std::memcpy(words.data(), code.data(), code.size());
  return words;
}

[[nodiscard]] const LX_core::ShaderResourceBinding &findReflectedBinding(
    const std::vector<LX_core::ShaderResourceBinding> &bindings, const u32 set,
    const u32 binding) {
  const auto it = std::find_if(
      bindings.begin(), bindings.end(),
      [set, binding](const LX_core::ShaderResourceBinding &reflected) {
        return reflected.set == set && reflected.binding == binding;
      });
  if (it == bindings.end()) {
    std::ostringstream msg;
    msg << "offline shader missing descriptor set " << set << " binding "
        << binding;
    throw std::runtime_error(msg.str());
  }
  return *it;
}

void validateOfflineDescriptorContract(const std::vector<char> &shaderCode) {
  struct ExpectedBinding {
    u32 binding = 0;
    const char *name = "";
    u32 blockSize = 0;
  };

  constexpr u32 kSet = 0;
  const std::array<ExpectedBinding, 9> expected{{
      {0, "SceneVertices", 0},
      {1, "SceneIndices", 0},
      {2, "SceneMeshes", 0},
      {3, "ScenePrimitives", 0},
      {4, "SceneObjects", 0},
      {5, "SceneMaterials", 0},
      {6, "SceneBvhNodes", 0},
      {7, "SceneFrameParams", static_cast<u32>(sizeof(SceneGpuFrameParams))},
      {8, "OutputPixels", 0},
  }};

  LX_core::ShaderStageCode stageCode{};
  stageCode.stage = LX_core::ShaderStage::Compute;
  stageCode.bytecode = toSpirvWords(shaderCode);
  const auto reflected = LX_infra::ShaderReflector::reflect({stageCode});
  if (reflected.size() != expected.size()) {
    std::ostringstream msg;
    msg << "offline shader descriptor count mismatch: expected "
        << expected.size() << ", reflected " << reflected.size();
    throw std::runtime_error(msg.str());
  }

  for (const ExpectedBinding &expectedBinding : expected) {
    const auto &binding =
        findReflectedBinding(reflected, kSet, expectedBinding.binding);
    if (binding.name != expectedBinding.name) {
      std::ostringstream msg;
      msg << "offline shader descriptor binding " << expectedBinding.binding
          << " name mismatch: expected " << expectedBinding.name
          << ", reflected " << binding.name;
      throw std::runtime_error(msg.str());
    }
    if (binding.type != LX_core::ShaderPropertyType::StorageBuffer) {
      std::ostringstream msg;
      msg << "offline shader descriptor binding " << expectedBinding.binding
          << " must be a storage buffer";
      throw std::runtime_error(msg.str());
    }
    if (binding.descriptorCount != 1) {
      std::ostringstream msg;
      msg << "offline shader descriptor binding " << expectedBinding.binding
          << " descriptorCount mismatch";
      throw std::runtime_error(msg.str());
    }
    if ((static_cast<u32>(binding.stageFlags) &
         static_cast<u32>(LX_core::ShaderStage::Compute)) == 0) {
      std::ostringstream msg;
      msg << "offline shader descriptor binding " << expectedBinding.binding
          << " is not visible to compute stage";
      throw std::runtime_error(msg.str());
    }
    if (expectedBinding.blockSize != 0 && binding.size != 0 &&
        binding.size != expectedBinding.blockSize) {
      std::ostringstream msg;
      msg << "offline shader descriptor binding " << expectedBinding.binding
          << " block size mismatch: expected " << expectedBinding.blockSize
          << ", reflected " << binding.size;
      throw std::runtime_error(msg.str());
    }
  }
}

} // namespace

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
    cleanupPipeline();
    resourceManager.reset();
    commandManager.reset();
    device.reset();
  }

  void cleanupPipeline() {
    if (!device) {
      return;
    }
    const VkDevice vkDevice = device->getLogicalDevice();
    if (pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(vkDevice, pipeline, nullptr);
      pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(vkDevice, pipelineLayout, nullptr);
      pipelineLayout = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(vkDevice, descriptorSetLayout, nullptr);
      descriptorSetLayout = VK_NULL_HANDLE;
    }
    if (descriptorPool != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(vkDevice, descriptorPool, nullptr);
      descriptorPool = VK_NULL_HANDLE;
    }
  }

  void ensurePipeline() {
    if (pipeline != VK_NULL_HANDLE) {
      return;
    }
    const VkDevice vkDevice = device->getLogicalDevice();
    const auto shaderCode = loadComputeShader();
    validateOfflineDescriptorContract(shaderCode);

    std::array<VkDescriptorSetLayoutBinding, 9> bindings{};
    for (u32 i = 0; i < bindings.size(); ++i) {
      bindings[i].binding = i;
      bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[i].descriptorCount = 1;
      bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<u32>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(vkDevice, &layoutInfo, nullptr,
                                    &descriptorSetLayout) != VK_SUCCESS) {
      throw std::runtime_error(
          "failed to create offline descriptor set layout");
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    if (vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr,
                               &pipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error(
          "failed to create offline compute pipeline layout");
    }

    const VkShaderModule shaderModule =
        createShaderModule(vkDevice, shaderCode);
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout;
    const VkResult result = vkCreateComputePipelines(
        vkDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(vkDevice, shaderModule, nullptr);
    if (result != VK_SUCCESS) {
      throw std::runtime_error("failed to create offline compute pipeline");
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = static_cast<u32>(bindings.size());
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(vkDevice, &poolInfo, nullptr, &descriptorPool) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create offline descriptor pool");
    }
  }

  [[nodiscard]] LX_core::offline::OfflineReadbackImage
  render(const LX_core::offline::OfflineRenderJob &job) {
    LX_core::offline::validateOfflineRenderJob(job);
    FrameGraph renderGraph =
        LX_core::offline::createOfflineRenderFrameGraph(job.output);
    renderGraph.build(LX_core::RenderWorkBuildContext::offline(job));
    const CompiledFrameGraph compiledGraph = renderGraph.compile();
    if (!compiledGraph.isValid()) {
      throw std::runtime_error(compiledGraph.errorText());
    }
    if (renderGraph.getPasses().empty()) {
      throw std::runtime_error("offline render graph has no passes");
    }
    const FramePass &rayTracePass = renderGraph.getPasses().front();
    if (rayTracePass.queue.getItems().empty()) {
      throw std::runtime_error("offline ray trace pass has no work item");
    }
    const RenderWorkItem &workItem = rayTracePass.queue.getItems().front();
    if (workItem.domain != RenderDomain::Offline ||
        workItem.kind != RenderWorkKind::ComputeDispatch) {
      throw std::runtime_error(
          "offline ray trace pass must produce offline compute work");
    }

    ensurePipeline();
    const RenderUploadPlan uploadPlan = buildRenderUploadPlan(rayTracePass.queue);
    for (const auto &resource : uploadPlan.resources) {
      resourceManager->syncResource(*commandManager, resource);
    }

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;
    if (vkAllocateDescriptorSets(device->getLogicalDevice(), &allocInfo,
                                 &descriptorSet) != VK_SUCCESS) {
      throw std::runtime_error("failed to allocate offline descriptor set");
    }

    const std::array<StringID, 9> bindingNames{{
        StringID("SceneVertices"), StringID("SceneIndices"),
        StringID("SceneMeshes"), StringID("ScenePrimitives"),
        StringID("SceneObjects"), StringID("SceneMaterials"),
        StringID("SceneBvhNodes"), StringID("SceneFrameParams"),
        StringID("OutputPixels"),
    }};
    std::array<VkDescriptorBufferInfo, 9> bufferInfos{};
    IGpuResourceSharedPtr outputPixelsResource;
    for (u32 i = 0; i < bindingNames.size(); ++i) {
      const auto resourceIt = std::find_if(
          workItem.descriptorResources.begin(), workItem.descriptorResources.end(),
          [bindingName = bindingNames[i]](const IGpuResourceSharedPtr &resource) {
            return resource && resource->getBindingName() == bindingName;
          });
      if (resourceIt == workItem.descriptorResources.end()) {
        throw std::runtime_error("offline work item missing storage buffer resource");
      }
      auto buffer = resourceManager->getBuffer((*resourceIt)->getBackendCacheIdentity());
      if (!buffer.has_value()) {
        throw std::runtime_error("offline storage buffer was not uploaded");
      }
      if (bindingNames[i] == StringID("OutputPixels")) {
        outputPixelsResource = *resourceIt;
      }
      bufferInfos[i].buffer = buffer->get().getHandle();
      bufferInfos[i].offset = 0;
      bufferInfos[i].range = buffer->get().getSize();
    }
    std::array<VkWriteDescriptorSet, 9> writes{};
    for (u32 i = 0; i < writes.size(); ++i) {
      writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet = descriptorSet;
      writes[i].dstBinding = i;
      writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      writes[i].descriptorCount = 1;
      writes[i].pBufferInfo = &bufferInfos[i];
    }
    vkUpdateDescriptorSets(device->getLogicalDevice(),
                           static_cast<u32>(writes.size()), writes.data(), 0,
                           nullptr);

    auto cmd = commandManager->beginSingleTimeCommands();
    vkCmdBindPipeline(cmd->getHandle(), VK_PIPELINE_BIND_POINT_COMPUTE,
                      pipeline);
    vkCmdBindDescriptorSets(cmd->getHandle(), VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDispatch(cmd->getHandle(), workItem.compute.groupCountX,
                  workItem.compute.groupCountY,
                  workItem.compute.groupCountZ);
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd->getHandle(), VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr,
                         0, nullptr);
    commandManager->endSingleTimeCommands(std::move(cmd),
                                          device->getGraphicsQueue());

    LX_core::offline::OfflineReadbackImage image;
    image.width = job.output.width;
    image.height = job.output.height;
    image.rgba.resize(image.pixelCount() * 4);
    if (!outputPixelsResource) {
      throw std::runtime_error("offline output storage buffer missing");
    }
    auto outputBuffer =
        resourceManager->getBuffer(outputPixelsResource->getBackendCacheIdentity());
    if (!outputBuffer.has_value()) {
      throw std::runtime_error("offline output storage buffer was not uploaded");
    }
    void *mapped = outputBuffer->get().map();
    std::memcpy(image.rgba.data(), mapped,
                static_cast<usize>(image.rgba.size() * sizeof(float)));
    outputBuffer->get().unmap();

    vkResetDescriptorPool(device->getLogicalDevice(), descriptorPool, 0);
    return image;
  }

  VulkanDeviceUniquePtr device;
  VulkanCommandBufferManagerUniquePtr commandManager;
  VulkanResourceManagerUniquePtr resourceManager;
  VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
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
