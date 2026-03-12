#include "vkb_material.hpp"

namespace LX_core::graphic_backend {

DescriptorSetLayoutCreateInfo
MaterialResourceBinding::getLayoutCreateInfo() {
  DescriptorSetLayoutCreateInfo info;
  info.index = DSLI_Material;

  VkDescriptorSetLayoutBinding uboBinding{};
  uboBinding.binding = 0;
  uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboBinding.descriptorCount = 1;
  uboBinding.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

  info.bindings = {uboBinding};

  info.layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  info.layoutInfo.bindingCount = static_cast<uint32_t>(info.bindings.size());
  info.layoutInfo.pBindings = info.bindings.data();

  return info;
}

void MaterialResourceBinding::update(VulkanDevice &device,
                                     const LX_core::MaterialBase &data) {
  MaterialUBO ubo{};

  if (auto *normal = dynamic_cast<const LX_core::MaterialBlinnPhong *>(&data)) {
    ubo.baseColor[0] = normal->baseColor.x;
    ubo.baseColor[1] = normal->baseColor.y;
    ubo.baseColor[2] = normal->baseColor.z;
    ubo.baseColor[3] = 1.0f;
    ubo.specularColor[0] = normal->specularColor.x;
    ubo.specularColor[1] = normal->specularColor.y;
    ubo.specularColor[2] = normal->specularColor.z;
    ubo.specularColor[3] = normal->shininess;
    ubo.metallic = 0.0f;
    ubo.roughness = 1.0f;
  } else if (auto *pbr = dynamic_cast<const LX_core::MaterialPBR *>(&data)) {
    ubo.baseColor[0] = pbr->baseColor.x;
    ubo.baseColor[1] = pbr->baseColor.y;
    ubo.baseColor[2] = pbr->baseColor.z;
    ubo.baseColor[3] = 1.0f;
    ubo.specularColor[0] = 0.0f;
    ubo.specularColor[1] = 0.0f;
    ubo.specularColor[2] = 0.0f;
    ubo.specularColor[3] = 0.0f;
    ubo.metallic = pbr->metallic;
    ubo.roughness = pbr->roughness;
  }

  m_ubo->update(&ubo, sizeof(MaterialUBO));

  VkDescriptorBufferInfo bufferInfo{};
  bufferInfo.buffer = m_ubo->getHandle();
  bufferInfo.offset = 0;
  bufferInfo.range = sizeof(MaterialUBO);

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = descriptorSet;
  write.dstBinding = 0;
  write.dstArrayElement = 0;
  write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  write.descriptorCount = 1;
  write.pBufferInfo = &bufferInfo;

  vkUpdateDescriptorSets(m_device.getHandle(), 1, &write, 0, nullptr);
}

} // namespace LX_core::graphic_backend
