#include "vk_material.hpp"

namespace LX_core::graphic_backend {

VulkanMaterial::VulkanMaterial(
    VulkanDeviceWeakPtr device,
    std::shared_ptr<VulkanDescriptorAllocator> allocator)
    : m_device(device), m_allocator(allocator) {}

VulkanMaterial::~VulkanMaterial() {
  if (m_allocator && m_descriptorSet != VK_NULL_HANDLE) {
    m_allocator->free(m_descriptorSet);
    m_descriptorSet = VK_NULL_HANDLE;
  }
}

void VulkanMaterial::setPipeline(VulkanPipelinePtr pipeline) {
  m_pipeline = pipeline;
}

void VulkanMaterial::setDescriptorSetLayout(VkDescriptorSetLayout layout) {
  m_descriptorLayout = layout;
}

void VulkanMaterial::bindTexture(uint32_t binding, VulkanTexturePtr texture) {
  TextureBinding tb;
  tb.binding = binding;
  tb.texture = texture;
  m_textures.push_back(tb);
}

void VulkanMaterial::bindUniformBuffer(uint32_t binding,
                                       VulkanUniformBufferPtr ubo) {
  UniformBinding ub;
  ub.binding = binding;
  ub.ubo = ubo;
  m_uniforms.push_back(ub);
}

void VulkanMaterial::buildDescriptorSet() {
  if (m_descriptorSet != VK_NULL_HANDLE)
    return;

  auto dev = m_device.lock();
  if (!dev)
    return;

  VkDevice device = dev->getHandle();

  m_descriptorSet = m_allocator->allocate(&m_descriptorLayout, 1);

  std::vector<VkWriteDescriptorSet> writes;
  std::vector<VkDescriptorImageInfo> imageInfos;

  for (auto &t : m_textures) {

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = t.texture->imageView->view;
    imageInfo.sampler = t.texture->sampler->sampler;

    imageInfos.push_back(imageInfo);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptorSet;
    write.dstBinding = t.binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfos.back();

    writes.push_back(write);
  }

  std::vector<VkDescriptorBufferInfo> bufferInfos;
  for (auto &b : m_uniforms) {
    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = b.ubo->getBuffer();
    bufInfo.offset = 0;
    bufInfo.range = b.ubo->getSize();
    bufferInfos.push_back(bufInfo);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_descriptorSet;
    write.dstBinding = b.binding;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufferInfos.back();

    writes.push_back(write);
  }

  vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()),
                         writes.data(), 0, nullptr);
}

} // namespace LX_core::graphic_backend