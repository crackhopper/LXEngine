#pragma once

#include "vk_device.hpp"
#include <memory>
#include <vulkan/vulkan.h>

namespace LX_core::graphic_backend {

using VulkanDeviceWeakPtr = std::weak_ptr<VulkanDevice>;

class VulkanBuffer {
public:
  VulkanBuffer(VulkanDeviceWeakPtr device);
  ~VulkanBuffer();

  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;

private:
  VulkanDeviceWeakPtr m_device;
};

class VulkanImage {
public:
  VulkanImage(VulkanDeviceWeakPtr device);
  ~VulkanImage();

  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;

private:
  VulkanDeviceWeakPtr m_device;
};

class VulkanImageView {
public:
  VulkanImageView(VulkanDeviceWeakPtr device);
  ~VulkanImageView();

  VkImageView view = VK_NULL_HANDLE;

private:
  VulkanDeviceWeakPtr m_device;
};

class VulkanSampler {
public:
  VulkanSampler(VulkanDeviceWeakPtr device);
  ~VulkanSampler();

  VkSampler sampler = VK_NULL_HANDLE;

private:
  VulkanDeviceWeakPtr m_device;
};

class VulkanMesh {
public:
  VulkanMesh(VulkanDeviceWeakPtr device);

  std::unique_ptr<VulkanBuffer> vertexBuffer;
  std::unique_ptr<VulkanBuffer> indexBuffer;

  uint32_t indexCount = 0;

private:
  VulkanDeviceWeakPtr m_device;
};

class VulkanTexture {
public:
  VulkanTexture(VulkanDeviceWeakPtr device);

  std::unique_ptr<VulkanImage> image;
  std::unique_ptr<VulkanImageView> imageView;
  std::unique_ptr<VulkanSampler> sampler;

private:
  VulkanDeviceWeakPtr m_device;
};

class VulkanUniformBuffer {
public:
  VulkanUniformBuffer(VulkanDeviceWeakPtr device);
  ~VulkanUniformBuffer();

  VulkanBufferPtr buffer;

  void update(const void* data, size_t size);

private:
  VulkanDeviceWeakPtr m_device;
};

using VulkanBufferPtr = std::shared_ptr<VulkanBuffer>;
using VulkanImagePtr = std::shared_ptr<VulkanImage>;
using VulkanMeshPtr = std::shared_ptr<VulkanMesh>;
using VulkanTexturePtr = std::shared_ptr<VulkanTexture>;
using VulkanUniformBufferPtr = std::shared_ptr<VulkanUniformBuffer>;

} // namespace LX_core::graphic_backend