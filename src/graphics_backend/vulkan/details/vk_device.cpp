#include "vk_device.hpp"
#include "details/vk_cmdbuffer.hpp"
#include <iostream>
#include <set>
#include <stdexcept>

namespace LX_core::graphic_backend {

VulkanCommandPool::VulkanCommandPool(Token, VkDevice _device,
                                     uint32_t queueFamilyIndex,
                                     VkCommandPoolCreateFlags flags)
    : device(_device) {

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.queueFamilyIndex = queueFamilyIndex;
  poolInfo.flags = flags;

  if (vkCreateCommandPool(device, &poolInfo, nullptr, &_handle) != VK_SUCCESS) {
    throw std::runtime_error("failed to create command pool!");
  }
}

VulkanCommandPool::~VulkanCommandPool() {
  if (_handle != VK_NULL_HANDLE) {
    vkDestroyCommandPool(device, _handle, nullptr);
    _handle = VK_NULL_HANDLE;
  }
}

void VulkanCommandPool::reset(VkCommandPoolResetFlags flags) {
  // 重置整个池，这会释放或重置该池分配的所有 CommandBuffer
  if (vkResetCommandPool(device, _handle, flags) != VK_SUCCESS) {
    throw std::runtime_error("failed to reset command pool!");
  }
}

VulkanDevice::~VulkanDevice() { shutdown(); }

void VulkanDevice::initialize() {
  // 1. 创建 Vulkan Instance
  createInstance();

  // 2. 选择物理设备
  pickPhysicalDevice();

  // 3. 创建逻辑设备和队列
  createLogicalDevice();

  // 4. 创建 Command Pool
  createCommandPool();
}

void VulkanDevice::createInstance() {
  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "LX Core Vulkan App";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "LX";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_3;

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;

  if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan instance!");
  }
}

void VulkanDevice::shutdown() {
  if (device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device); // 关键：确保 GPU 活干完了再拆迁
  }
  mp_graphicsCmdPool = nullptr;
  if (device != VK_NULL_HANDLE) {
    vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
  }

  if (instance != VK_NULL_HANDLE) {
    vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
  }
}

void VulkanDevice::pickPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
  if (deviceCount == 0) {
    throw std::runtime_error("Failed to find GPUs with Vulkan support!");
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

  // 简化：选择第一个支持图形队列的设备
  for (const auto &dev : devices) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(dev, &props);

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount,
                                             queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
      if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        physicalDevice = dev;
        graphicsQueueFamilyIndex = i;
        return;
      }
    }
  }

  throw std::runtime_error("Failed to find a suitable GPU!");
}

void VulkanDevice::createLogicalDevice() {
  VkDeviceQueueCreateInfo queueCreateInfo{};
  queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
  queueCreateInfo.queueCount = 1;
  float queuePriority = 1.0f;
  queueCreateInfo.pQueuePriorities = &queuePriority;

  VkPhysicalDeviceFeatures deviceFeatures{};

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.pQueueCreateInfos = &queueCreateInfo;
  createInfo.queueCreateInfoCount = 1;
  createInfo.pEnabledFeatures = &deviceFeatures;

  if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create logical device!");
  }

  vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
}

void VulkanDevice::createCommandPool() {
  mp_graphicsCmdPool =
      VulkanCommandPool::create(device, graphicsQueueFamilyIndex);
}

uint32_t VulkanDevice::findMemoryTypeIndex(uint32_t typeFilter,
                                      VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProps;
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

  for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
    if ((typeFilter & (1 << i)) &&
        (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("Failed to find suitable memory type");
}

VulkanCommandBufferPtr VulkanDevice::newCmdBuffer(
    VkCommandBufferUsageFlags usage,
    VkCommandBufferLevel level) {
  return VulkanCommandBuffer::create(device, mp_graphicsCmdPool->getHandle(),
                                     usage, level);
}

} // namespace LX_core::graphic_backend