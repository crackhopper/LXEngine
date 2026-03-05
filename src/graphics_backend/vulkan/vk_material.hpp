#pragma once

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

#include "vk_device.hpp"
#include "vk_resources.hpp"
#include "vk_descriptor_allocator.hpp"

namespace LX_core::graphic_backend {

class VulkanPipeline;

using VulkanPipelinePtr = std::shared_ptr<VulkanPipeline>;
using VulkanDeviceWeakPtr = std::weak_ptr<VulkanDevice>;

class VulkanMaterial {
public:
  VulkanMaterial(VulkanDeviceWeakPtr device,
                 std::shared_ptr<VulkanDescriptorAllocator> allocator);
  ~VulkanMaterial();

  void setPipeline(VulkanPipelinePtr pipeline);

  void setDescriptorSetLayout(VkDescriptorSetLayout layout);

  void bindTexture(uint32_t binding, VulkanTexturePtr texture);

  void bindUniformBuffer(uint32_t binding, VulkanUniformBufferPtr ubo);

  void buildDescriptorSet();

  VkDescriptorSet descriptorSet() const { return m_descriptorSet; }

  VulkanPipelinePtr pipeline() const { return m_pipeline; }

private:
  VulkanDeviceWeakPtr m_device;

  VulkanPipelinePtr m_pipeline;

  VkDescriptorSetLayout m_descriptorLayout = VK_NULL_HANDLE;

  std::shared_ptr<VulkanDescriptorAllocator> m_allocator;


  VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

  struct TextureBinding {
    uint32_t binding;
    VulkanTexturePtr texture;
    // VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  };

  struct UniformBinding {
    uint32_t binding;
    VulkanUniformBufferPtr ubo;
  };

  std::vector<TextureBinding> m_textures;
  std::vector<UniformBinding> m_uniforms;
};

using VulkanMaterialPtr = std::shared_ptr<VulkanMaterial>;

} // namespace LX_core::graphic_backend