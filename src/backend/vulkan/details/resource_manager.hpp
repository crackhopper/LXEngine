#pragma once

#include "core/frame_graph/render_input.hpp"
#include "core/pipeline/pipeline_build_desc.hpp"
#include "core/pipeline/pipeline_key.hpp"
#include "core/rhi/descriptor_resource_ref.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/utils/string_table.hpp"
#include "pipelines/graphics_pipeline.hpp"
#include "pipelines/pipeline_cache.hpp"
#include "pipelines/pipeline_ref.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include <vulkan/vulkan.h>

namespace LX_core::backend {

class VulkanDevice;
class VulkanCommandBufferManager;
class VulkanRenderPass;
class VulkanBuffer;
class VulkanImageView;
class VulkanTexture;
class VulkanShader;

using VulkanBufferUniquePtr = std::unique_ptr<VulkanBuffer>;
using VulkanTextureUniquePtr = std::unique_ptr<VulkanTexture>;

using VulkanAnyResource =
    std::variant<VulkanBufferUniquePtr, VulkanTextureUniquePtr>;

struct VulkanFrameGraphAttachment {
  std::unique_ptr<VulkanTexture> texture;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageAspectFlags aspect = 0;
  VkImageUsageFlags usage = 0;
  VkImageLayout currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkExtent2D extent{};
};

struct VulkanCubemapBakeAttachment {
  std::unique_ptr<VulkanTexture> texture;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageUsageFlags usage = 0;
  VkExtent2D baseExtent{};
  u32 mipLevels = 1;
  std::unordered_map<usize, std::unique_ptr<VulkanImageView>> subresourceViews;
};

struct VulkanFrameGraphAttachmentKey {
  StringID name;
  u32 frameIndex = 0;

  bool operator==(const VulkanFrameGraphAttachmentKey &other) const {
    return name == other.name && frameIndex == other.frameIndex;
  }

  struct Hash {
    usize operator()(const VulkanFrameGraphAttachmentKey &key) const {
      usize hash = StringID::Hash{}(key.name);
      hash ^= static_cast<usize>(key.frameIndex) + 0x9e3779b9u + (hash << 6u) +
              (hash >> 2u);
      return hash;
    }
  };
};

enum class VulkanTextureAliasKind {
  CubemapBake,
  FrameGraphAttachment,
};

struct VulkanTextureAlias {
  VulkanTextureAliasKind kind = VulkanTextureAliasKind::CubemapBake;
  StringID resourceName;
};

class VulkanResourceManager;
using VulkanResourceManagerUniquePtr = std::unique_ptr<VulkanResourceManager>;
class VulkanResourceManager {
  struct Token {};
  struct CachedGpuResource {
    std::unique_ptr<VulkanAnyResource> resource;
    ResourceCacheIdentity lastSeenFrame = 0;
  };

public:
  explicit VulkanResourceManager(Token token, VulkanDevice &device);
  ~VulkanResourceManager();

  static VulkanResourceManagerUniquePtr create(VulkanDevice &device) {
    auto p = std::make_unique<VulkanResourceManager>(Token{}, device);
    return p;
  }

  VulkanResourceManager(const VulkanResourceManager &) = delete;
  VulkanResourceManager &operator=(const VulkanResourceManager &) = delete;

  void syncResource(VulkanCommandBufferManager &cmdBufferManager,
                    const GpuResourceRef &cpuRes);
  void beginFrame(u32 currentFrameIndex);
  void collectGarbage();

  void initializeRenderPassAndPipeline(VkSurfaceFormatKHR surfaceFormat,
                                       VkFormat depthFormat);

  std::optional<std::reference_wrapper<VulkanBuffer>>
  getBuffer(ResourceCacheIdentity identity);
  std::optional<std::reference_wrapper<VulkanTexture>>
  getTexture(ResourceCacheIdentity identity);
  void aliasCubemapBakeTextureResource(const GpuResourceRef &cpuRes,
                                       StringID attachmentName);
  void aliasFrameGraphTextureResource(const GpuResourceRef &cpuRes,
                                      StringID attachmentName);
  VulkanRenderPass &getRenderPass();
  VulkanRenderPass &getRenderPass(const RenderTargetDesc &target);

  VulkanPipelineRef
  getOrCreatePipeline(const LX_core::PipelineBuildDesc &desc);
  VulkanPipelineRef getOrCreatePipeline(const LX_core::RenderInputDesc &desc);

  /// Bulk preload — intended to be called once per scene init from the
  /// VulkanRenderer after building a FrameGraph.
  void preloadPipelines(const std::vector<LX_core::PipelineBuildDesc> &infos);

  PipelineCache &getPipelineCache() { return *m_pipelineCache; }
  usize getCachedResourceCount() const { return m_gpuResources.size(); }
  usize getFrameGraphAttachmentCount() const {
    return m_frameGraphAttachments.size();
  }
  usize getCubemapBakeAttachmentCount() const {
    return m_cubemapBakeAttachments.size();
  }

  VulkanFrameGraphAttachment &
  createOrGetFrameGraphAttachment(StringID name, VkExtent2D extent,
                                  VkFormat format, VkImageAspectFlags aspect,
                                  VkImageUsageFlags usage);
  std::optional<std::reference_wrapper<VulkanFrameGraphAttachment>>
  getFrameGraphAttachment(StringID name);
  void updateFrameGraphAttachmentLayout(StringID name, VkImageLayout layout);
  void clearFrameGraphAttachments();

  VulkanCubemapBakeAttachment &
  createOrGetCubemapBakeAttachment(StringID name, VkExtent2D baseExtent,
                                   VkFormat format, u32 mipLevels,
                                   VkImageUsageFlags usage);
  std::optional<std::reference_wrapper<VulkanCubemapBakeAttachment>>
  getCubemapBakeAttachment(StringID name);
  VulkanImageView &getOrCreateCubemapBakeSubresourceView(StringID name,
                                                         u32 mipLevel,
                                                         u32 faceLayer);

private:
  std::unique_ptr<VulkanAnyResource> createGpuResource(const IGpuResource &cpuRes);
  void updateGpuResource(VulkanAnyResource &gpuRes,
                         const IGpuResource &cpuRes,
                         VulkanCommandBufferManager &cmdBufferManager);

  VulkanDevice &m_device;
  std::unordered_map<ResourceCacheIdentity, CachedGpuResource> m_gpuResources;
  std::unordered_set<ResourceCacheIdentity> m_activeResourceIds;
  ResourceCacheIdentity m_frameSerial = 0;
  u32 m_currentFrameIndex = 0;

  std::unique_ptr<VulkanRenderPass> m_renderPass;
  std::unique_ptr<PipelineCache> m_pipelineCache;
  std::unordered_map<usize, std::unique_ptr<VulkanRenderPass>>
      m_frameGraphRenderPasses;
  std::unordered_map<VulkanFrameGraphAttachmentKey, VulkanFrameGraphAttachment,
                     VulkanFrameGraphAttachmentKey::Hash>
      m_frameGraphAttachments;
  std::unordered_map<StringID, VulkanCubemapBakeAttachment, StringID::Hash>
      m_cubemapBakeAttachments;
  std::unordered_map<ResourceCacheIdentity, VulkanTextureAlias>
      m_textureAliases;
};

} // namespace LX_core::backend
