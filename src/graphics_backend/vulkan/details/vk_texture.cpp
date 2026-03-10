#include "vk_texture.hpp"

namespace LX_core::graphic_backend {

void VulkanTexture::upload(VulkanDevice &device,
                           const LX_core::Texture &texData) {
  image = VulkanImage::create(device, 512, 512, VK_FORMAT_R8G8B8A8_SRGB,
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                  VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  imageView = VulkanImageView::create(device, *image);
  sampler = VulkanSampler::create(device);
}

} // namespace LX_core::graphic_backend
