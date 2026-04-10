#include "vk_resource_manager.hpp"
#include "commands/vkc_cmdbuffer_manager.hpp"
#include "core/resources/index_buffer.hpp"
#include "core/resources/shader.hpp"
#include "core/resources/texture.hpp"
#include "core/resources/vertex_buffer.hpp"
#include "core/scene/scene.hpp"
#include "pipelines/forward_pipeline_slots.hpp"
#include "pipelines/vkp_shader_graphics.hpp"
#include "render_objects/vkr_renderpass.hpp"
#include "resources/vkr_buffer.hpp"
#include "resources/vkr_shader.hpp"
#include "resources/vkr_texture.hpp"
#include "vk_device.hpp"
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
} // namespace

VulkanResourceManager::VulkanResourceManager(Token, VulkanDevice &device)
    : m_device(device) {}

VulkanResourceManager::~VulkanResourceManager() {
  if (m_device.getLogicalDevice() != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(m_device.getLogicalDevice());
  }
}

void VulkanResourceManager::syncResource(
    VulkanCommandBufferManager &cmdBufferManager,
    const IRenderResourcePtr &cpuRes) {
  if (!cpuRes)
    return;

  // Push constants are written directly into command buffers; no Vulkan object
  // needed.
  if (cpuRes->getType() == ResourceType::PushConstant) {
    return;
  }

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
VulkanResourceManager::createGpuResource(const IRenderResourcePtr &cpuRes) {
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

  case ResourceType::Shader:
    // Pipelines load SPIR-V from disk (`VulkanShader`); `IShader` on the CPU
    // side is for reflection / material binding, not standalone GPU upload here.
    throw std::runtime_error(
        "syncResource: ResourceType::Shader (IShader) has no GPU mirror in "
        "VulkanResourceManager; use pipeline file paths");

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
    const IRenderResourcePtr &cpuRes,
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

std::optional<std::reference_wrapper<VulkanShader>>
VulkanResourceManager::getShader(void *handle) {
  GET_RESOURCE_IMPL(VulkanShader, VulkanShaderPtr);
}

VulkanRenderPass &VulkanResourceManager::getRenderPass() {
  return *m_renderPass;
}

VulkanPipeline &
VulkanResourceManager::getOrCreateRenderPipeline(const LX_core::RenderingItem &item) {
  auto it = m_pipelines.find(item.pipelineKey);
  if (it != m_pipelines.end()) {
    return *it->second;
  }

  auto vb = std::dynamic_pointer_cast<LX_core::IVertexBuffer>(item.vertexBuffer);
  auto ib = std::dynamic_pointer_cast<LX_core::IndexBuffer>(item.indexBuffer);
  if (!vb || !ib) {
    throw std::runtime_error(
        "getOrCreateRenderPipeline: missing vertex or index buffer");
  }
  if (!item.shaderInfo) {
    throw std::runtime_error("getOrCreateRenderPipeline: missing shader (IShader)");
  }

  const std::string shaderName = item.shaderInfo->getShaderName();
  if (shaderName.empty()) {
    throw std::runtime_error(
        "getOrCreateRenderPipeline: IShader::getShaderName() is empty");
  }

  std::vector<PipelineSlotDetails> slots;
  PushConstantDetails pushConstants{};
  if (shaderName == "blinnphong_0") {
    slots = blinnPhongForwardSlots();
    pushConstants = blinnPhongPushConstants();
  } else {
    throw std::runtime_error("No pipeline slot table for shader: " + shaderName);
  }

  VkExtent2D dummyExtent{1, 1};
  VulkanPipelinePtr pipeline = VulkanShaderGraphicsPipeline::create(
      m_device, dummyExtent, shaderName, vb->getLayout(), std::move(slots),
      pushConstants, ib->getTopology());
  pipeline->loadShaders();
  pipeline->createLayout();
  pipeline->buildGraphicsPpl(m_renderPass->getHandle());

  VulkanPipeline *raw = pipeline.get();
  m_pipelines[item.pipelineKey] = std::move(pipeline);
  return *raw;
}

} // namespace LX_core::backend