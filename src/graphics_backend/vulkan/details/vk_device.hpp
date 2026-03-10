#pragma once

#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace LX_core::graphic_backend {

// 前置声明，方便在 Pool 中直接提供创建 CommandBuffer 的快捷方法
class VulkanCommandBuffer;
class VulkanDevice;
class VulkanCommandPool;
class VulkanDescriptorAllocator;

// 全局唯一对象仅支持 unique_ptr。
using VulkanDevicePtr = std::unique_ptr<VulkanDevice>;
using VulkanCommandPoolPtr = std::unique_ptr<VulkanCommandPool>;
using VulkanCommandBufferPtr = std::unique_ptr<VulkanCommandBuffer>;
using VulkanDescriptorAllocatorPtr = std::unique_ptr<VulkanDescriptorAllocator>;


class VulkanCommandPool {
  struct Token {};

public:
  /**
   * @brief 构造函数
   * @param queueFamilyIndex 该池所属的队列族索引（例如 Graphics 队列）
   * @param flags 常用标志位：
   * VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT (允许单独重置录制中的
   * Buffer) VK_COMMAND_POOL_CREATE_TRANSIENT_BIT (用于寿命极短的 Buffer)
   */
  VulkanCommandPool(Token, VkDevice device, uint32_t queueFamilyIndex,
                    VkCommandPoolCreateFlags flags =
                        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
  virtual ~VulkanCommandPool(); // 启用 RTTI

  // 禁用拷贝
  VulkanCommandPool(const VulkanCommandPool &) = delete;
  VulkanCommandPool &operator=(const VulkanCommandPool &) = delete;

  // 核心操作
  void reset(VkCommandPoolResetFlags flags = 0);

  // 获取原始句柄
  VkCommandPool getHandle() const { return _handle; }

  static std::unique_ptr<VulkanCommandPool> create(VkDevice device,
                                                   uint32_t index) {
    // 类内部可以创建 Token
    return std::make_unique<VulkanCommandPool>(Token{}, device, index);
  }

protected:
  VkDevice device = VK_NULL_HANDLE;
  VkCommandPool _handle{VK_NULL_HANDLE};
};

class VulkanDevice : public std::enable_shared_from_this<VulkanDevice> {
  void shutdown();
  struct Token {};

public:
  VulkanDevice(Token) {}
  ~VulkanDevice();
  static VulkanDevicePtr create() {
    auto ptr = std::make_unique<VulkanDevice>(Token{});
    ptr->initialize();
    return ptr;
  }
  // 初始化 Vulkan Instance、Device、Queue、CommandPool
  void initialize();

  // 获取 Vulkan 核心对象
  VkDevice getDevice() const { return device; }
  VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
  VkQueue getGraphicsQueue() const { return graphicsQueue; }

  uint32_t getGraphicsQueueFamilyIndex() const {
    return graphicsQueueFamilyIndex;
  }

  VkInstance getInstance() const { return instance; }
  VkDevice getHandle() const { return device; }

  uint32_t findMemoryTypeIndex(uint32_t typeFilter,
                          VkMemoryPropertyFlags properties);

  //----------------------- 资源创建相关 -----------------------
  // 注意： VulkanCommandBuffer 会自动管理生命周期。
  VulkanCommandBufferPtr
  newCmdBuffer(VkCommandBufferUsageFlags usage,
               VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY);

  VulkanDescriptorAllocator &getDescriptorAllocator() {
    return *pDescriptorAllocator;
  }               

private:
  // 内部初始化函数
  void createInstance();
  void pickPhysicalDevice();
  void createLogicalDevice();
  void createCommandPool();

private:
  VulkanCommandPoolPtr mp_graphicsCmdPool;
  VulkanDescriptorAllocatorPtr pDescriptorAllocator;

  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue graphicsQueue = VK_NULL_HANDLE;

  // 可扩展：队列族索引
  uint32_t graphicsQueueFamilyIndex = 0;
};

} // namespace LX_core::graphic_backend