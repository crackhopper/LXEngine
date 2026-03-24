#pragma once
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

namespace LX_core {
namespace graphic_backend {

// 前置声明
class VulkanDevice;
class DescriptorSet;
struct PipelineSlotDetails;
struct DescriptorLayoutKey;
class DescriptorLayoutHasher;

// Descriptor 更新信息 (moved before DescriptorSet to fix forward reference)
struct DescriptorUpdateInfo {
  uint32_t binding = 0;
  VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
  uint32_t descriptorCount = 1;
  const VkDescriptorBufferInfo *bufferInfo = nullptr;
  const VkDescriptorImageInfo *imageInfo = nullptr;
};

// DescriptorSet 的智能指针
using DescriptorSetPtr = std::unique_ptr<DescriptorSet>;

// 描述符集合
class DescriptorSet {
public:
  DescriptorSet(VkDescriptorSet set, VkDescriptorSetLayout layout,
                class VulkanDescriptorManager &manager);
  ~DescriptorSet();

  VkDescriptorSet getHandle() const { return m_set; }

  void updateBuffer(uint32_t binding, VkDescriptorBufferInfo bufferInfo,
                   VkDescriptorType type);
  void updateImage(uint32_t binding, VkDescriptorImageInfo imageInfo,
                   VkDescriptorType type);
  void updateBatch(const std::vector<DescriptorUpdateInfo> &updates);

private:
  VkDescriptorSet m_set = VK_NULL_HANDLE;
  VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
  class VulkanDescriptorManager &m_manager;
};

// Layout 唯一性键值
struct DescriptorLayoutKey {
  std::vector<PipelineSlotDetails> slots;
  bool operator==(const DescriptorLayoutKey &other) const;
};

class DescriptorLayoutHasher {
public:
  size_t operator()(const DescriptorLayoutKey &key) const;
};

// 描述符管理器
class VulkanDescriptorManager {
public:
  struct Token {};

  VulkanDescriptorManager(Token);
  ~VulkanDescriptorManager();

  void initialize(VulkanDevice &device);

  VkDescriptorSetLayout getOrCreateLayout(
      const std::vector<PipelineSlotDetails> &slots);
  DescriptorSetPtr allocateSet(const std::vector<PipelineSlotDetails> &slots);

  void beginFrame(uint32_t currentFrameIndex);
  void returnSet(VkDescriptorSet set, VkDescriptorSetLayout layout);
  void reset();

  VkDevice getDeviceHandle() const;

private:
  VulkanDevice *m_device = nullptr;
  uint32_t m_currentFrameIndex = 0;
  uint32_t m_maxFramesInFlight = 3;

  struct FrameContext {
    VkDescriptorPool pool = VK_NULL_HANDLE;
    std::unordered_map<VkDescriptorSetLayout, std::vector<VkDescriptorSet>>
        freeSets;
    std::vector<std::pair<VkDescriptorSet, VkDescriptorSetLayout>> pendingReturn;
  };

  std::vector<FrameContext> m_frameContexts;
  std::unordered_map<DescriptorLayoutKey, VkDescriptorSetLayout,
                     DescriptorLayoutHasher>
      m_layoutCache;

  struct Config {
    uint32_t uniformCount = 16;
    uint32_t samplerCount = 16;
    uint32_t storageCount = 8;
    uint32_t maxSets = 64;
  } m_config;
};

using VulkanDescriptorManagerPtr = std::unique_ptr<VulkanDescriptorManager>;

} // namespace graphic_backend
} // namespace LX_core