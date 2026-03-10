#pragma once

#include <memory>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "vk_device.hpp"

namespace LX_core::graphic_backend {

using VulkanDeviceWeakPtr = std::weak_ptr<VulkanDevice>;

class VulkanShaderModule {
public:
  VulkanShaderModule(VulkanDeviceWeakPtr device);
  ~VulkanShaderModule();

  bool loadFromFile(const std::string &path);

  VkShaderModule module() const { return m_module; }

private:
  VulkanDeviceWeakPtr m_device;
  VkShaderModule m_module = VK_NULL_HANDLE;
};

class VulkanDescriptorSetLayout {
public:
  VulkanDescriptorSetLayout(VulkanDeviceWeakPtr device);
  ~VulkanDescriptorSetLayout();

  void addBinding(uint32_t binding, VkDescriptorType type,
                  VkShaderStageFlags stageFlags);

  bool build();

  VkDescriptorSetLayout layout() const { return m_layout; }

private:
  VulkanDeviceWeakPtr m_device;

  std::vector<VkDescriptorSetLayoutBinding> m_bindings;

  VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
};

class VulkanPipelineLayout {
public:
  VulkanPipelineLayout(VulkanDeviceWeakPtr device);
  ~VulkanPipelineLayout();

  void addDescriptorSetLayout(VkDescriptorSetLayout layout);

  bool build();

  VkPipelineLayout layout() const { return m_layout; }

private:
  VulkanDeviceWeakPtr m_device;

  std::vector<VkDescriptorSetLayout> m_descriptorLayouts;

  VkPipelineLayout m_layout = VK_NULL_HANDLE;
};

class VulkanGraphicsPipeline {
public:
  VulkanGraphicsPipeline(VulkanDeviceWeakPtr device);
  ~VulkanGraphicsPipeline();

  bool build(VkRenderPass renderPass, VkPipelineLayout layout,
             VkExtent2D extent, VulkanShaderModule *vert,
             VulkanShaderModule *frag);

  VkPipeline pipeline() const { return m_pipeline; }

private:
  VulkanDeviceWeakPtr m_device;

  VkPipeline m_pipeline = VK_NULL_HANDLE;
};

using VulkanShaderModulePtr = std::shared_ptr<VulkanShaderModule>;
using VulkanPipelineLayoutPtr = std::shared_ptr<VulkanPipelineLayout>;
using VulkanGraphicsPipelinePtr = std::shared_ptr<VulkanGraphicsPipeline>;
using VulkanDescriptorSetLayoutPtr = std::shared_ptr<VulkanDescriptorSetLayout>;

} // namespace LX_core::graphic_backend