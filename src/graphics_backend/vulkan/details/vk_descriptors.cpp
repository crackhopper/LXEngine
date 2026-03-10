#include "vk_descriptors.hpp"
#include <array>
#include <stdexcept>

namespace LX_core::graphic_backend {

// =============================================================================
// VulkanDescriptorAllocator 实现
// =============================================================================

VulkanDescriptorAllocatorPtr
VulkanDescriptorAllocator::create(VulkanDevice &device) {
  return std::make_unique<VulkanDescriptorAllocator>(Token{}, device);
}

void VulkanDescriptorAllocator::createPool() {
  // 风险点 1: 硬编码的池大小。
  // 如果渲染对象极多（如成千上万个材质实例），vkAllocateDescriptorSets 会失败。
  // 改进建议：实现一个 Pool 链，当当前池满时自动创建新池。
  std::array<VkDescriptorPoolSize, 3> poolSizes{};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[0].descriptorCount = uniformCount;
  poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[1].descriptorCount = samplerCount;
  poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  poolSizes[2].descriptorCount = storageCount;

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();
  poolInfo.maxSets = maxSetCount;
  // 注意：这里没有设置 VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
  // 因为我们目前采用的是手动 freeSets 缓存复用逻辑，而不是真正归还给 Vulkan
  // 池。

  if (vkCreateDescriptorPool(device.getHandle(), &poolInfo, nullptr, &hPool) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor pool!");
  }
}

// =============================================================================
// CameraResourceBinding 实现
// =============================================================================

CameraResourceBinding::CameraResourceBinding(Base::Token t,
                                             VulkanDevice &device,
                                             const LX_core::Camera &camera)
    : ResourceBindingBase<CameraResourceBinding, LX_core::Camera>(
          t, device, device.getDescriptorAllocator()) {}

ResourceBindingBase<CameraResourceBinding, LX_core::Camera>::LayoutCreateInfo
CameraResourceBinding::getLayoutCreateInfo() {
  LayoutCreateInfo info;
  info.name = "CameraLayout";

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
  // 风险点 2: Buffer 状态管理。
  // 这里假设 Camera 对象已经拥有一个准备好的 VkBuffer。
  // 在实际渲染循环中，如果 Buffer
  // 内容每帧改变，需要考虑双缓冲/多缓冲防止读取冲突。

  VkDescriptorBufferInfo bufferInfo{};
  // 假设 Camera 类有 getBufferHandle() 接口
  // bufferInfo.buffer = data.getBufferHandle();
  bufferInfo.offset = 0;
  bufferInfo.range = sizeof(float) * 16 * 2; // 示例：VP 矩阵

  VkWriteDescriptorSet descriptorWrite{};
  descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrite.dstSet = descriptorSet;
  descriptorWrite.dstBinding = 0;
  descriptorWrite.dstArrayElement = 0;
  descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorWrite.descriptorCount = 1;
  descriptorWrite.pBufferInfo = &bufferInfo;

  vkUpdateDescriptorSets(device.getHandle(), 1, &descriptorWrite, 0, nullptr);
}

// =============================================================================
// MaterialResourceBinding 实现
// =============================================================================

MaterialResourceBinding::MaterialResourceBinding(
    Base::Token t, VulkanDevice &device, const LX_core::MaterialBase &material)
    : ResourceBindingBase<MaterialResourceBinding, LX_core::MaterialBase>(
          t, device, device.getDescriptorAllocator()) {}

ResourceBindingBase<MaterialResourceBinding,
                    LX_core::MaterialBase>::LayoutCreateInfo
MaterialResourceBinding::getLayoutCreateInfo() {
  LayoutCreateInfo info;
  info.name = "BasicMaterialLayout";

  // 示例：一个 Uniform Buffer (颜色参数) + 一个 Combined Image Sampler (贴图)
  VkDescriptorSetLayoutBinding uboBinding{};
  uboBinding.binding = 0;
  uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uboBinding.descriptorCount = 1;
  uboBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  VkDescriptorSetLayoutBinding samplerBinding{};
  samplerBinding.binding = 1;
  samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  samplerBinding.descriptorCount = 1;
  samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

  info.bindings = {uboBinding, samplerBinding};
  info.layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  info.layoutInfo.bindingCount = static_cast<uint32_t>(info.bindings.size());
  info.layoutInfo.pBindings = info.bindings.data();

  return info;
}

void MaterialResourceBinding::update(VulkanDevice &device,
                                     const LX_core::MaterialBase &data) {
  // 假设 MaterialBase 有两个成员：
  // 1. uniformBufferHandle() 返回 VkBuffer
  // 2. textureImageView() 返回 VkImageView，textureSampler() 返回 VkSampler

  VkDescriptorBufferInfo bufferInfo{};
  bufferInfo.buffer = data.uniformBufferHandle(); // Uniform Buffer
  bufferInfo.offset = 0;
  bufferInfo.range = data.uniformBufferSize(); // 例如 sizeof(MaterialUniforms)

  VkDescriptorImageInfo imageInfo{};
  imageInfo.imageView = data.textureImageView();
  imageInfo.sampler = data.textureSampler();
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  // 创建写入结构体数组
  std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

  // Uniform Buffer 写入
  descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[0].dstSet = descriptorSet;
  descriptorWrites[0].dstBinding = 0; // 对应 getLayoutCreateInfo 绑定 0
  descriptorWrites[0].dstArrayElement = 0;
  descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorWrites[0].descriptorCount = 1;
  descriptorWrites[0].pBufferInfo = &bufferInfo;

  // Combined Image Sampler 写入
  descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[1].dstSet = descriptorSet;
  descriptorWrites[1].dstBinding = 1; // 对应 getLayoutCreateInfo 绑定 1
  descriptorWrites[1].dstArrayElement = 0;
  descriptorWrites[1].descriptorType =
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorWrites[1].descriptorCount = 1;
  descriptorWrites[1].pImageInfo = &imageInfo;

  // 更新 Vulkan 描述符集
  vkUpdateDescriptorSets(device.getHandle(),
                         static_cast<uint32_t>(descriptorWrites.size()),
                         descriptorWrites.data(), 0, nullptr);
}

// =============================================================================
// SkeletonResourceBinding 实现
// =============================================================================

SkeletonResourceBinding::SkeletonResourceBinding(
    Base::Token t, VulkanDevice &device, const LX_core::Skeleton &skeleton)
    : ResourceBindingBase<SkeletonResourceBinding, LX_core::Skeleton>(
          t, device, device.getDescriptorAllocator()) {}

ResourceBindingBase<SkeletonResourceBinding,
                    LX_core::Skeleton>::LayoutCreateInfo
SkeletonResourceBinding::getLayoutCreateInfo() {
  LayoutCreateInfo info;
  info.name = "SkeletonLayout";

  VkDescriptorSetLayoutBinding storageBinding{};
  storageBinding.binding = 0;
  storageBinding.descriptorType =
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; // 骨骼矩阵通常较多，建议用 Storage
  storageBinding.descriptorCount = 1;
  storageBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  info.bindings = {storageBinding};
  info.layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  info.layoutInfo.bindingCount = static_cast<uint32_t>(info.bindings.size());
  info.layoutInfo.pBindings = info.bindings.data();

  return info;
}

void SkeletonResourceBinding::update(VulkanDevice &device,
                                     const LX_core::Skeleton &data) {
  // 实现略。
}

} // namespace LX_core::graphic_backend