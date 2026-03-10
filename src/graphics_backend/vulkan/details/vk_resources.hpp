#pragma once

#include "vk_device.hpp"
#include <memory>
#include <vulkan/vulkan.h>

namespace LX_core {
class Mesh;
class Texture;
} // namespace LX_core

namespace LX_core::graphic_backend {

class VulkanBuffer;
using VulkanBufferPtr = std::unique_ptr<VulkanBuffer>;
class VulkanCommandBuffer;

class VulkanBuffer {
  struct Token {};

public:
  VulkanBuffer(Token token, VulkanDevice &_device, VkDeviceSize _size,
               VkBufferUsageFlags _usage, VkMemoryPropertyFlags properties);
  ~VulkanBuffer();

  static VulkanBufferPtr create(const VulkanDevice &_device, VkDeviceSize size,
                                VkBufferUsageFlags usage,
                                VkMemoryPropertyFlags properties) {
    return std::make_unique<VulkanBuffer>(Token{}, _device, size, usage,
                                          properties);
  }

  void *map();
  void unmap();

  void uploadData(const void *data, VkDeviceSize dataSize);

  void copyTo(VulkanCommandBuffer &cmdBuffer, VulkanBuffer &dst);

  VkBuffer getHandle() const { return buffer; }

private:
  VkDevice device = VK_NULL_HANDLE;
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  VkBufferUsageFlags usage;
};

class VulkanImage;
using VulkanImagePtr = std::unique_ptr<VulkanImage>;

class VulkanImage {
  struct Token {};

public:
  VulkanImage(Token, VulkanDevice &device, uint32_t width,
              uint32_t height, VkFormat format, VkImageUsageFlags usage,
              VkMemoryPropertyFlags properties);
  ~VulkanImage();

  static VulkanImagePtr create(VulkanDevice &device, uint32_t width,
                               uint32_t height, VkFormat format,
                               VkImageUsageFlags usage,
                               VkMemoryPropertyFlags properties) {
    return std::make_unique<VulkanImage>(Token{}, device, width, height, format,
                                         usage, properties);
  }

  void transitionLayout(VulkanCommandBuffer &cmd, VkImageLayout oldLayout,
                        VkImageLayout newLayout);
  void copyFromBuffer(VulkanCommandBuffer &cmd, class VulkanBuffer &buffer);

  VkImage getHandle() const { return mImage; }
  VkFormat getFormat() const { return mFormat; }
  uint32_t getWidth() const { return mWidth; }
  uint32_t getHeight() const { return mHeight; }

private:
  VkDevice mDevice = VK_NULL_HANDLE;
  VkImage mImage = VK_NULL_HANDLE;
  VkDeviceMemory mMemory = VK_NULL_HANDLE;
  VkFormat mFormat;
  uint32_t mWidth, mHeight;
};

// --- ImageView ---
class VulkanImageView;
using VulkanImageViewPtr = std::unique_ptr<VulkanImageView>;

class VulkanImageView {
  struct Token {};

public:
  VulkanImageView(Token, const VulkanDevice &device, const VulkanImage &image,
                  VkImageAspectFlags aspectFlags, uint32_t mipLevels,
                  VkImageViewType viewType);
  ~VulkanImageView();

  static VulkanImageViewPtr
  create(const VulkanDevice &device, const VulkanImage &image,
         VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT,
         uint32_t mipLevels = 1,
         VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D) {
    return std::make_unique<VulkanImageView>(Token{}, device, image,
                                             aspectFlags, mipLevels, viewType);
  }

  VkImageView getHandle() const { return mView; }

private:
  VkDevice mDevice = VK_NULL_HANDLE;
  VkImageView mView = VK_NULL_HANDLE;
};

class VulkanSampler;
using VulkanSamplerPtr = std::unique_ptr<VulkanSampler>;

class VulkanSampler {
  struct Token {};

public:
  VulkanSampler(Token, const VulkanDevice &device, VkFilter magFilter,
                VkFilter minFilter);
  ~VulkanSampler();

  static VulkanSamplerPtr create(const VulkanDevice &device,
                                 VkFilter magFilter = VK_FILTER_LINEAR,
                                 VkFilter minFilter = VK_FILTER_LINEAR) {
    return std::make_unique<VulkanSampler>(Token{}, device, magFilter,
                                           minFilter);
  }

  VkSampler getHandle() const { return mSampler; }

private:
  VkDevice mDevice = VK_NULL_HANDLE;
  VkSampler mSampler = VK_NULL_HANDLE;
};

// --- UniformBuffer ---
class VulkanUniformBuffer;
using VulkanUniformBufferPtr = std::unique_ptr<VulkanUniformBuffer>;

class VulkanUniformBuffer {
  struct Token {};

public:
  VulkanUniformBuffer(Token, VulkanDevice &device, VkDeviceSize size);
  ~VulkanUniformBuffer();

  static VulkanUniformBufferPtr create(VulkanDevice &device,
                                       VkDeviceSize size) {
    return std::make_unique<VulkanUniformBuffer>(Token{}, device, size);
  }

  void update(const void *data, VkDeviceSize size);
  VkBuffer getHandle() const { return mBuffer; }

private:
  VkDevice mDevice = VK_NULL_HANDLE;
  VkBuffer mBuffer = VK_NULL_HANDLE;
  VkDeviceMemory mMemory = VK_NULL_HANDLE;
  void *mMapped = nullptr;
  VkDeviceSize mSize;
};



} // namespace LX_core::graphic_backend