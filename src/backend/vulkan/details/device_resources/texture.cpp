#include "texture.hpp"
#include "../commands/command_buffer.hpp"
#include "../device.hpp"
#include "buffer.hpp"
#include <algorithm>
#include <stdexcept>

namespace LX_core {
namespace backend {

namespace {
bool formatHasDepthAspect(VkFormat format) {
  switch (format) {
  case VK_FORMAT_D16_UNORM:
  case VK_FORMAT_X8_D24_UNORM_PACK32:
  case VK_FORMAT_D32_SFLOAT:
  case VK_FORMAT_D16_UNORM_S8_UINT:
  case VK_FORMAT_D24_UNORM_S8_UINT:
  case VK_FORMAT_D32_SFLOAT_S8_UINT:
    return true;
  default:
    return false;
  }
}

bool formatHasStencilAspect(VkFormat format) {
  switch (format) {
  case VK_FORMAT_S8_UINT:
  case VK_FORMAT_D16_UNORM_S8_UINT:
  case VK_FORMAT_D24_UNORM_S8_UINT:
  case VK_FORMAT_D32_SFLOAT_S8_UINT:
    return true;
  default:
    return false;
  }
}

bool formatHasColorAspect(VkFormat format) {
  return !formatHasDepthAspect(format) && !formatHasStencilAspect(format);
}

u32 maxMipLevelsForExtent(u32 width, u32 height) {
  if (width == 0 || height == 0) {
    return 0;
  }
  u32 levels = 1;
  u32 extent = std::max(width, height);
  while (extent > 1) {
    extent /= 2;
    ++levels;
  }
  return levels;
}

void validateImageShape(u32 width, u32 height, u32 mipLevels,
                        u32 arrayLayers) {
  if (width == 0 || height == 0) {
    throw std::runtime_error("Texture dimensions must be non-zero");
  }
  if (mipLevels == 0 || arrayLayers == 0) {
    throw std::runtime_error(
        "Texture mip levels and array layers must be non-zero");
  }
  if (mipLevels > maxMipLevelsForExtent(width, height)) {
    throw std::runtime_error("Texture mip level count exceeds texture extent");
  }
}

void validateImageAspect(VkFormat format, VkImageAspectFlags aspectMask) {
  constexpr VkImageAspectFlags kSupportedAspects =
      VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT |
      VK_IMAGE_ASPECT_STENCIL_BIT;
  if (aspectMask == 0) {
    throw std::runtime_error("Image aspect mask must not be empty");
  }
  if ((aspectMask & ~kSupportedAspects) != 0) {
    throw std::runtime_error("Image aspect mask contains unsupported bits");
  }
  if ((aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) != 0 &&
      !formatHasColorAspect(format)) {
    throw std::runtime_error("Image aspect color is incompatible with format");
  }
  if ((aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) != 0 &&
      !formatHasDepthAspect(format)) {
    throw std::runtime_error("Image aspect depth is incompatible with format");
  }
  if ((aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) != 0 &&
      !formatHasStencilAspect(format)) {
    throw std::runtime_error("Image aspect stencil is incompatible with format");
  }
}

void validateAttachmentUsage(VkFormat format, VkImageUsageFlags usage) {
  if ((usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0 &&
      formatHasColorAspect(format)) {
    throw std::runtime_error(
        "Attachment usage depth-stencil is incompatible with color format");
  }
  if ((usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0 &&
      !formatHasColorAspect(format)) {
    throw std::runtime_error(
        "Attachment usage color is incompatible with depth/stencil format");
  }
}

class VulkanTextureCleanupGuard {
public:
  VulkanTextureCleanupGuard(VkDevice device, VkImage &image,
                            VkDeviceMemory &memory, VkImageView &imageView,
                            VkSampler &sampler)
      : m_device(device), m_image(image), m_memory(memory),
        m_imageView(imageView), m_sampler(sampler) {}

  ~VulkanTextureCleanupGuard() {
    if (!m_active || m_device == VK_NULL_HANDLE) {
      return;
    }
    if (m_sampler != VK_NULL_HANDLE) {
      vkDestroySampler(m_device, m_sampler, nullptr);
      m_sampler = VK_NULL_HANDLE;
    }
    if (m_imageView != VK_NULL_HANDLE) {
      vkDestroyImageView(m_device, m_imageView, nullptr);
      m_imageView = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE) {
      vkDestroyImage(m_device, m_image, nullptr);
      m_image = VK_NULL_HANDLE;
    }
    if (m_memory != VK_NULL_HANDLE) {
      vkFreeMemory(m_device, m_memory, nullptr);
      m_memory = VK_NULL_HANDLE;
    }
  }

  void release() { m_active = false; }

private:
  VkDevice m_device = VK_NULL_HANDLE;
  VkImage &m_image;
  VkDeviceMemory &m_memory;
  VkImageView &m_imageView;
  VkSampler &m_sampler;
  bool m_active = true;
};
} // namespace

VulkanTexture::VulkanTexture(Token, VulkanDevice &device,
                             u32 width, u32 height,
                             VkFormat format,
                             VkImageUsageFlags usage, VkFilter filter)
    : m_device(device.getLogicalDevice()), m_width(width), m_height(height),
      m_format(format), m_usage(usage) {
  validateImageShape(width, height, 1, 1);
  validateImageAspect(format, VK_IMAGE_ASPECT_COLOR_BIT);
  VulkanTextureCleanupGuard cleanup(m_device, m_image, m_memory, m_imageView,
                                    m_sampler);

  // Create image
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(m_device, &imageInfo, nullptr, &m_image) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create image!");
  }

  // Allocate memory
  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(m_device, m_image, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = device.findMemoryTypeIndex(
      memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_memory) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate image memory!");
  }

  vkBindImageMemory(m_device, m_image, m_memory, 0);

  // Create image view and sampler
  createImageView(VK_IMAGE_ASPECT_COLOR_BIT);
  createSampler(filter, VK_SAMPLER_ADDRESS_MODE_REPEAT);
  cleanup.release();
}

VulkanTexture::VulkanTexture(Token, VulkanDevice &device, u32 width, u32 height,
                             VkFormat format, VkImageUsageFlags usage,
                             VkImageAspectFlags aspectMask, u32 mipLevels,
                             u32 arrayLayers, VkImageViewType viewType,
                             VkImageCreateFlags flags, VkFilter filter,
                             VkSamplerAddressMode addressMode)
    : m_device(device.getLogicalDevice()), m_width(width), m_height(height),
      m_mipLevels(mipLevels), m_arrayLayers(arrayLayers),
      m_viewType(viewType), m_format(format), m_usage(usage) {
  validateImageShape(width, height, mipLevels, arrayLayers);
  validateImageAspect(format, aspectMask);
  VulkanTextureCleanupGuard cleanup(m_device, m_image, m_memory, m_imageView,
                                    m_sampler);

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.flags = flags;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = mipLevels;
  imageInfo.arrayLayers = arrayLayers;
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(m_device, &imageInfo, nullptr, &m_image) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(m_device, m_image, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = device.findMemoryTypeIndex(
      memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_memory) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate image memory!");
  }

  vkBindImageMemory(m_device, m_image, m_memory, 0);

  createImageView(aspectMask);
  if ((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0) {
    createSampler(filter, addressMode);
  }
  cleanup.release();
}

VulkanTexture::VulkanTexture(Token, VulkanDevice &device,
                             u32 width, u32 height,
                             VkFormat format,
                             VkImageUsageFlags usage,
                             VkImageAspectFlags aspectMask)
    : m_device(device.getLogicalDevice()), m_width(width), m_height(height),
      m_format(format), m_usage(usage) {
  validateImageShape(width, height, 1, 1);
  validateImageAspect(format, aspectMask);
  VulkanTextureCleanupGuard cleanup(m_device, m_image, m_memory, m_imageView,
                                    m_sampler);

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(m_device, &imageInfo, nullptr, &m_image) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(m_device, m_image, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex = device.findMemoryTypeIndex(
      memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(m_device, &allocInfo, nullptr, &m_memory) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate image memory!");
  }

  vkBindImageMemory(m_device, m_image, m_memory, 0);

  createImageView(aspectMask);
  if ((usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0) {
    createSampler(VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
  }
  cleanup.release();
}

VulkanTexture::~VulkanTexture() {
  if (m_device != VK_NULL_HANDLE) {
    if (m_sampler != VK_NULL_HANDLE) {
      vkDestroySampler(m_device, m_sampler, nullptr);
      m_sampler = VK_NULL_HANDLE;
    }
    if (m_imageView != VK_NULL_HANDLE) {
      vkDestroyImageView(m_device, m_imageView, nullptr);
      m_imageView = VK_NULL_HANDLE;
    }
    if (m_image != VK_NULL_HANDLE) {
      vkDestroyImage(m_device, m_image, nullptr);
      m_image = VK_NULL_HANDLE;
    }
    if (m_memory != VK_NULL_HANDLE) {
      vkFreeMemory(m_device, m_memory, nullptr);
      m_memory = VK_NULL_HANDLE;
    }
  }
}

void VulkanTexture::createImageView(VkImageAspectFlags aspectMask) {
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = m_image;
  viewInfo.viewType = m_viewType;
  viewInfo.format = m_format;
  viewInfo.subresourceRange.aspectMask = aspectMask;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = m_mipLevels;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = m_arrayLayers;

  if (vkCreateImageView(m_device, &viewInfo, nullptr, &m_imageView) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create image view!");
  }
}

void VulkanTexture::createSampler(VkFilter filter,
                                  VkSamplerAddressMode addressMode) {
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = filter;
  samplerInfo.minFilter = filter;
  samplerInfo.addressModeU = addressMode;
  samplerInfo.addressModeV = addressMode;
  samplerInfo.addressModeW = addressMode;
  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = static_cast<float>(m_mipLevels);

  if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_sampler) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create texture sampler!");
  }
}

void VulkanTexture::transitionLayout(VulkanCommandBuffer &cmd,
                                     VkImageLayout oldLayout,
                                     VkImageLayout newLayout,
                                     VkPipelineStageFlags pipelineStage,
                                     VkImageAspectFlags aspectMask) {
  if (oldLayout != m_currentLayout) {
    throw std::runtime_error(
        "Texture old layout does not match current layout");
  }

  VkAccessFlags srcAccessMask = 0;
  VkAccessFlags dstAccessMask = 0;
  VkPipelineStageFlags sourceStage = 0;
  VkPipelineStageFlags destinationStage = 0;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    srcAccessMask = 0;
    dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    sourceStage = pipelineStage;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = pipelineStage;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    srcAccessMask = 0;
    dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    srcAccessMask = 0;
    dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sourceStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    destinationStage = pipelineStage;
  } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    destinationStage = pipelineStage;
  } else {
    throw std::runtime_error("Unsupported layout transition");
  }

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcAccessMask = srcAccessMask;
  barrier.dstAccessMask = dstAccessMask;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = m_image;
  barrier.subresourceRange.aspectMask = aspectMask;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = m_mipLevels;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = m_arrayLayers;

  vkCmdPipelineBarrier(cmd.getHandle(), sourceStage, destinationStage, 0, 0,
                       nullptr, 0, nullptr, 1, &barrier);

  m_currentLayout = newLayout;
}

void VulkanTexture::copyFromBuffer(VulkanCommandBuffer &cmd,
                                   VulkanBuffer &buffer) {
  std::vector<VkBufferImageCopy> regions;
  regions.reserve(m_mipLevels * m_arrayLayers);
  VkDeviceSize offset = 0;
  u32 mipWidth = m_width;
  u32 mipHeight = m_height;
  const auto bytesPerPixel = [&]() -> VkDeviceSize {
    switch (m_format) {
    case VK_FORMAT_R8_UNORM:
      return 1;
    case VK_FORMAT_R8G8B8_UNORM:
      return 3;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_UNORM:
      return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
      return 8;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
      return 16;
    default:
      throw std::runtime_error("Unsupported texture copy format");
    }
  }();

  for (u32 mip = 0; mip < m_mipLevels; ++mip) {
    const u32 levelWidth = std::max(mipWidth, 1u);
    const u32 levelHeight = std::max(mipHeight, 1u);
    const VkDeviceSize layerBytes =
        static_cast<VkDeviceSize>(levelWidth) *
        static_cast<VkDeviceSize>(levelHeight) * bytesPerPixel;

    for (u32 layer = 0; layer < m_arrayLayers; ++layer) {
      VkBufferImageCopy region{};
      region.bufferOffset = offset;
      region.bufferRowLength = 0;
      region.bufferImageHeight = 0;
      region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      region.imageSubresource.mipLevel = mip;
      region.imageSubresource.baseArrayLayer = layer;
      region.imageSubresource.layerCount = 1;
      region.imageOffset = {0, 0, 0};
      region.imageExtent.width = levelWidth;
      region.imageExtent.height = levelHeight;
      region.imageExtent.depth = 1;
      regions.push_back(region);
      offset += layerBytes;
    }

    mipWidth = std::max(mipWidth / 2u, 1u);
    mipHeight = std::max(mipHeight / 2u, 1u);
  }

  vkCmdCopyBufferToImage(cmd.getHandle(), buffer.getHandle(), m_image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         static_cast<u32>(regions.size()), regions.data());
}

VulkanTextureUniquePtr VulkanTexture::createForAttachment(
    VulkanDevice &device, u32 width, u32 height,
    VkFormat format,
    VkImageUsageFlags usage, VkImageAspectFlags aspectMask) {
  validateAttachmentUsage(format, usage);
  return std::make_unique<VulkanTexture>(Token{}, device, width, height, format,
                                         usage, aspectMask);
}

VulkanTextureUniquePtr VulkanTexture::create2D(
    VulkanDevice &device, u32 width, u32 height, VkFormat format,
    VkImageUsageFlags usage, u32 mipLevels, VkFilter filter) {
  return VulkanTextureUniquePtr(new VulkanTexture(
      Token{}, device, width, height, format,
      usage | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels,
      1, VK_IMAGE_VIEW_TYPE_2D, 0, filter, VK_SAMPLER_ADDRESS_MODE_REPEAT));
}

VulkanTextureUniquePtr VulkanTexture::createCube(
    VulkanDevice &device, u32 width, u32 height, VkFormat format,
    VkImageUsageFlags usage, u32 mipLevels, VkFilter filter) {
  validateImageShape(width, height, mipLevels, 6);
  if (width != height) {
    throw std::runtime_error("Cubemap texture must be square");
  }
  return VulkanTextureUniquePtr(new VulkanTexture(
      Token{}, device, width, height, format,
      usage | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels,
      6, VK_IMAGE_VIEW_TYPE_CUBE, VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT, filter,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE));
}

} // namespace backend
} // namespace LX_core
