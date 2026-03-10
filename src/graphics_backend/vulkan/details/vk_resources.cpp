#include "vk_resources.hpp"
#include "core/resources/mesh.hpp"
#include "core/resources/texture.hpp"
#include "vk_cmdbuffer.hpp"
#include "vk_device.hpp"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace LX_core::graphic_backend {

// ============================================================
//  VulkanBuffer (保持原有正确实现)
// ============================================================

VulkanBuffer::VulkanBuffer(Token, VulkanDevice &_device, VkDeviceSize _size,
                           VkBufferUsageFlags _usage,
                           VkMemoryPropertyFlags properties)
    : device(_device.getHandle()), size(_size), usage(_usage) {

  VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufInfo.size = _size;
  bufInfo.usage = _usage;
  bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(device, &bufInfo, nullptr, &buffer) != VK_SUCCESS)
    throw std::runtime_error("Failed to create buffer");

  VkMemoryRequirements memReqs;
  vkGetBufferMemoryRequirements(device, buffer, &memReqs);

  VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex =
      _device.findMemoryTypeIndex(memReqs.memoryTypeBits, properties);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
    throw std::runtime_error("Failed to allocate buffer memory");

  vkBindBufferMemory(device, buffer, memory, 0);
}

VulkanBuffer::~VulkanBuffer() {
  if (buffer != VK_NULL_HANDLE)
    vkDestroyBuffer(device, buffer, nullptr);
  if (memory != VK_NULL_HANDLE)
    vkFreeMemory(device, memory, nullptr);
}

void *VulkanBuffer::map() {
  void *mapped = nullptr;
  vkMapMemory(device, memory, 0, size, 0, &mapped);
  return mapped;
}

void VulkanBuffer::unmap() { vkUnmapMemory(device, memory); }

void VulkanBuffer::uploadData(const void *data, VkDeviceSize dataSize) {
  void *mapped = map();
  std::memcpy(mapped, data, dataSize);
  unmap();
}

void VulkanBuffer::copyTo(VulkanCommandBuffer &cmdBuffer, VulkanBuffer &dst) {
  VkBufferCopy region{};
  region.size = size;
  vkCmdCopyBuffer(cmdBuffer.getHandle(), buffer, dst.buffer, 1, &region);
}

// ============================================================
//  VulkanImage
// ============================================================

VulkanImage::VulkanImage(Token, VulkanDevice &device, uint32_t width,
                         uint32_t height, VkFormat format,
                         VkImageUsageFlags usage,
                         VkMemoryPropertyFlags properties)
    : mDevice(device.getHandle()), mFormat(format), mWidth(width),
      mHeight(height) {

  VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent = {width, height, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(mDevice, &imageInfo, nullptr, &mImage) != VK_SUCCESS)
    throw std::runtime_error("Failed to create image");

  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(mDevice, mImage, &memReqs);

  VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex =
      device.findMemoryTypeIndex(memReqs.memoryTypeBits, properties);

  if (vkAllocateMemory(mDevice, &allocInfo, nullptr, &mMemory) != VK_SUCCESS)
    throw std::runtime_error("Failed to allocate image memory");

  vkBindImageMemory(mDevice, mImage, mMemory, 0);
}

VulkanImage::~VulkanImage() {
  if (mImage != VK_NULL_HANDLE)
    vkDestroyImage(mDevice, mImage, nullptr);
  if (mMemory != VK_NULL_HANDLE)
    vkFreeMemory(mDevice, mMemory, nullptr);
}

void VulkanImage::transitionLayout(VulkanCommandBuffer &cmd,
                                   VkImageLayout oldLayout,
                                   VkImageLayout newLayout) {
  VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = mImage;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags srcStage;
  VkPipelineStageFlags dstStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else {
    srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  }

  vkCmdPipelineBarrier(cmd.getHandle(), srcStage, dstStage, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
}

void VulkanImage::copyFromBuffer(VulkanCommandBuffer &cmd,
                                 VulkanBuffer &buffer) {
  VkBufferImageCopy region{};
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.layerCount = 1;
  region.imageExtent = {mWidth, mHeight, 1};

  vkCmdCopyBufferToImage(cmd.getHandle(), buffer.getHandle(), mImage,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

// ============================================================
//  VulkanImageView
// ============================================================

VulkanImageView::VulkanImageView(Token, const VulkanDevice &device,
                                 const VulkanImage &image,
                                 VkImageAspectFlags aspectFlags,
                                 uint32_t mipLevels, VkImageViewType viewType)
    : mDevice(device.getHandle()) {

  VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.image = image.getHandle();
  viewInfo.viewType = viewType;
  viewInfo.format = image.getFormat();
  viewInfo.subresourceRange.aspectMask = aspectFlags;
  viewInfo.subresourceRange.levelCount = mipLevels;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(mDevice, &viewInfo, nullptr, &mView) != VK_SUCCESS)
    throw std::runtime_error("Failed to create image view");
}

VulkanImageView::~VulkanImageView() {
  if (mView != VK_NULL_HANDLE)
    vkDestroyImageView(mDevice, mView, nullptr);
}

// ============================================================
//  VulkanSampler
// ============================================================

VulkanSampler::VulkanSampler(Token, const VulkanDevice &device,
                             VkFilter magFilter, VkFilter minFilter)
    : mDevice(device.getHandle()) {

  VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  samplerInfo.magFilter = magFilter;
  samplerInfo.minFilter = minFilter;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxAnisotropy = 1.0f;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

  if (vkCreateSampler(mDevice, &samplerInfo, nullptr, &mSampler) != VK_SUCCESS)
    throw std::runtime_error("Failed to create sampler");
}

VulkanSampler::~VulkanSampler() {
  if (mSampler != VK_NULL_HANDLE)
    vkDestroySampler(mDevice, mSampler, nullptr);
}

// ============================================================
//  VulkanUniformBuffer
// ============================================================

VulkanUniformBuffer::VulkanUniformBuffer(Token, VulkanDevice &device,
                                         VkDeviceSize size)
    : mDevice(device.getHandle()), mSize(size) {

  VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufInfo.size = size;
  bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(mDevice, &bufInfo, nullptr, &mBuffer) != VK_SUCCESS)
    throw std::runtime_error("Failed to create uniform buffer");

  VkMemoryRequirements memReqs;
  vkGetBufferMemoryRequirements(mDevice, mBuffer, &memReqs);

  VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex = device.findMemoryTypeIndex(
      memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (vkAllocateMemory(mDevice, &allocInfo, nullptr, &mMemory) != VK_SUCCESS)
    throw std::runtime_error("Failed to allocate uniform memory");

  vkBindBufferMemory(mDevice, mBuffer, mMemory, 0);
  vkMapMemory(mDevice, mMemory, 0, mSize, 0, &mMapped);
}

VulkanUniformBuffer::~VulkanUniformBuffer() {
  if (mMapped)
    vkUnmapMemory(mDevice, mMemory);
  if (mBuffer != VK_NULL_HANDLE)
    vkDestroyBuffer(mDevice, mBuffer, nullptr);
  if (mMemory != VK_NULL_HANDLE)
    vkFreeMemory(mDevice, mMemory, nullptr);
}

void VulkanUniformBuffer::update(const void *data, VkDeviceSize size) {
  if (mMapped)
    std::memcpy(mMapped, data, size);
}



} // namespace LX_core::graphic_backend