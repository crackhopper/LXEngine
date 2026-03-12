#include "vk_material.hpp"

namespace LX_core::graphic_backend {

VulkanMaterial::VulkanMaterial(VulkanDevice &device)
    : m_device(device) {}

VulkanMaterial::~VulkanMaterial() = default;

void VulkanMaterial::setDescriptorSetLayout(VkDescriptorSetLayout layout) {
  m_descriptorLayout = layout;
}

void VulkanMaterial::bindTexture(uint32_t binding, VulkanTexture &texture) {
  m_textures.push_back({binding, &texture});
}

void VulkanMaterial::bindUniformBuffer(uint32_t binding,
                                       VulkanUniformBuffer &ubo) {
  m_uniforms.push_back({binding, &ubo});
}

void VulkanMaterial::buildDescriptorSet() {
  // TODO: 迁移至 MaterialResourceBinding 后，
  // descriptor set 的分配和写入由 ResourceBinding 内部管理。
}

} // namespace LX_core::graphic_backend
