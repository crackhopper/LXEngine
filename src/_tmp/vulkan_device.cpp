#include "./device.hpp"
#include "core/platform.hpp"
#include <vector>

#ifdef NDEBUG
  const bool enableValidationLayers = false;
#else
  const bool enableValidationLayers = true;
#endif

#ifdef USE_GEOMETRY_SHADER
  const bool useGeometryShader = true;
#else
  const bool useGeometryShader = false;
#endif

namespace LX_core {

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

class VulkanInstance {

private:
  VkInstance instance;
};


} // namespace LX_graphics