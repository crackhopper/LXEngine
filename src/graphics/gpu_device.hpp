#pragma once
#include "foundation/platform.hpp"
#include <optional>
#include <vector>
#include <vulkan/vulkan.h>

namespace LX {

// 选择物理设备时，用来保存特性和计算评分的结构
struct DeviceScore {
  VkPhysicalDevice device;
  VkPhysicalDeviceProperties properties;
  VkPhysicalDeviceDriverProperties driverProperties;
  i32 score = 0;
  bool suitable = false;
};

// 设备支持的相关队列族 的 结果（支持XXX功能队列族的索引）
struct QueryQueueFamilyResult {
  std::optional<uint32_t> graphicsAndComputeFamily; // 图形和计算队列族索引
  std::optional<uint32_t> graphicsFamily;           // 图形队列族索引
  std::optional<uint32_t> presentFamily;            // 呈现队列族索引

  bool isComplete() {
    return graphicsFamily.has_value() && presentFamily.has_value() &&
           graphicsAndComputeFamily;
  }
};

// (逻辑设备)交换链支持的详细信息
struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> presentModes;
};
} // namespace LX