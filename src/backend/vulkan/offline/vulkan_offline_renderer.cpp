#include "backend/vulkan/offline/vulkan_offline_renderer.hpp"

#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/device_resources/buffer.hpp"
#include "core/offline/offline_ray_scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/shader_compiler/shader_reflector.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <stdexcept>
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

void uploadVector(VulkanBuffer &buffer, const void *data, VkDeviceSize size) {
  if (size == 0) {
    return;
  }
  buffer.uploadData(data, size);
}

[[nodiscard]] std::vector<u32> toSpirvWords(const std::vector<char> &code) {
  if (code.size() % sizeof(u32) != 0) {
    throw std::runtime_error("offline compute shader SPIR-V has invalid size");
  }
  std::vector<u32> words(code.size() / sizeof(u32));
  std::memcpy(words.data(), code.data(), code.size());
  return words;
}

[[nodiscard]] const LX_core::ShaderResourceBinding &
findReflectedBinding(
    const std::vector<LX_core::ShaderResourceBinding> &bindings,
    const u32 set, const u32 binding) {
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
      {0, "Vertices", 0},
      {1, "Indices", 0},
      {2, "Meshes", 0},
      {3, "Primitives", 0},
      {4, "Objects", 0},
      {5, "Materials", 0},
      {6, "BvhNodes", 0},
      {7, "ParamsBuffer",
       static_cast<u32>(sizeof(LX_core::offline::OfflineSceneParams))},
      {8, "OutputBuffer", 0},
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
    ensurePipeline();

    LX_core::offline::OfflineRaySceneBuilder sceneBuilder;
    LX_core::offline::OfflineRayScene rayScene =
        sceneBuilder.build(job.scene, job.output, job.offline);

    const VkBufferUsageFlags storageUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    const VkMemoryPropertyFlags hostMemory =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    auto vertexBuffer = VulkanBuffer::create(
        *device, sizeof(LX_core::offline::OfflineVertexRecord) *
                     rayScene.vertices.size(),
        storageUsage, hostMemory);
    auto indexBuffer = VulkanBuffer::create(
        *device, sizeof(u32) * rayScene.indices.size(), storageUsage,
        hostMemory);
    auto meshBuffer = VulkanBuffer::create(
        *device, sizeof(LX_core::offline::OfflineMeshRecord) *
                     rayScene.meshes.size(),
        storageUsage, hostMemory);
    auto primitiveBuffer = VulkanBuffer::create(
        *device, sizeof(LX_core::offline::OfflinePrimitiveRecord) *
                     rayScene.primitives.size(),
        storageUsage, hostMemory);
    auto objectBuffer = VulkanBuffer::create(
        *device, sizeof(LX_core::offline::OfflineObjectRecord) *
                     rayScene.objects.size(),
        storageUsage,
        hostMemory);
    auto materialBuffer = VulkanBuffer::create(
        *device, sizeof(LX_core::offline::OfflineMaterialRecord) *
                     rayScene.materials.size(),
        storageUsage,
        hostMemory);
    auto bvhBuffer = VulkanBuffer::create(
        *device,
        sizeof(LX_core::offline::OfflineBvhNode) * rayScene.bvhNodes.size(),
        storageUsage, hostMemory);
    auto paramsBuffer =
        VulkanBuffer::create(*device,
                             sizeof(LX_core::offline::OfflineSceneParams),
                             storageUsage, hostMemory);
    const VkDeviceSize outputSize =
        static_cast<VkDeviceSize>(job.output.width) *
        static_cast<VkDeviceSize>(job.output.height) * sizeof(Vec4f);
    auto outputBuffer =
        VulkanBuffer::create(*device, outputSize, storageUsage, hostMemory);

    uploadVector(*vertexBuffer, rayScene.vertices.data(),
                 sizeof(LX_core::offline::OfflineVertexRecord) *
                     rayScene.vertices.size());
    uploadVector(*indexBuffer, rayScene.indices.data(),
                 sizeof(u32) * rayScene.indices.size());
    uploadVector(*meshBuffer, rayScene.meshes.data(),
                 sizeof(LX_core::offline::OfflineMeshRecord) *
                     rayScene.meshes.size());
    uploadVector(*primitiveBuffer, rayScene.primitives.data(),
                 sizeof(LX_core::offline::OfflinePrimitiveRecord) *
                     rayScene.primitives.size());
    uploadVector(*objectBuffer, rayScene.objects.data(),
                 sizeof(LX_core::offline::OfflineObjectRecord) *
                     rayScene.objects.size());
    uploadVector(*materialBuffer, rayScene.materials.data(),
                 sizeof(LX_core::offline::OfflineMaterialRecord) *
                     rayScene.materials.size());
    uploadVector(*bvhBuffer, rayScene.bvhNodes.data(),
                 sizeof(LX_core::offline::OfflineBvhNode) *
                     rayScene.bvhNodes.size());
    uploadVector(*paramsBuffer, &rayScene.params,
                 sizeof(LX_core::offline::OfflineSceneParams));

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

    std::array<VkDescriptorBufferInfo, 9> bufferInfos{{
        {vertexBuffer->getHandle(), 0, vertexBuffer->getSize()},
        {indexBuffer->getHandle(), 0, indexBuffer->getSize()},
        {meshBuffer->getHandle(), 0, meshBuffer->getSize()},
        {primitiveBuffer->getHandle(), 0, primitiveBuffer->getSize()},
        {objectBuffer->getHandle(), 0, objectBuffer->getSize()},
        {materialBuffer->getHandle(), 0, materialBuffer->getSize()},
        {bvhBuffer->getHandle(), 0, bvhBuffer->getSize()},
        {paramsBuffer->getHandle(), 0, paramsBuffer->getSize()},
        {outputBuffer->getHandle(), 0, outputBuffer->getSize()},
    }};
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
    vkCmdBindPipeline(cmd->getHandle(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(cmd->getHandle(), VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
    vkCmdDispatch(cmd->getHandle(), (job.output.width + 7) / 8,
                  (job.output.height + 7) / 8, 1);
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
