#include "descriptor_manager.hpp"
#include "../device.hpp"
#include "core/utils/hash.hpp"
#include <array>
#include <stdexcept>
#include <string>

namespace LX_core {
namespace backend {

// Defined in pipeline.cpp — reused here so descriptor layout creation
// mirrors the mapping pipeline layout uses.
VkDescriptorType toVkDescriptorType(LX_core::ShaderPropertyType t);
VkShaderStageFlags toVkShaderStageFlags(LX_core::ShaderStage mask);

// Equality / hash intentionally ignore the `name` field so layouts with
// compatible shape but different member names share a single
// VkDescriptorSetLayout.
bool DescriptorLayoutKey::operator==(const DescriptorLayoutKey &other) const {
  if (bindings.size() != other.bindings.size())
    return false;
  for (size_t i = 0; i < bindings.size(); ++i) {
    const auto &a = bindings[i];
    const auto &b = other.bindings[i];
    if (a.set != b.set || a.binding != b.binding || a.type != b.type ||
        a.stageFlags != b.stageFlags || a.descriptorCount != b.descriptorCount)
      return false;
  }
  return true;
}

size_t
DescriptorLayoutHasher::operator()(const DescriptorLayoutKey &key) const {
  size_t res = 0;
  for (const auto &b : key.bindings) {
    size_t bindingHash = 0;
    LX_core::hash_combine(bindingHash, b.set);
    LX_core::hash_combine(bindingHash, b.binding);
    LX_core::hash_combine(bindingHash, static_cast<u32>(b.type));
    LX_core::hash_combine(bindingHash, static_cast<u32>(b.stageFlags));
    LX_core::hash_combine(bindingHash, b.descriptorCount);
    LX_core::hash_combine(res, bindingHash);
  }
  return res;
}

// --- 析构函数 ---
DescriptorSet::DescriptorSet(VkDescriptorSet set, VkDescriptorSetLayout layout,
                             VulkanDescriptorManager &manager)
    : m_set(set), m_layout(layout), m_manager(manager) {}

DescriptorSet::~DescriptorSet() {
  // 只有当句柄有效时才归还，防止移动构造后的空句柄触发逻辑
  if (m_set != VK_NULL_HANDLE) {
    m_manager.returnSet(m_set, m_layout);
  }
}

// --- 更新 Buffer 资源 ---
void DescriptorSet::updateBuffer(DescriptorBindingIndex32 binding,
                                 VkDescriptorBufferInfo bufferInfo,
                                 VkDescriptorType type) {
  VkWriteDescriptorSet descriptorWrite{};
  descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrite.dstSet = m_set;
  descriptorWrite.dstBinding = binding;
  descriptorWrite.dstArrayElement = 0; // 假设不是数组，或者是从数组第0个开始
  descriptorWrite.descriptorType = type;
  descriptorWrite.descriptorCount = 1;
  descriptorWrite.pBufferInfo = &bufferInfo;

  // 立即更新描述符集
  vkUpdateDescriptorSets(m_manager.getDeviceHandle(), 1, &descriptorWrite, 0,
                         nullptr);
}

// --- 更新 Image/Sampler 资源 ---
void DescriptorSet::updateImage(DescriptorBindingIndex32 binding,
                                VkDescriptorImageInfo imageInfo,
                                VkDescriptorType type) {
  VkWriteDescriptorSet descriptorWrite{};
  descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrite.dstSet = m_set;
  descriptorWrite.dstBinding = binding;
  descriptorWrite.dstArrayElement = 0;
  descriptorWrite.descriptorType = type;
  descriptorWrite.descriptorCount = 1;
  descriptorWrite.pImageInfo = &imageInfo;

  // 立即更新描述符集
  vkUpdateDescriptorSets(m_manager.getDeviceHandle(), 1, &descriptorWrite, 0,
                         nullptr);
}

void DescriptorSet::updateBatch(
    const std::vector<DescriptorUpdateInfo> &updates) {
  if (updates.empty())
    return;

  std::vector<VkWriteDescriptorSet> writes;
  writes.reserve(updates.size());

  for (const auto &info : updates) {
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_set;
    write.dstBinding = info.binding;
    write.dstArrayElement = 0;
    write.descriptorType = info.type;
    write.descriptorCount = info.descriptorCount;

    // 根据指针是否为空自动挂载
    if (info.bufferInfo) {
      write.pBufferInfo = info.bufferInfo;
    } else if (info.imageInfo) {
      write.pImageInfo = info.imageInfo;
    }

    writes.push_back(write);
  }

  // 一次性提交给驱动，比多次调用单个更新效率更高
  vkUpdateDescriptorSets(m_manager.getDeviceHandle(),
                         static_cast<u32>(writes.size()), writes.data(), 0,
                         nullptr);
}

VulkanDescriptorManager::VulkanDescriptorManager(Token, VulkanDevice &device)
    : m_device(device), m_currentFrameIndex(0) {
  m_frameContexts.resize(m_maxFramesInFlight);

  // 为每一帧创建一个独立的描述符池
  for (FrameIndex32 i = 0; i < m_maxFramesInFlight; ++i) {
    std::array<VkDescriptorPoolSize, 3> poolSizes{};

    // 1. Uniform Buffers (UBO)
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = m_config.uniformCount;

    // 2. Combined Image Samplers (Textures)
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = m_config.samplerCount;

    // 3. Storage Buffers (SSBO)
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = m_config.storageCount;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags =
        0; // 我们不使用
           // VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT，靠重置池或逻辑复用
    poolInfo.maxSets = m_config.maxSets;
    poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(m_device.getLogicalDevice(), &poolInfo, nullptr,
                               &m_frameContexts[i].pool) != VK_SUCCESS) {
      throw std::runtime_error("failed to create descriptor pool for frame " +
                               std::to_string(i));
    }
  }
}

VulkanDescriptorManagerUniquePtr
VulkanDescriptorManager::create(VulkanDevice &device) {
  return std::make_unique<VulkanDescriptorManager>(Token{}, device);
}

VulkanDescriptorManager::~VulkanDescriptorManager() {
  // 1. 等待 GPU 空闲，确保没有任何 DescriptorSet 正在被读取
  vkDeviceWaitIdle(m_device.getLogicalDevice());

  // 2. 销毁全局缓存的 Layouts
  // Layout 是跨帧共享的，只需销毁一次
  for (auto &pair : m_layoutCache) {
    if (pair.second != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(m_device.getLogicalDevice(), pair.second,
                                   nullptr);
    }
  }
  m_layoutCache.clear();

  // 3. 销毁每一帧的资源
  for (FrameIndex32 i = 0; i < m_maxFramesInFlight; ++i) {
    if (m_frameContexts[i].pool != VK_NULL_HANDLE) {
      // 销毁池会自动释放所有关联的 VkDescriptorSet 句柄
      vkDestroyDescriptorPool(m_device.getLogicalDevice(),
                              m_frameContexts[i].pool, nullptr);
    }

    // 清理内存中的追踪容器
    m_frameContexts[i].freeSets.clear();
    m_frameContexts[i].pendingReturn.clear();
  }
}

VkDevice VulkanDescriptorManager::getDeviceHandle() const {
  return m_device.getLogicalDevice();
}

VkDescriptorSetLayout VulkanDescriptorManager::getOrCreateLayout(
    const std::vector<LX_core::ShaderResourceBinding> &bindingsIn) {
  DescriptorLayoutKey key{bindingsIn};

  auto it = m_layoutCache.find(key);
  if (it != m_layoutCache.end()) {
    return it->second;
  }

  std::vector<VkDescriptorSetLayoutBinding> vkBindings;
  vkBindings.reserve(bindingsIn.size());

  for (const auto &b : bindingsIn) {
    VkDescriptorSetLayoutBinding vb{};
    vb.binding = b.binding;
    vb.descriptorType = toVkDescriptorType(b.type);
    vb.descriptorCount = b.descriptorCount > 0 ? b.descriptorCount : 1;
    vb.stageFlags = toVkShaderStageFlags(b.stageFlags);
    vb.pImmutableSamplers = nullptr;
    vkBindings.push_back(vb);
  }

  VkDescriptorSetLayoutCreateInfo layoutInfo{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  layoutInfo.bindingCount = static_cast<u32>(vkBindings.size());
  layoutInfo.pBindings = vkBindings.data();

  VkDescriptorSetLayout layout;
  if (vkCreateDescriptorSetLayout(m_device.getLogicalDevice(), &layoutInfo,
                                  nullptr, &layout) != VK_SUCCESS) {
    throw std::runtime_error("Vulkan: Failed to create descriptor set layout!");
  }

  m_layoutCache[key] = layout;
  return layout;
}

DescriptorSetUniquePtr VulkanDescriptorManager::allocateSet(
    const std::vector<LX_core::ShaderResourceBinding> &bindingsIn) {
  VkDescriptorSetLayout layout = getOrCreateLayout(bindingsIn);

  // 2. 获取当前帧的上下文
  auto &context = m_frameContexts[m_currentFrameIndex];

  VkDescriptorSet setHandle = VK_NULL_HANDLE;

  // 3. 优先尝试从当前帧的 FreeList 中复用 (这些是 GPU 之前已经处理完的旧 Set)
  auto &freeList = context.freeSets[layout];
  if (!freeList.empty()) {
    setHandle = freeList.back();
    freeList.pop_back();
  } else {
    // 4. FreeList 为空，从当前帧的 Pool 中分配新的 Set
    VkDescriptorSetAllocateInfo allocInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = context.pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;

    VkResult result = vkAllocateDescriptorSets(m_device.getLogicalDevice(),
                                               &allocInfo, &setHandle);

    if (result != VK_SUCCESS) {
      // 这里可以扩展：如果池满了，动态创建新池
      throw std::runtime_error(
          "Vulkan: Failed to allocate descriptor set! Pool might be full.");
    }
  }

  // 5. 封装进 RAII 对象并返回
  // 注意：DescriptorSet 析构时会异步调用 returnSet，放入 pendingReturn
  return std::make_unique<DescriptorSet>(setHandle, layout, *this);
}

void VulkanDescriptorManager::beginFrame(FrameIndex32 currentFrameIndex) {
  // 1. 更新当前帧索引
  m_currentFrameIndex = currentFrameIndex;

  // 2. 获取当前帧的上下文
  auto &context = m_frameContexts[m_currentFrameIndex];

  // 3. 处理延迟回收 (Pending Return -> Free Sets)
  // 既然我们现在回到了这个 frameIndex，说明 GPU 已经至少跑完了一圈
  // 之前在这帧里被“逻辑销毁”的 sets 现在可以安全复用了
  for (const auto &[set, layout] : context.pendingReturn) {
    context.freeSets[layout].push_back(set);
  }

  // 4. 清空待处理队列，准备记录本帧即将产生的销毁任务
  context.pendingReturn.clear();
}

void VulkanDescriptorManager::returnSet(VkDescriptorSet set,
                                        VkDescriptorSetLayout layout) {
  if (set == VK_NULL_HANDLE)
    return;

  // 获取当前正在进行的帧上下文
  auto &context = m_frameContexts[m_currentFrameIndex];

  // 将其加入“待处理”名单
  // 它会静静地躺在这里，直到下一次 beginFrame 切换回这个 frameIndex
  context.pendingReturn.push_back({set, layout});
}

void VulkanDescriptorManager::reset() {
  // 必须确保 GPU 已经停下，否则重置池会导致正在执行的命令崩溃
  vkDeviceWaitIdle(m_device.getLogicalDevice());

  for (FrameIndex32 i = 0; i < m_maxFramesInFlight; ++i) {
    auto &context = m_frameContexts[i];

    // 1. 重置物理描述符池 (这会使该池分配的所有 VkDescriptorSet 失效)
    if (context.pool != VK_NULL_HANDLE) {
      vkResetDescriptorPool(m_device.getLogicalDevice(), context.pool, 0);
    }

    // 2. 清空所有的逻辑记录
    context.freeSets.clear();
    context.pendingReturn.clear();
  }

  // 注意：Layout 缓存一般不在这里清理，因为 Layout 是跟 Pipeline 走的，
  // 只要 Pipeline 还在，Layout 就得留着。
}

} // namespace backend
} // namespace LX_core
