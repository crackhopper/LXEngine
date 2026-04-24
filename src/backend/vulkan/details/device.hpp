#pragma once
#include "core/platform/window.hpp"
#include <iterator>
#include <memory>
#include <optional>
#include <vector>
#include <vulkan/vulkan.h>

namespace LX_core {
namespace backend {

// 前置声明
class VulkanDescriptorManager;
class VulkanCommandBufferManager;

/**
 * @brief Vulkan 逻辑设备包装类
 * 负责物理设备选择、逻辑设备创建、全局队列获取以及核心管理器的生命周期。
 */
class VulkanDevice {
  struct Token {};

public:
  // 强制通过工厂方法创建
  explicit VulkanDevice(Token);
  ~VulkanDevice();

  static std::unique_ptr<VulkanDevice> create() {
    return std::make_unique<VulkanDevice>(Token{});
  }

  // --- 生命周期 ---
  void initialize(WindowSharedPtr window, const char *appName,
                  uint32_t appVersion = VK_MAKE_VERSION(1, 0, 0),
                  const char *engineName = "LX",
                  uint32_t engineVersion = VK_MAKE_VERSION(1, 0, 0),
                  uint32_t apiVersion = VK_API_VERSION_1_3,
                  std::vector<const char *> validationLayers = {
                      "VK_LAYER_KHRONOS_validation"});
  void shutdown();

  // --- 句柄获取 (只读) ---
  VkDevice getLogicalDevice() const { return m_device; }
  VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
  VkInstance getInstance() const { return m_instance; }
  VkSurfaceKHR getSurface() const { return m_surface; }
  VkSurfaceFormatKHR getSurfaceFormat() const { return m_surfaceFormat; }
  VkFormat getDepthFormat() const { return m_depthFormat; }
  VkExtent2D getExtent() const { return m_extent; }

  // 队列一定存在，否则创建实例的时候就会扔出异常。
  VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
  VkQueue getPresentQueue() const { return m_presentQueue; }
  uint32_t getGraphicsQueueFamilyIndex() const {
    return m_queueIndices.graphicsFamily.value_or(0);
  }
  uint32_t getPresentQueueFamilyIndex() const {
    return m_queueIndices.presentFamily.value_or(0);
  }

  // --- 核心管理器访问 ---
  VulkanDescriptorManager &getDescriptorManager() {
    return *m_descriptorManager;
  }

  // --- 实用工具 ---
  uint32_t findMemoryTypeIndex(uint32_t typeFilter,
                               VkMemoryPropertyFlags properties) const;
  void waitIdle() const { vkDeviceWaitIdle(m_device); }

  /**
   * @brief 在给定的候选格式列表中，根据硬件支持情况寻找最合适的图像格式。
   * * 该函数用于解决不同 GPU
   * 硬件对图像格式支持不一的问题（尤其是深度/模板缓冲格式）。 它会遍历
   * candidates 列表，返回第一个满足 tiling 和 features 限制的格式。
   * * @param physicalDevice 物理设备句柄，用于查询硬件属性。
   * @param candidates 候选格式列表，应按优先级从高到低排列（例如：D32_SFLOAT
   * 优先于 D24_UNORM）。
   * @param tiling         图像布局方式。通常渲染目标使用
   * VK_IMAGE_TILING_OPTIMAL (性能最优)。
   * @param features 该格式必须支持的功能位（如：必须能作为深度附件使用）。
   * * @return VkFormat      返回选中的首选格式。
   * @throw std::runtime_error 如果没有任何候选格式满足硬件要求，则抛出异常。
   * * @note 常用于初始化深度缓冲 (Depth Buffer) 时确定硬件支持的深度格式。
   */
  VkFormat findSupportedFormat(const std::vector<VkFormat> &candidates,
                               VkImageTiling tiling,
                               VkFormatFeatureFlags features);

  VkImageAspectFlags getDepthAspectMask() const;
private:
  // 内部初始化流程
  void createInstance(const char *appName, uint32_t appVersion,
                      const char *engineName, uint32_t engineVersion,
                      uint32_t apiVersion);
  void createSurface();
  void pickPhysicalDevice();
  void findSurfaceDepthFormat();
  void createLogicalDevice();

  struct QueueFamilyIndices {
    // std::optional 表示这个索引可能存在，也可能不存在（初始为 null）
    std::optional<uint32_t> graphicsFamily; // 图形渲染队列
    std::optional<uint32_t> presentFamily;  // 屏幕显示队列（Surface）

    // 辅助函数：判断我们需要的队列是否都找齐了
    bool isComplete() const {
      return graphicsFamily.has_value() && presentFamily.has_value();
    }
  };
  QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
  bool isDeviceSuitable(VkPhysicalDevice device,
                        std::vector<const char *> extensionsRequired);
  bool
  checkDeviceExtensionSupport(VkPhysicalDevice device,
                              std::vector<const char *> extensionsRequired);

  // 管理器：由 Device 持有，因为它们的生命周期与 Device 一致
  std::unique_ptr<VulkanDescriptorManager> m_descriptorManager;

  // Vulkan 核心句柄
  VkInstance m_instance = VK_NULL_HANDLE;
  std::vector<const char *> m_instanceExtensions;
  std::vector<const char *> m_validationLayers;

  WindowSharedPtr m_window = nullptr;
  VkSurfaceKHR m_surface = VK_NULL_HANDLE;
  VkSurfaceFormatKHR m_surfaceFormat = {};
  VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
  VkExtent2D m_extent = {};

  VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
  VkDevice m_device = VK_NULL_HANDLE;
  std::vector<const char *> m_deviceExtensions;

  // 队列
  VkQueue m_graphicsQueue = VK_NULL_HANDLE;
  VkQueue m_presentQueue = VK_NULL_HANDLE;
  QueueFamilyIndices m_queueIndices;

  // 调试层
  VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
};

using VulkanDeviceUniquePtr = std::unique_ptr<VulkanDevice>;

} // namespace backend
} // namespace LX_core