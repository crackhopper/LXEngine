#pragma once
#include "core/platform/types.hpp"
#include <memory>
#include <vulkan/vulkan.h>

namespace LX_core {
namespace backend {

class VulkanCommandBuffer;
class VulkanBuffer;
class VulkanDevice;


class VulkanTexture;
using VulkanTextureUniquePtr = std::unique_ptr<VulkanTexture>;

class VulkanImageView final {
public:
  VulkanImageView(VkDevice device, VkImageView imageView);
  ~VulkanImageView();

  VulkanImageView(const VulkanImageView &) = delete;
  VulkanImageView &operator=(const VulkanImageView &) = delete;
  VulkanImageView(VulkanImageView &&other) noexcept;
  VulkanImageView &operator=(VulkanImageView &&other) noexcept;

  VkImageView getHandle() const { return m_imageView; }

private:
  void destroy();

  VkDevice m_device = VK_NULL_HANDLE;
  VkImageView m_imageView = VK_NULL_HANDLE;
};

class VulkanTexture {
  struct Token {};

public:
  VulkanTexture(Token, VulkanDevice &device, u32 width, u32 height,
                VkFormat format,
                VkImageUsageFlags usage,
                VkFilter filter);
  VulkanTexture(Token, VulkanDevice &device, u32 width, u32 height, VkFormat format,
                VkImageUsageFlags usage,
                VkImageAspectFlags aspectMask);
  ~VulkanTexture();

  static VulkanTextureUniquePtr create(VulkanDevice &device,
                                       u32 width, u32 height, VkFormat format,
                                       VkImageUsageFlags usage,
                                       VkFilter filter = VK_FILTER_LINEAR) {
    return std::make_unique<VulkanTexture>(Token{}, device, width, height,
                                           format, usage, filter);
  }

  static VulkanTextureUniquePtr create2D(
      VulkanDevice &device, u32 width, u32 height, VkFormat format,
      VkImageUsageFlags usage, u32 mipLevels = 1,
      VkFilter filter = VK_FILTER_LINEAR);

  static VulkanTextureUniquePtr createForAttachment(
      VulkanDevice &device, u32 width, u32 height,
      VkFormat format, VkImageUsageFlags usage,
      VkImageAspectFlags aspectMask);

  static VulkanTextureUniquePtr createCube(
      VulkanDevice &device, u32 width, u32 height, VkFormat format,
      VkImageUsageFlags usage, u32 mipLevels = 1,
      VkFilter filter = VK_FILTER_LINEAR);

  // 用于 Descriptor Set 绑定的信息
  VkDescriptorImageInfo getDescriptorInfo() const {
    return {m_sampler, m_imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  }

  VkImage getHandle() const { return m_image; }
  VkImageView getImageView() const { return m_imageView; }
  VkSampler getSampler() const { return m_sampler; }
  VkImageLayout getCurrentLayout() const { return m_currentLayout; }
  VkImageUsageFlags getUsage() const { return m_usage; }
  VulkanImageView createSubresourceView(
      VkImageAspectFlags aspectMask, u32 baseMipLevel, u32 levelCount,
      u32 baseArrayLayer, u32 layerCount,
      VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D) const;

  void transitionLayout(VulkanCommandBuffer &cmd, VkImageLayout oldLayout,
                        VkImageLayout newLayout, VkPipelineStageFlags pipelineStage,
                        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT);
  void copyFromBuffer(VulkanCommandBuffer &cmd, class VulkanBuffer &buffer);

  VkFormat getFormat() const { return m_format; }
  u32 getWidth() const { return m_width; }
  u32 getHeight() const { return m_height; }
  u32 getMipLevels() const { return m_mipLevels; }
  u32 getArrayLayers() const { return m_arrayLayers; }
  VkDeviceMemory getMemory() const { return m_memory; }

private:
  VulkanTexture(Token, VulkanDevice &device, u32 width, u32 height,
                VkFormat format, VkImageUsageFlags usage,
                VkImageAspectFlags aspectMask, u32 mipLevels, u32 arrayLayers,
                VkImageViewType viewType, VkImageCreateFlags flags,
                VkFilter filter, VkSamplerAddressMode addressMode);

  void createImageView(VkImageAspectFlags aspectMask);
  void createSampler(VkFilter filter, VkSamplerAddressMode addressMode);

  VkDevice m_device = VK_NULL_HANDLE;
  VkImage m_image = VK_NULL_HANDLE;
  VkDeviceMemory m_memory = VK_NULL_HANDLE;
  VkImageView m_imageView = VK_NULL_HANDLE;
  VkSampler m_sampler = VK_NULL_HANDLE;
  VkFormat m_format = VK_FORMAT_UNDEFINED;
  VkImageUsageFlags m_usage = 0;
  u32 m_width = 0;
  u32 m_height = 0;
  u32 m_mipLevels = 1;
  u32 m_arrayLayers = 1;
  VkImageViewType m_viewType = VK_IMAGE_VIEW_TYPE_2D;
  VkImageLayout m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

} // namespace backend
} // namespace LX_core
