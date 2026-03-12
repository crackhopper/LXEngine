#pragma once
#include "vk_device.hpp"
#include "vk_resources.hpp"
#include <memory>
namespace LX_core::graphic_backend {

class VulkanTexture;
using VulkanTexturePtr = std::unique_ptr<VulkanTexture>;

class VulkanTexture {
  struct Token {};

public:
  VulkanTexture(Token) {}

  static VulkanTexturePtr
  create(VulkanDevice &device, const LX_core::Texture &texData) {
    auto tex = std::make_unique<VulkanTexture>(Token{});
    tex->upload(device, texData);
    return tex;
  }

  VkImageView getImageView() const { return imageView ? imageView->getHandle() : VK_NULL_HANDLE; }
  VkSampler getSampler() const { return sampler ? sampler->getHandle() : VK_NULL_HANDLE; }

private:
  VulkanImagePtr image;
  VulkanImageViewPtr imageView;
  VulkanSamplerPtr sampler;

  void upload(VulkanDevice &device, const LX_core::Texture &texData);
};

} // namespace LX_core::graphic_backend
