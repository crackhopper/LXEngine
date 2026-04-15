#pragma once
#include "core/asset/shader.hpp"
#include "../device.hpp"
#include <vulkan/vulkan.h>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

namespace LX_core {
namespace backend {

class DescriptorSet;
struct DescriptorLayoutKey;
class DescriptorLayoutHasher;

struct DescriptorUpdateInfo {
  uint32_t binding = 0;
  VkDescriptorType type = VK_DESCRIPTOR_TYPE_MAX_ENUM;
  uint32_t descriptorCount = 1;
  const VkDescriptorBufferInfo *bufferInfo = nullptr;
  const VkDescriptorImageInfo *imageInfo = nullptr;
};

using DescriptorSetPtr = std::unique_ptr<DescriptorSet>;

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

/// Layout cache key derived from a list of reflected shader bindings (all
/// belonging to the same descriptor set). Hashing deliberately ignores the
/// `name` field so layouts with compatible shape but different member names
/// share a single VkDescriptorSetLayout.
struct DescriptorLayoutKey {
  std::vector<LX_core::ShaderResourceBinding> bindings;
  bool operator==(const DescriptorLayoutKey &other) const;
};

class DescriptorLayoutHasher {
public:
  size_t operator()(const DescriptorLayoutKey &key) const;
};

class VulkanDescriptorManager;
using VulkanDescriptorManagerPtr = std::unique_ptr<VulkanDescriptorManager>;

class VulkanDescriptorManager {
public:
  struct Token {};

  VulkanDescriptorManager(Token, VulkanDevice &device);
  ~VulkanDescriptorManager();

  static VulkanDescriptorManagerPtr create(VulkanDevice &device);

  /// Build (or fetch from the cache) a VkDescriptorSetLayout for the given
  /// reflection bindings. Caller is responsible for ensuring all bindings
  /// belong to the same descriptor set index.
  VkDescriptorSetLayout getOrCreateLayout(
      const std::vector<LX_core::ShaderResourceBinding> &bindings);

  /// Allocate a descriptor set with the layout derived from the given bindings.
  DescriptorSetPtr
  allocateSet(const std::vector<LX_core::ShaderResourceBinding> &bindings);

  void beginFrame(uint32_t currentFrameIndex);
  void returnSet(VkDescriptorSet set, VkDescriptorSetLayout layout);
  void reset();

  VkDevice getDeviceHandle() const;

private:
  VulkanDevice &m_device;
  uint32_t m_currentFrameIndex = 0;
  uint32_t m_maxFramesInFlight = 3;

  struct FrameContext {
    VkDescriptorPool pool = VK_NULL_HANDLE;
    std::unordered_map<VkDescriptorSetLayout, std::vector<VkDescriptorSet>>
        freeSets;
    std::vector<std::pair<VkDescriptorSet, VkDescriptorSetLayout>>
        pendingReturn;
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

} // namespace backend
} // namespace LX_core
