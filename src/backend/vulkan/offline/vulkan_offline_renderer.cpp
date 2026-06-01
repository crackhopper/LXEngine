#include "backend/vulkan/offline/vulkan_offline_renderer.hpp"

#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/device_resources/buffer.hpp"
#include "backend/vulkan/offline/compute_bvh_builder.hpp"
#include "backend/vulkan/offline/gpu_scene_builder.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace LX_core::backend::offline {
namespace {

[[nodiscard]] std::vector<char> loadComputeShader() {
  constexpr const char *shaderFile = "offline_primary_ray.comp.spv";
  std::string shaderPath = getShaderPath("offline_primary_ray", "comp.spv");
  if (shaderPath.empty()) {
    std::filesystem::path probe = std::filesystem::current_path();
    for (int i = 0; i < 8 && shaderPath.empty(); ++i) {
      const std::filesystem::path buildShaderPath =
          probe / "build" / "assets" / "shaders" / "glsl" / shaderFile;
      if (std::filesystem::exists(buildShaderPath)) {
        shaderPath = buildShaderPath.string();
        break;
      }
      const std::filesystem::path localBuildShaderPath =
          probe / "assets" / "shaders" / "glsl" / shaderFile;
      if (std::filesystem::exists(localBuildShaderPath)) {
        shaderPath = localBuildShaderPath.string();
        break;
      }
      const auto parent = probe.parent_path();
      if (parent == probe) {
        break;
      }
      probe = parent;
    }
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

void uploadVector(VulkanBuffer &buffer, const void *data, VkDeviceSize size) {
  if (size == 0) {
    return;
  }
  buffer.uploadData(data, size);
}

} // namespace

struct VulkanOfflineRenderer::Impl final {
  Impl() {
    expSetEnvVK();
    if (!initializeRuntimeAssetRoot()) {
      throw std::runtime_error("failed to initialize runtime asset root");
    }
    device = VulkanDevice::create();
    device->initializeHeadless("lxe_offline_render");
    commandManager = VulkanCommandBufferManager::create(
        *device, 1, device->getGraphicsQueueFamilyIndex());
  }

  ~Impl() {
    if (device) {
      device->waitIdle();
    }
    cleanupPipeline();
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

    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
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
      throw std::runtime_error("failed to create offline descriptor set layout");
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    if (vkCreatePipelineLayout(vkDevice, &pipelineLayoutInfo, nullptr,
                               &pipelineLayout) != VK_SUCCESS) {
      throw std::runtime_error("failed to create offline compute pipeline layout");
    }

    const auto shaderCode = loadComputeShader();
    const VkShaderModule shaderModule = createShaderModule(vkDevice, shaderCode);
    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
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
    poolSize.descriptorCount = 5;
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
    ensurePipeline();

    GpuSceneBuilder sceneBuilder;
    GpuSceneData gpuScene = sceneBuilder.build(job.scene, job.profile);
    ComputeBvhBuilder bvhBuilder;
    BvhBuildResult bvh = bvhBuilder.build(std::move(gpuScene.triangles));
    gpuScene.params.triangleCount = static_cast<u32>(bvh.triangles.size());
    gpuScene.params.bvhNodeCount = static_cast<u32>(bvh.nodes.size());

    const VkBufferUsageFlags storageUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkMemoryPropertyFlags hostMemory =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    auto triangleBuffer = VulkanBuffer::create(
        *device, sizeof(GpuTriangle) * bvh.triangles.size(), storageUsage,
        hostMemory);
    auto materialBuffer = VulkanBuffer::create(
        *device, sizeof(GpuMaterial) * gpuScene.materials.size(), storageUsage,
        hostMemory);
    auto bvhBuffer = VulkanBuffer::create(
        *device, sizeof(GpuBvhNode) * bvh.nodes.size(), storageUsage, hostMemory);
    auto paramsBuffer =
        VulkanBuffer::create(*device, sizeof(GpuCameraParams), storageUsage,
                             hostMemory);
    const VkDeviceSize outputSize =
        static_cast<VkDeviceSize>(job.profile.width) *
        static_cast<VkDeviceSize>(job.profile.height) * sizeof(Vec4f);
    auto outputBuffer =
        VulkanBuffer::create(*device, outputSize, storageUsage, hostMemory);

    uploadVector(*triangleBuffer, bvh.triangles.data(),
                 sizeof(GpuTriangle) * bvh.triangles.size());
    uploadVector(*materialBuffer, gpuScene.materials.data(),
                 sizeof(GpuMaterial) * gpuScene.materials.size());
    uploadVector(*bvhBuffer, bvh.nodes.data(),
                 sizeof(GpuBvhNode) * bvh.nodes.size());
    uploadVector(*paramsBuffer, &gpuScene.params, sizeof(GpuCameraParams));

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

    std::array<VkDescriptorBufferInfo, 5> bufferInfos{{
        {triangleBuffer->getHandle(), 0, triangleBuffer->getSize()},
        {materialBuffer->getHandle(), 0, materialBuffer->getSize()},
        {bvhBuffer->getHandle(), 0, bvhBuffer->getSize()},
        {paramsBuffer->getHandle(), 0, paramsBuffer->getSize()},
        {outputBuffer->getHandle(), 0, outputBuffer->getSize()},
    }};
    std::array<VkWriteDescriptorSet, 5> writes{};
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
    vkCmdBindPipeline(cmd->getHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd->getHandle(), VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDispatch(cmd->getHandle(), (job.profile.width + 7) / 8,
                  (job.profile.height + 7) / 8, 1);
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
    image.width = job.profile.width;
    image.height = job.profile.height;
    image.rgba.resize(image.pixelCount() * 4);
    void *mapped = outputBuffer->map();
    std::memcpy(image.rgba.data(), mapped,
                static_cast<usize>(image.rgba.size() * sizeof(float)));
    outputBuffer->unmap();

    vkResetDescriptorPool(device->getLogicalDevice(), descriptorPool, 0);
    return image;
  }

  VulkanDeviceUniquePtr device;
  VulkanCommandBufferManagerUniquePtr commandManager;
  VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
};

VulkanOfflineRenderer::VulkanOfflineRenderer()
    : m_impl(std::make_unique<Impl>()) {}

VulkanOfflineRenderer::~VulkanOfflineRenderer() = default;

LX_core::offline::OfflineReadbackImage
VulkanOfflineRenderer::render(const LX_core::offline::OfflineRenderJob &job) {
  return m_impl->render(job);
}

} // namespace LX_core::backend::offline
