#include "vkb_skeleton.hpp"
#include "core/math/quat.hpp"

namespace LX_core::graphic_backend {

DescriptorSetLayoutCreateInfo
SkeletonResourceBinding::getLayoutCreateInfo() {
  DescriptorSetLayoutCreateInfo info;
  info.index = DSLI_Skeleton;

  VkDescriptorSetLayoutBinding boneBinding{};
  boneBinding.binding = 0;
  boneBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  boneBinding.descriptorCount = 1;
  boneBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  info.bindings = {boneBinding};

  info.layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  info.layoutInfo.bindingCount = static_cast<uint32_t>(info.bindings.size());
  info.layoutInfo.pBindings = info.bindings.data();

  return info;
}

void SkeletonResourceBinding::update(VulkanDevice &device,
                                     const LX_core::Skeleton &data) {
  const auto &bones = data.getBones();
  LX_core::Mat4f matrices[LX_core::MAX_BONE_COUNT];

  for (uint32_t i = 0; i < LX_core::MAX_BONE_COUNT; ++i)
    matrices[i] = LX_core::Mat4f::identity();

  for (size_t i = 0; i < bones.size(); ++i) {
    const auto &bone = bones[i];
    LX_core::Mat4f local = LX_core::Mat4f::translate(bone.position) *
                           bone.rotation.toMat4() *
                           LX_core::Mat4f::scale(bone.scale);

    if (bone.parentIndex >= 0 &&
        bone.parentIndex < static_cast<int>(i)) {
      matrices[i] = matrices[bone.parentIndex] * local;
    } else {
      matrices[i] = local;
    }
  }

  m_ubo->update(matrices,
                sizeof(LX_core::Mat4f) * LX_core::MAX_BONE_COUNT);

  VkDescriptorBufferInfo bufferInfo{};
  bufferInfo.buffer = m_ubo->getHandle();
  bufferInfo.offset = 0;
  bufferInfo.range = sizeof(LX_core::Mat4f) * LX_core::MAX_BONE_COUNT;

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
