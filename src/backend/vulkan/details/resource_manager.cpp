#include "resource_manager.hpp"
#include "core/rhi/image_format.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/asset/shader.hpp"
#include "core/asset/texture.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/scene.hpp"
#include "commands/command_buffer_manager.hpp"
#include "pipelines/graphics_shader_program.hpp"
#include "render_objects/render_pass.hpp"
#include "device_resources/buffer.hpp"
#include "device_resources/texture.hpp"
#include "device.hpp"
#include <stdexcept>

namespace LX_core::backend {

namespace {
VkFormat toVkFormat(TextureFormat format) {
  switch (format) {
  case TextureFormat::RGBA8:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case TextureFormat::RGB8:
    return VK_FORMAT_R8G8B8_UNORM;
  case TextureFormat::R8:
    return VK_FORMAT_R8_UNORM;
  default:
    throw std::runtime_error("Unsupported TextureFormat");
  }
}

VkFormat toVkFormat(LX_core::ImageFormat format) {
  switch (format) {
  case LX_core::ImageFormat::RGBA8:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case LX_core::ImageFormat::BGRA8:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case LX_core::ImageFormat::R8:
    return VK_FORMAT_R8_UNORM;
  case LX_core::ImageFormat::D32Float:
    return VK_FORMAT_D32_SFLOAT;
  case LX_core::ImageFormat::D24UnormS8:
    return VK_FORMAT_D24_UNORM_S8_UINT;
  case LX_core::ImageFormat::D32FloatS8:
    return VK_FORMAT_D32_SFLOAT_S8_UINT;
  }
  throw std::runtime_error("Unsupported ImageFormat");
}
} // namespace

VulkanResourceManager::VulkanResourceManager(Token, VulkanDevice &device)
    : m_device(device),
      m_pipelineCache(std::make_unique<PipelineCache>(device)) {}

VulkanResourceManager::~VulkanResourceManager() {
  if (m_device.getLogicalDevice() != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(m_device.getLogicalDevice());
  }
}

void VulkanResourceManager::syncResource(
    VulkanCommandBufferManager &cmdBufferManager,
    const IGpuResourcePtr &cpuRes) {
  if (!cpuRes)
    return;

  void *handle = cpuRes->getResourceHandle();
  m_activeHandles.insert(handle);

  auto it = m_gpuResources.find(handle);
  if (it == m_gpuResources.end()) {
    m_gpuResources[handle] = createGpuResource(cpuRes);
    // 新创建的资源强制更新一次数据
    updateGpuResource(m_gpuResources[handle], cpuRes, cmdBufferManager);
    cpuRes->clearDirty();
  } else if (cpuRes->isDirty()) {
    updateGpuResource(it->second, cpuRes, cmdBufferManager);
    cpuRes->clearDirty();
  }
}

std::shared_ptr<VulkanAnyResource>
VulkanResourceManager::createGpuResource(const IGpuResourcePtr &cpuRes) {
  ResourceType type = cpuRes->getType();

  switch (type) {
  case ResourceType::VertexBuffer:
    return std::make_shared<VulkanAnyResource>(VulkanBuffer::create(
        m_device, cpuRes->getByteSize(),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

  case ResourceType::IndexBuffer:
    return std::make_shared<VulkanAnyResource>(VulkanBuffer::create(
        m_device, cpuRes->getByteSize(),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

  case ResourceType::UniformBuffer:
    return std::make_shared<VulkanAnyResource>(VulkanBuffer::create(
        m_device, cpuRes->getByteSize(), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));

  case ResourceType::CombinedImageSampler: {
    auto texCpu = std::dynamic_pointer_cast<CombinedTextureSampler>(cpuRes);
    if (!texCpu || !texCpu->texture()) {
      throw std::runtime_error(
          "CombinedImageSampler resource missing texture data");
    }
    const auto &desc = texCpu->texture()->desc();
    const VkFormat vkFormat = toVkFormat(desc.format);
    VkImageUsageFlags usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    return std::make_shared<VulkanAnyResource>(VulkanTexture::create(
        m_device, desc.width, desc.height, vkFormat, usage, VK_FILTER_LINEAR));
  }

  default:
    throw std::runtime_error("Unsupported resource type for GPU creation");
  }
}

void VulkanResourceManager::updateGpuResource(
    std::shared_ptr<VulkanAnyResource> &gpuRes,
    const IGpuResourcePtr &cpuRes,
    VulkanCommandBufferManager &cmdBufferManager) {
  std::visit(
      [&](auto &&res) {
        using T = std::decay_t<decltype(res)>;
        if constexpr (std::is_same_v<T, VulkanBufferPtr>) {
          // 如果是 Host Visible (Uniform)，直接 map/memcpy
          // 如果是 Device Local (Vertex/Index)，初级架构建议直接
          // uploadData（内部处理 staging）
          res->uploadData(cpuRes->getRawData(), cpuRes->getByteSize());
        } else if constexpr (std::is_same_v<T, VulkanTexturePtr>) {
          const VkDeviceSize imageSize =
              static_cast<VkDeviceSize>(cpuRes->getByteSize());

          // Staging buffer in host-visible memory.
          auto staging = VulkanBuffer::create(
              m_device, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
          staging->uploadData(cpuRes->getRawData(), imageSize);

          auto cmd = cmdBufferManager.beginSingleTimeCommands();

          // Upload the texture contents.
          res->transitionLayout(*cmd, res->getCurrentLayout(),
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
          res->copyFromBuffer(*cmd, *staging);
          res->transitionLayout(*cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

          cmdBufferManager.endSingleTimeCommands(std::move(cmd),
                                                 m_device.getGraphicsQueue());
        }
        // Shaders are immutable for this initial framework; no updates needed.
      },
      *gpuRes);
}

void VulkanResourceManager::collectGarbage() {
  for (auto it = m_gpuResources.begin(); it != m_gpuResources.end();) {
    if (m_activeHandles.find(it->first) == m_activeHandles.end()) {
      it = m_gpuResources.erase(it);
    } else {
      ++it;
    }
  }
  m_activeHandles.clear();
}

void VulkanResourceManager::initializeRenderPassAndPipeline(
    VkSurfaceFormatKHR surfaceFormat, VkFormat depthFormat) {
  if (m_renderPass) {
    return;
  }

  m_renderPass =
      VulkanRenderPass::create(m_device, surfaceFormat.format, depthFormat);
}

// 辅助查找宏，简化代码
#define GET_RESOURCE_IMPL(ReturnType, VariantType)                             \
  auto it = m_gpuResources.find(handle);                                       \
  if (it != m_gpuResources.end()) {                                            \
    if (auto resPtr = std::get_if<VariantType>(&(*(it->second)))) {            \
      return std::ref(*(resPtr->get()));                                       \
    }                                                                          \
  }                                                                            \
  return std::nullopt;

std::optional<std::reference_wrapper<VulkanBuffer>>
VulkanResourceManager::getBuffer(void *handle) {
  GET_RESOURCE_IMPL(VulkanBuffer, VulkanBufferPtr);
}

std::optional<std::reference_wrapper<VulkanTexture>>
VulkanResourceManager::getTexture(void *handle) {
  GET_RESOURCE_IMPL(VulkanTexture, VulkanTexturePtr);
}

VulkanRenderPass &VulkanResourceManager::getRenderPass() {
  return *m_renderPass;
}

VulkanPipeline &VulkanResourceManager::getOrCreateRenderPipeline(
    const LX_core::RenderingItem &item) {
  return m_pipelineCache->getOrCreate(
      LX_core::PipelineBuildDesc::fromRenderingItem(item),
      m_renderPass->getHandle());
}

void VulkanResourceManager::preloadPipelines(
    const std::vector<LX_core::PipelineBuildDesc> &infos) {
  m_pipelineCache->preload(infos, m_renderPass->getHandle());
}

} // namespace LX_core::backend
