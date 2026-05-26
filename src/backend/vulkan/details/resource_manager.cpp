#include "resource_manager.hpp"
#include "core/asset/shader.hpp"
#include "core/asset/texture.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/rhi/image_format.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"
#include "core/utils/string_table.hpp"
#include "commands/command_buffer_manager.hpp"
#include "device.hpp"
#include "device_resources/buffer.hpp"
#include "device_resources/texture.hpp"
#include "pipelines/graphics_shader_program.hpp"
#include "render_objects/render_pass.hpp"
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace LX_core::backend {

namespace {
constexpr ResourceCacheIdentity kInactiveFrameGracePeriod = 2;
constexpr int kDebugBurstFrames = 3;

usize cubemapSubresourceKey(u32 mipLevel, u32 faceLayer) {
  return (static_cast<usize>(mipLevel) << 32u) |
         static_cast<usize>(faceLayer);
}

VkFormat toVkFormat(TextureFormat format) {
  switch (format) {
  case TextureFormat::RGBA8:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case TextureFormat::RGB8:
    return VK_FORMAT_R8G8B8_UNORM;
  case TextureFormat::R8:
    return VK_FORMAT_R8_UNORM;
  case TextureFormat::RGBA16Float:
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case TextureFormat::RGBA32Float:
    return VK_FORMAT_R32G32B32A32_SFLOAT;
  default:
    throw std::runtime_error("Unsupported TextureFormat");
  }
}

VkFormat toVkFormat(LX_core::ImageFormat format) {
  switch (format) {
  case LX_core::ImageFormat::RGBA8:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case LX_core::ImageFormat::RGBA16Float:
    return VK_FORMAT_R16G16B16A16_SFLOAT;
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

usize hashBytes(const void *data, usize size) {
  constexpr usize kFnvOffset = static_cast<usize>(1469598103934665603ull);
  constexpr usize kFnvPrime = static_cast<usize>(1099511628211ull);
  usize hash = kFnvOffset;
  const auto *bytes = static_cast<const std::uint8_t *>(data);
  for (usize i = 0; i < size; ++i) {
    hash ^= static_cast<usize>(bytes[i]);
    hash *= kFnvPrime;
  }
  return hash;
}

template <typename T>
bool shouldLogBurst(const T &next, T &state, int &remainingFrames) {
  if (!(next == state)) {
    state = next;
    remainingFrames = kDebugBurstFrames;
    return true;
  }
  if (remainingFrames > 0) {
    --remainingFrames;
    return true;
  }
  return false;
}

std::optional<usize>
bufferHandleToken(const std::shared_ptr<VulkanAnyResource> &gpuRes) {
  if (!gpuRes) {
    return std::nullopt;
  }
  if (const auto bufferPtr = std::get_if<VulkanBufferUniquePtr>(gpuRes.get())) {
    if (*bufferPtr) {
      return std::hash<VkBuffer>{}((*bufferPtr)->getHandle());
    }
  }
  return std::nullopt;
}

void logCameraUploadIfChanged(std::string_view reason,
                              const IGpuResourceSharedPtr &cpuRes,
                              const std::shared_ptr<VulkanAnyResource> &gpuRes,
                              u32 currentFrameIndex) {
  if (!expRendererDebugEnabled() || !cpuRes) {
    return;
  }

  const StringID bindingName = cpuRes->getBindingName();
  const std::string &name = GlobalStringTable::get().getName(bindingName.id);
  if (name != "CameraUBO") {
    return;
  }

  struct UploadLogState {
    usize dataHash = 0;
    usize handleToken = 0;

    bool operator==(const UploadLogState &other) const = default;
  };
  struct UploadLogEntry {
    UploadLogState state{};
    int remainingFrames = 0;
  };
  static std::unordered_map<ResourceCacheIdentity, UploadLogEntry> logged;

  const usize dataHash = hashBytes(cpuRes->getRawData(), cpuRes->getByteSize());
  const usize handleToken = bufferHandleToken(gpuRes).value_or(0);
  const UploadLogState next{dataHash, handleToken};
  auto &entry = logged[cpuRes->getBackendCacheIdentity()];
  if (!shouldLogBurst(next, entry.state, entry.remainingFrames)) {
    return;
  }

  std::cerr << "[RendererDebug] syncResource: " << reason
            << " frameSlot=" << currentFrameIndex << " name=" << name
            << " identity=" << cpuRes->getBackendCacheIdentity()
            << " byteSize=" << cpuRes->getByteSize() << " dataHash=" << dataHash
            << " bufferToken=" << handleToken << std::endl;
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

void VulkanResourceManager::beginFrame(u32 currentFrameIndex) {
  m_currentFrameIndex = currentFrameIndex;
}

void VulkanResourceManager::syncResource(
    VulkanCommandBufferManager &cmdBufferManager,
    const IGpuResourceSharedPtr &cpuRes) {
  if (!cpuRes)
    return;

  if (cpuRes->getType() == ResourceType::Special) {
    return;
  }

  const ResourceCacheIdentity identity = cpuRes->getBackendCacheIdentity();
  if (m_textureAliases.find(identity) != m_textureAliases.end()) {
    m_activeResourceIds.insert(identity);
    return;
  }
  m_activeResourceIds.insert(identity);

  auto it = m_gpuResources.find(identity);
  if (it == m_gpuResources.end()) {
    CachedGpuResource entry;
    entry.resource = createGpuResource(cpuRes);
    entry.lastSeenFrame = m_frameSerial;
    auto [insertedIt, inserted] =
        m_gpuResources.emplace(identity, std::move(entry));
    (void)inserted;
    updateGpuResource(insertedIt->second.resource, cpuRes, cmdBufferManager);
    logCameraUploadIfChanged("create", cpuRes, insertedIt->second.resource,
                             m_currentFrameIndex);
    cpuRes->clearDirty();
    return;
  }

  it->second.lastSeenFrame = m_frameSerial;

  if (cpuRes->isDirty()) {
    updateGpuResource(it->second.resource, cpuRes, cmdBufferManager);
    logCameraUploadIfChanged("update", cpuRes, it->second.resource,
                             m_currentFrameIndex);
    cpuRes->clearDirty();
  }
}

std::shared_ptr<VulkanAnyResource>
VulkanResourceManager::createGpuResource(const IGpuResourceSharedPtr &cpuRes) {
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
    if (desc.dimension == TextureDimension::TextureCube) {
      return std::make_shared<VulkanAnyResource>(VulkanTexture::createCube(
          m_device, desc.width, desc.height, vkFormat, usage, desc.mipLevels,
          VK_FILTER_LINEAR));
    }
    return std::make_shared<VulkanAnyResource>(VulkanTexture::create2D(
        m_device, desc.width, desc.height, vkFormat, usage, desc.mipLevels,
        VK_FILTER_LINEAR));
  }

  default:
    throw std::runtime_error("Unsupported resource type for GPU creation");
  }
}

void VulkanResourceManager::updateGpuResource(
    std::shared_ptr<VulkanAnyResource> &gpuRes,
    const IGpuResourceSharedPtr &cpuRes,
    VulkanCommandBufferManager &cmdBufferManager) {
  std::visit(
      [&](auto &&res) {
        using T = std::decay_t<decltype(res)>;
        if constexpr (std::is_same_v<T, VulkanBufferUniquePtr>) {
          // 如果是 Host Visible (Uniform)，直接 map/memcpy
          // 如果是 Device Local (Vertex/Index)，初级架构建议直接
          // uploadData（内部处理 staging）
          res->uploadData(cpuRes->getRawData(), cpuRes->getByteSize());
        } else if constexpr (std::is_same_v<T, VulkanTextureUniquePtr>) {
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
    if (m_activeResourceIds.find(it->first) == m_activeResourceIds.end() &&
        (m_frameSerial - it->second.lastSeenFrame) >=
            kInactiveFrameGracePeriod) {
      it = m_gpuResources.erase(it);
    } else {
      ++it;
    }
  }
  m_activeResourceIds.clear();
  ++m_frameSerial;
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
    if (auto resPtr = std::get_if<VariantType>(&(*(it->second.resource)))) {   \
      return std::ref(*(resPtr->get()));                                       \
    }                                                                          \
  }                                                                            \
  return std::nullopt;

std::optional<std::reference_wrapper<VulkanBuffer>>
VulkanResourceManager::getBuffer(ResourceCacheIdentity handle) {
  GET_RESOURCE_IMPL(VulkanBuffer, VulkanBufferUniquePtr);
}

std::optional<std::reference_wrapper<VulkanTexture>>
VulkanResourceManager::getTexture(ResourceCacheIdentity handle) {
  if (const auto aliasIt = m_textureAliases.find(handle);
      aliasIt != m_textureAliases.end()) {
    if (aliasIt->second.kind == VulkanTextureAliasKind::CubemapBake) {
      auto attachment = getCubemapBakeAttachment(aliasIt->second.resourceName);
      if (attachment.has_value() && attachment->get().texture) {
        return std::ref(*attachment->get().texture);
      }
    } else {
      auto attachment = getFrameGraphAttachment(aliasIt->second.resourceName);
      if (attachment.has_value() && attachment->get().texture) {
        return std::ref(*attachment->get().texture);
      }
    }
    return std::nullopt;
  }
  GET_RESOURCE_IMPL(VulkanTexture, VulkanTextureUniquePtr);
}

void VulkanResourceManager::aliasCubemapBakeTextureResource(
    const IGpuResourceSharedPtr &cpuRes, StringID attachmentName) {
  if (!cpuRes) {
    throw std::runtime_error("Cannot alias a null texture resource");
  }
  if (!getCubemapBakeAttachment(attachmentName).has_value()) {
    const std::string &resourceName =
        GlobalStringTable::get().getName(attachmentName.id);
    throw std::runtime_error("Missing cubemap bake attachment '" +
                             resourceName + "'");
  }
  m_textureAliases[cpuRes->getBackendCacheIdentity()] =
      VulkanTextureAlias{VulkanTextureAliasKind::CubemapBake, attachmentName};
}

void VulkanResourceManager::aliasFrameGraphTextureResource(
    const IGpuResourceSharedPtr &cpuRes, StringID attachmentName) {
  if (!cpuRes) {
    throw std::runtime_error("Cannot alias a null texture resource");
  }
  if (!getFrameGraphAttachment(attachmentName).has_value()) {
    const std::string &resourceName =
        GlobalStringTable::get().getName(attachmentName.id);
    throw std::runtime_error("Missing frame graph texture attachment '" +
                             resourceName + "'");
  }
  m_textureAliases[cpuRes->getBackendCacheIdentity()] = VulkanTextureAlias{
      VulkanTextureAliasKind::FrameGraphAttachment, attachmentName};
}

VulkanRenderPass &VulkanResourceManager::getRenderPass() {
  return *m_renderPass;
}

VulkanRenderPass &
VulkanResourceManager::getRenderPass(const RenderTargetDesc &target) {
  if (target.role == RenderTargetRole::Swapchain) {
    return *m_renderPass;
  }

  const usize hash = target.getHash();
  auto it = m_frameGraphRenderPasses.find(hash);
  if (it != m_frameGraphRenderPasses.end()) {
    return *it->second;
  }

  std::optional<VkFormat> colorFormat;
  std::optional<VkFormat> depthFormat;
  if (target.colorFormat.has_value()) {
    colorFormat = toVkFormat(*target.colorFormat);
  }
  if (target.depthFormat.has_value()) {
    depthFormat = toVkFormat(*target.depthFormat);
  }

  auto renderPass =
      VulkanRenderPass::create(m_device, colorFormat, depthFormat, false);
  auto [insertedIt, inserted] =
      m_frameGraphRenderPasses.emplace(hash, std::move(renderPass));
  (void)inserted;
  return *insertedIt->second;
}

VulkanPipeline &VulkanResourceManager::getOrCreateRenderPipeline(
    const LX_core::RenderingItem &item) {
  return m_pipelineCache->getOrCreate(
      LX_core::PipelineBuildDesc::fromRenderingItem(item),
      getRenderPass(item.target).getHandle());
}

void VulkanResourceManager::preloadPipelines(
    const std::vector<LX_core::PipelineBuildDesc> &infos) {
  for (const auto &info : infos) {
    m_pipelineCache->preload({info}, getRenderPass(info.target).getHandle());
  }
}

VulkanFrameGraphAttachment &
VulkanResourceManager::createOrGetFrameGraphAttachment(
    StringID name, VkExtent2D extent, VkFormat format,
    VkImageAspectFlags aspect, VkImageUsageFlags usage) {
  const VulkanFrameGraphAttachmentKey key{name, m_currentFrameIndex};
  auto it = m_frameGraphAttachments.find(key);
  if (it != m_frameGraphAttachments.end()) {
    const auto &attachment = it->second;
    if (attachment.format != format || attachment.aspect != aspect ||
        attachment.extent.width != extent.width ||
        attachment.extent.height != extent.height ||
        (attachment.usage & usage) != usage) {
      const std::string &resourceName =
          GlobalStringTable::get().getName(name.id);
      throw std::runtime_error(
          "Frame graph attachment reuse mismatch for resource '" +
          resourceName + "' frame " + std::to_string(m_currentFrameIndex) +
          "; format/aspect/extent must match and requested usage must be a "
          "subset of existing usage");
    }
    return it->second;
  }

  VulkanFrameGraphAttachment attachment;
  attachment.texture = VulkanTexture::createForAttachment(
      m_device, extent.width, extent.height, format, usage, aspect);
  attachment.format = format;
  attachment.aspect = aspect;
  attachment.usage = usage;
  attachment.currentLayout = attachment.texture->getCurrentLayout();
  attachment.extent = extent;

  auto [insertedIt, inserted] =
      m_frameGraphAttachments.emplace(key, std::move(attachment));
  (void)inserted;
  return insertedIt->second;
}

std::optional<std::reference_wrapper<VulkanFrameGraphAttachment>>
VulkanResourceManager::getFrameGraphAttachment(StringID name) {
  const VulkanFrameGraphAttachmentKey key{name, m_currentFrameIndex};
  auto it = m_frameGraphAttachments.find(key);
  if (it == m_frameGraphAttachments.end()) {
    return std::nullopt;
  }
  return std::ref(it->second);
}

void VulkanResourceManager::updateFrameGraphAttachmentLayout(
    StringID name, VkImageLayout layout) {
  auto attachment = getFrameGraphAttachment(name);
  if (!attachment.has_value()) {
    const std::string &resourceName = GlobalStringTable::get().getName(name.id);
    throw std::runtime_error("Missing frame graph attachment '" + resourceName +
                             "' for frame " +
                             std::to_string(m_currentFrameIndex));
  }
  attachment->get().currentLayout = layout;
}

void VulkanResourceManager::clearFrameGraphAttachments() {
  m_frameGraphAttachments.clear();
}

VulkanCubemapBakeAttachment &
VulkanResourceManager::createOrGetCubemapBakeAttachment(
    StringID name, VkExtent2D baseExtent, VkFormat format, u32 mipLevels,
    VkImageUsageFlags usage) {
  if (baseExtent.width == 0 || baseExtent.height == 0 || mipLevels == 0) {
    throw std::runtime_error(
        "Cubemap bake attachment requires non-zero extent and mip count");
  }

  auto it = m_cubemapBakeAttachments.find(name);
  if (it != m_cubemapBakeAttachments.end()) {
    const auto &attachment = it->second;
    if (attachment.format != format ||
        attachment.baseExtent.width != baseExtent.width ||
        attachment.baseExtent.height != baseExtent.height ||
        attachment.mipLevels != mipLevels ||
        (attachment.usage & usage) != usage) {
      const std::string &resourceName =
          GlobalStringTable::get().getName(name.id);
      throw std::runtime_error(
          "Cubemap bake attachment reuse mismatch for resource '" +
          resourceName +
          "'; format/extent/mips must match and requested usage must be a "
          "subset of existing usage");
    }
    return it->second;
  }

  VulkanCubemapBakeAttachment attachment;
  attachment.texture = VulkanTexture::createCube(m_device, baseExtent.width,
                                                 baseExtent.height, format,
                                                 usage, mipLevels,
                                                 VK_FILTER_LINEAR);
  attachment.format = format;
  attachment.usage = usage;
  attachment.baseExtent = baseExtent;
  attachment.mipLevels = mipLevels;

  auto [insertedIt, inserted] =
      m_cubemapBakeAttachments.emplace(name, std::move(attachment));
  (void)inserted;
  return insertedIt->second;
}

std::optional<std::reference_wrapper<VulkanCubemapBakeAttachment>>
VulkanResourceManager::getCubemapBakeAttachment(StringID name) {
  auto it = m_cubemapBakeAttachments.find(name);
  if (it == m_cubemapBakeAttachments.end()) {
    return std::nullopt;
  }
  return std::ref(it->second);
}

VulkanImageView &VulkanResourceManager::getOrCreateCubemapBakeSubresourceView(
    StringID name, u32 mipLevel, u32 faceLayer) {
  auto attachmentOpt = getCubemapBakeAttachment(name);
  if (!attachmentOpt.has_value()) {
    const std::string &resourceName = GlobalStringTable::get().getName(name.id);
    throw std::runtime_error("Missing cubemap bake attachment '" +
                             resourceName + "'");
  }
  auto &attachment = attachmentOpt->get();
  if (mipLevel >= attachment.mipLevels || faceLayer >= 6u) {
    const std::string &resourceName = GlobalStringTable::get().getName(name.id);
    throw std::runtime_error("Cubemap bake subresource out of range for '" +
                             resourceName + "'");
  }

  const usize key = cubemapSubresourceKey(mipLevel, faceLayer);
  auto viewIt = attachment.subresourceViews.find(key);
  if (viewIt != attachment.subresourceViews.end()) {
    return *viewIt->second;
  }

  auto view = std::make_unique<VulkanImageView>(
      attachment.texture->createSubresourceView(VK_IMAGE_ASPECT_COLOR_BIT,
                                                mipLevel, 1, faceLayer, 1));
  auto [insertedIt, inserted] =
      attachment.subresourceViews.emplace(key, std::move(view));
  (void)inserted;
  return *insertedIt->second;
}

} // namespace LX_core::backend
