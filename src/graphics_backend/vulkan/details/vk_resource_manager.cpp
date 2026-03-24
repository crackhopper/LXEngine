#include "vk_resource_manager.hpp"
#include "vk_device.hpp"
#include "commands/vkc_cmdbuffer_manager.hpp"
#include "pipelines/vkp_blinnphong.hpp"
#include "resources/vkr_buffer.hpp"
#include "resources/vkr_texture.hpp"
#include "resources/vkr_shader.hpp"
#include "render_objects/vkr_renderpass.hpp"
#include "core/resources/shader.hpp"
#include "core/resources/texture.hpp"
#include <stdexcept>

namespace LX_core::graphic_backend {

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

  // RenderPass/Pipeline are stored as raw pointers in this manager.
  delete m_pipeline;
  m_pipeline = nullptr;

  delete m_renderPass;
  m_renderPass = nullptr;
}

void VulkanResourceManager::syncResource(const IRenderResourcePtr &cpuRes) {
  if (!cpuRes)
    return;

  // Push constants are written directly into command buffers; no Vulkan object needed.
  if (cpuRes->getType() == ResourceType::PushConstant) {
    return;
  }

  void *handle = cpuRes->getResourceHandle();
  m_activeHandles.insert(handle);

  auto it = m_gpuResources.find(handle);
  if (it == m_gpuResources.end()) {
    m_gpuResources[handle] = createGpuResource(cpuRes);
    // 新创建的资源强制更新一次数据
    updateGpuResource(m_gpuResources[handle], cpuRes);
    cpuRes->clearDirty();
  } else if (cpuRes->isDirty()) {
    updateGpuResource(it->second, cpuRes);
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

  case ResourceType::VertexShader:
  case ResourceType::FragmentShader: {
    VkShaderStageFlagBits stage = (type == ResourceType::VertexShader)
                                      ? VK_SHADER_STAGE_VERTEX_BIT
                                      : VK_SHADER_STAGE_FRAGMENT_BIT;
    auto shaderCpu = std::dynamic_pointer_cast<Shader>(cpuRes);
    const std::string name =
        shaderCpu ? shaderCpu->getShaderName() : std::string{};
    return std::make_shared<VulkanAnyResource>(
        VulkanShader::create(m_device, name, stage));
  }

  case ResourceType::CombinedImageSampler: {
    auto texCpu = std::dynamic_pointer_cast<CombinedTextureSampler>(cpuRes);
    if (!texCpu || !texCpu->texture()) {
      throw std::runtime_error("CombinedImageSampler resource missing texture data");
    }
    const auto &desc = texCpu->texture()->desc();
    const VkFormat vkFormat = toVkFormat(desc.format);
    VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    return std::make_shared<VulkanAnyResource>(
        VulkanTexture::create(m_device, desc.width, desc.height, vkFormat,
                               usage, VK_FILTER_LINEAR));
  }

  default:
    throw std::runtime_error("Unsupported resource type for GPU creation");
  }
}

void VulkanResourceManager::updateGpuResource(
    std::shared_ptr<VulkanAnyResource> &gpuRes,
    const IRenderResourcePtr &cpuRes) {
  std::visit(
      [&](auto &&res) {
        using T = std::decay_t<decltype(res)>;
        if constexpr (std::is_same_v<T, VulkanBufferPtr>) {
          // 如果是 Host Visible (Uniform)，直接 map/memcpy
          // 如果是 Device Local (Vertex/Index)，初级架构建议直接
          // uploadData（内部处理 staging）
          res->uploadData(cpuRes->getRawData(), cpuRes->getByteSize());
        } else if constexpr (std::is_same_v<T, VulkanTexturePtr>) {
          if (!m_cmdBufferMgr) {
            // Without a command-buffer manager we can't transition/upload.
            return;
          }

          const VkDeviceSize imageSize =
              static_cast<VkDeviceSize>(cpuRes->getByteSize());

          // Staging buffer in host-visible memory.
          auto staging = VulkanBuffer::create(
              m_device, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
          staging->uploadData(cpuRes->getRawData(), imageSize);

          auto cmd = m_cmdBufferMgr->beginSingleTimeCommands();

          // Upload the texture contents.
          res->transitionLayout(cmd, res->getCurrentLayout(),
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
          res->copyFromBuffer(cmd, *staging);
          res->transitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

          m_cmdBufferMgr->endSingleTimeCommands(cmd, m_device.getGraphicsQueue());
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
  if (m_renderPass && m_pipeline) {
    return;
  }

  // Render pass depends on the swapchain image format.
  m_renderPass = VulkanRenderPass::create(m_device, surfaceFormat.format, depthFormat).release();

  // Pipeline viewport/scissor values are overwritten dynamically each frame,
  // so we can use a small dummy extent here.
  VkExtent2D dummyExtent{1, 1};
  m_pipeline = VkPipelineBlinnPhong::create(m_device, dummyExtent).release();

  // Build the actual VkPipeline object (layout/shaders are created above).
  // This is required before vkCmdBindPipeline can use a valid handle.
  m_pipeline->buildGraphicsPpl(m_renderPass->getHandle());
}

// 辅助查找宏，简化代码
#define GET_RESOURCE_IMPL(ReturnType, VariantType)                             \
  auto it = m_gpuResources.find(handle);                                       \
  if (it != m_gpuResources.end()) {                                            \
    if (auto resPtr = std::get_if<VariantType>(&(*(it->second)))) {            \
      return resPtr->get();                                                    \
    }                                                                          \
  }                                                                            \
  return nullptr;

VulkanBuffer *VulkanResourceManager::getBuffer(void *handle) {
  GET_RESOURCE_IMPL(VulkanBuffer *, VulkanBufferPtr);
}

VulkanTexture *VulkanResourceManager::getTexture(void *handle) {
  GET_RESOURCE_IMPL(VulkanTexture *, VulkanTexturePtr);
}

VulkanShader *VulkanResourceManager::getShader(void *handle) {
  GET_RESOURCE_IMPL(VulkanShader *, VulkanShaderPtr);
}

VulkanRenderPass *VulkanResourceManager::getRenderPass() {
  return m_renderPass;
}

VulkanPipelineBase *VulkanResourceManager::getRenderPipeline() {
  return m_pipeline;
}

} // namespace LX_core::graphic_backend