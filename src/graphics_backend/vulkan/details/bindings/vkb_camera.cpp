#include "vkb_camera.hpp"

namespace LX_core::graphic_backend {


DescriptorSetLayoutCreateInfo
CameraResourceBinding::getLayoutCreateInfo() {
  DescriptorSetLayoutCreateInfo info;
  info.index = DSLI_Camera;

  VkDescriptorSetLayoutBinding cameraBinding{};
  cameraBinding.binding = 0;
  cameraBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  cameraBinding.descriptorCount = 1;
  cameraBinding.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

  info.bindings = {cameraBinding};

  info.layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  info.layoutInfo.bindingCount = static_cast<uint32_t>(info.bindings.size());
  info.layoutInfo.pBindings = info.bindings.data();

  return info;
}

void CameraResourceBinding::update(VulkanDevice &device,
                                   const LX_core::Camera &data) {
  LX_core::Camera::CameraUBO ubo = data.getUBO();
  m_ubo->update(&ubo, sizeof(LX_core::Camera::CameraUBO));

  VkDescriptorBufferInfo bufferInfo{};
  bufferInfo.buffer = m_ubo->getHandle();
  bufferInfo.offset = 0;
  bufferInfo.range = sizeof(LX_core::Camera::CameraUBO);

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
