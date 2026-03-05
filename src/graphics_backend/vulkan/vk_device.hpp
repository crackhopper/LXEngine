#pragma once

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace LX_core::graphic_backend {

class VulkanDevice {
public:
  VulkanDevice() = default;
  ~VulkanDevice();

  // 初始化 Vulkan Instance、Device、Queue、CommandPool
  void initialize();
  void shutdown();

  // 获取 Vulkan 核心对象
  VkDevice getDevice() const { return device; }
  VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
  VkQueue getGraphicsQueue() const { return graphicsQueue; }
  VkCommandPool getCommandPool() const { return commandPool; }

  uint32_t getGraphicsQueueFamilyIndex() const { return graphicsQueueFamilyIndex; }

  VkInstance getInstance() const { return instance; }
  VkDevice getHandle() const { return device; }
private:
  // 内部初始化函数
  void createInstance();
  void pickPhysicalDevice();
  void createLogicalDevice();
  void createCommandPool();

private:
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue graphicsQueue = VK_NULL_HANDLE;
  VkCommandPool commandPool = VK_NULL_HANDLE;

  // 可扩展：队列族索引
  uint32_t graphicsQueueFamilyIndex = 0;
};

using VulkanDevicePtr = std::shared_ptr<VulkanDevice>;
using VulkanDeviceWeakPtr = std::weak_ptr<VulkanDevice>;


} // namespace LX_core::graphic_backend