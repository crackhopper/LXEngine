#pragma once

// TODO: 迁移至 MaterialResourceBinding。
// VulkanMaterial 当前作为占位，待 MaterialBase 暴露 GPU 资源句柄后，
// 由 MaterialResourceBinding 统一管理 descriptor set。

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

#include "vk_device.hpp"
#include "details/vk_resources.hpp"
#include "details/vk_texture.hpp"

namespace LX_core::graphic_backend {

class VulkanMaterial;
using VulkanMaterialPtr = std::unique_ptr<VulkanMaterial>;

class VulkanMaterial {
public:
  explicit VulkanMaterial(VulkanDevice &device);
  ~VulkanMaterial();

  void setDescriptorSetLayout(VkDescriptorSetLayout layout);

  void bindTexture(uint32_t binding, VulkanTexture &texture);
  void bindUniformBuffer(uint32_t binding, VulkanUniformBuffer &ubo);

  void buildDescriptorSet();

  VkDescriptorSet descriptorSet() const { return m_descriptorSet; }

private:
  VulkanDevice &m_device;

  VkDescriptorSetLayout m_descriptorLayout = VK_NULL_HANDLE;
  VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

  struct TextureBinding {
    uint32_t binding;
    VulkanTexture *texture;
  };

  struct UniformBinding {
    uint32_t binding;
    VulkanUniformBuffer *ubo;
  };

  std::vector<TextureBinding> m_textures;
  std::vector<UniformBinding> m_uniforms;
};

} // namespace LX_core::graphic_backend
