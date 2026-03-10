#pragma once
#include "vk_device.hpp"
#include "details/vk_resources.hpp"
#include <memory>
namespace LX_core::graphic_backend {

class VulkanTexture {
  struct Token {};

public:
  VulkanTexture(Token) {}

  static std::unique_ptr<VulkanTexture>
  create(VulkanDevice &device, const LX_core::Texture &texData) {
    auto tex = std::make_unique<VulkanTexture>(Token{});
    tex->upload(device, texData);
    return tex;
  }

private:

  VulkanImagePtr image;
  VulkanImageViewPtr imageView;
  VulkanSamplerPtr sampler;

  void upload(VulkanDevice &device, const LX_core::Texture &texData);
};

} // namespace LX_core::graphic_backend