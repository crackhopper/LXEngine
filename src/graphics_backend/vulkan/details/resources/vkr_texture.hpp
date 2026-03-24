#pragma once
#include <memory>
#include <vulkan/vulkan.h>

namespace LX_core {
namespace graphic_backend {

class VulkanCommandBuffer;
class VulkanBuffer;
class VulkanDevice;


class VulkanTexture;
using VulkanTexturePtr = std::unique_ptr<VulkanTexture>;

class VulkanTexture {
  struct Token {};

public:
  VulkanTexture(Token, VulkanDevice &device, uint32_t width,
                uint32_t height, VkFormat format, VkImageUsageFlags usage,
                VkFilter filter);
  ~VulkanTexture();

  static VulkanTexturePtr create(VulkanDevice &device, uint32_t width,
                                 uint32_t height, VkFormat format,
                                 VkImageUsageFlags usage,
                                 VkFilter filter = VK_FILTER_LINEAR) {
    return std::make_unique<VulkanTexture>(Token{}, device, width, height,
                                           format, usage, filter);
  }

  // 用于 Descriptor Set 绑定的信息
  VkDescriptorImageInfo getDescriptorInfo() const {
    return {m_sampler, m_imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  }

  VkImage getHandle() const { return m_image; }
  VkImageView getImageView() const { return m_imageView; }
  VkImageLayout getCurrentLayout() const { return m_currentLayout; }

  void transitionLayout(VulkanCommandBuffer &cmd, VkImageLayout oldLayout,
                        VkImageLayout newLayout);
  void copyFromBuffer(VulkanCommandBuffer &cmd, class VulkanBuffer &buffer);

  VkFormat getFormat() const { return m_format; }
  uint32_t getWidth() const { return m_width; }
  uint32_t getHeight() const { return m_height; }

private:
  void createImageView();
  void createSampler(VkFilter filter);

  VkDevice m_device = VK_NULL_HANDLE;
  VkImage m_image = VK_NULL_HANDLE;
  VkDeviceMemory m_memory = VK_NULL_HANDLE;
  VkImageView m_imageView = VK_NULL_HANDLE;
  VkSampler m_sampler = VK_NULL_HANDLE;
  VkFormat m_format = VK_FORMAT_UNDEFINED;
  uint32_t m_width = 0;
  uint32_t m_height = 0;
  VkImageLayout m_currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
};

} // namespace graphic_backend
} // namespace LX_core