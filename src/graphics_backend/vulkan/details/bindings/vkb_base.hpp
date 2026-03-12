#pragma once
#include "../vk_device.hpp"
#include <array>
#include <cassert>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>
/**
 *
 * 描述符：就是可以被shader访问的资源（其实是资源的访问器）。可以绑定的资源类型：
 * - Uniform Buffer （常用的 shader 的输入参数）
 * - Image / Texture (常用的 shader
 * 的输入参数；主要以图像的方式传入shader，但访问的时候，主要结合对应的坐标来采样访问)
 * - Sampler （是对 Texture 的采样器，用于在 shader 中对 Texture 进行采样访问）
 * - Storage Buffer/Storage
 * Image（更高权限的资源，不仅可以读写，容量也大，格式更加自由。可以立即为进阶的Texture）
 *
 *
 * 描述符集（Descriptor Set）：描述符集，可以理解为一组资源的绑定。这些资源会被
 * shader 程序使用。（描述符集显然包含了很多描述符；
 * 但实际这里的集，不仅仅是多个，而且是多种描述符；主要强调多种）
 * - 每个描述符集，可以有多种类别的描述符；每个类别可以有多个描述符实例。
 * - 描述符集，关于类别和类别个数的这些设定，由 **描述符集布局（Descriptor Set
 * Layout）** 来定义。因此一个描述符 集都有与之对应的描述符集布局。
 * -
 * 描述集+描述集布局，构成了一组资源绑定。通常这些资源绑定会和逻辑层的对象关联，比如：
 *   - 相机（描述符集会刻画：相机的位置、方向、FOV 等）
 *   - 材质（描述符集会刻画：材质的颜色、贴图、法线贴图等）
 *   - 模型骨骼（描述符集会刻画：模型的骨骼变换矩阵等）
 * - 描述符集，由专门的更新指令，用来更新描述符内部的数据。
 * - 描述符集布局 会参与 管线布局的定义。
 *
 * 管线：
 * - 关键可以理解是封装了shader程序执行的上下文，构成了GPU可以执行的"程序"。
 * - 管线布局描述 shader 可访问资源的布局（descriptor set + push
 * constant）。包含：
 *   - 描述符集布局
 *   - 对于特别小的临时数据，还有push constant可以用。（通常仅128 Byte -
 * 256Byte)
 * - 管线本身包含了 shader 二进制。
 * - 对于图形管线的额外输入：
 *   - 顶点输入信息VertexInput（世界中的顶点）。虽然本质可以用descriptor来绑定，
 *     不过这种单独的结构定义是为了兼容老接口，和性能考虑(vertex fetch硬件)。
 *   -
 * 输入装配信息InputAssembly（顶点如何装配成图元；顶点之间的拓扑关系的类型，比如：点、线、三角形等）
 *     配合顶点信息，方便drawcall调用。（实际draw的时候也会指定装配方式）
 * -
 * 其他管线相关的参数，比如：视角裁剪、光栅化、深度测试、混合等。（会控制管线执行的流程）
 * -
 * 另外，管线除了渲染管线，还支持计算管线（相当于管线的类别）。会有不同的管线阶段。
 *
 * 这里提一下 renderpass/subpass， framebuffer
 * 。这些其实是管线的上下文。（新的API可以动态渲染，不需要这些定义）。
 * 目的主要是刻画管线输出的目标。(需要和pipeline兼容)
 *
 *
 * 回归主题，描述符集的使用方式。具体的流程：
 * 1. CPU侧根据概念定义一些参数。
 * 2. GPU侧，定义这些参数的资源绑定（即，DescriptorSet）。需要和CPU侧对应上。
 * 3. 管线建立：需要根据资源绑定布局，对应的shader，创建好管线。
 * 4. 渲染循环中，根据需要，更新描述符集。
 *
 * 注意到这里面1-3都是逻辑上互相耦合的。而描述符布局变化、shader变化都导致管线建立的变化。因此，通常我们会建立几个
 * 比较通用的管线。（很多参数都预留出来，保存一些默认值）图形渲染常用的管线类型：
 * - opaque pipeline : 非透明物体，普通的渲染管线。
 * - transparent pipeline :
 * 透明物体，需要特殊处理的渲染管线。（会涉及到深度测试、混合等）
 * - shadow pipeline : 阴影渲染管线。（会涉及到阴影贴图的写入）
 * - skinning pipeline : 骨骼动画渲染管线。（会涉及到模型骨骼的变换矩阵等）
 *
 * 我们这个文件，目前v1.0版本，仅考虑常规的渲染下的资源绑定。
 *
 */
namespace LX_core::graphic_backend {

enum DescriptorSetLayoutIndex {
  DSLI_Camera = 0,
  DSLI_Light = 1,
  DSLI_Material = 2,
  DSLI_Skeleton = 3,

  DSLI_NUM_LAYOUT = 4,
};

struct DescriptorSetLayoutCreateInfo {
  DescriptorSetLayoutIndex index;
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  VkDescriptorSetLayoutCreateInfo layoutInfo;
  std::vector<VkDescriptorSetLayoutBinding> bindings;
  std::vector<VkDescriptorPoolSize> poolSizes;
};

// 描述符集分配器
// - 描述符集，需要从描述符池来分配。这里简单做法是，用一个足够大的描述符池。

template <typename Derived, typename Data> class ResourceBindingBase;

class VulkanDescriptorAllocator;
using VulkanDescriptorAllocatorPtr = std::unique_ptr<VulkanDescriptorAllocator>;
// 会保存在 VulkanDevice 中。通过 getDescriptorAllocator() 来获取。
class VulkanDescriptorAllocator {
  struct Token {};

public:
  VulkanDescriptorAllocator(Token, VulkanDevice &device) : device(device) {
    // 风险点 1: 硬编码的池大小。
    // 如果渲染对象极多（如成千上万个材质实例），vkAllocateDescriptorSets
    // 会失败。 改进建议：实现一个 Pool 链，当当前池满时自动创建新池。
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = uniformCount;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = samplerCount;
    poolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[2].descriptorCount = storageCount;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = maxSetCount;
    // 注意：这里没有设置 VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
    // 因为我们目前采用的是手动 freeSets 缓存复用逻辑，而不是真正归还给 Vulkan
    // 池。

    if (vkCreateDescriptorPool(device.getHandle(), &poolInfo, nullptr,
                               &hPool) != VK_SUCCESS) {
      throw std::runtime_error("failed to create descriptor pool!");
    }
  }
  ~VulkanDescriptorAllocator() {
    for (auto &[layout, sets] : usedSets) {
      assert(sets.empty());
    }
    for (auto &[name, info] : layoutInfo) {
      vkDestroyDescriptorSetLayout(device.getHandle(), info.layout, nullptr);
    }
  }

  template <typename T, typename Data>
  bool allocate(ResourceBindingBase<T, Data> &resourceBinding);
  template <typename T, typename Data>
  void free(ResourceBindingBase<T, Data> &resourceBinding);

  static VulkanDescriptorAllocatorPtr create(VulkanDevice &device) {
    return std::make_unique<VulkanDescriptorAllocator>(Token{}, device);
  }

private:
  VulkanDevice &device;
  VkDescriptorPool hPool = VK_NULL_HANDLE;

  std::unordered_map<DescriptorSetLayoutIndex, DescriptorSetLayoutCreateInfo>
      layoutInfo;
  // 已分配列表，方便复用
  std::unordered_map<VkDescriptorSetLayout, std::vector<VkDescriptorSet>>
      usedSets;
  std::unordered_map<VkDescriptorSetLayout, std::vector<VkDescriptorSet>>
      freeSets;

  uint32_t maxSetCount = 1000; // 默认最大 descriptor set 数量
  uint32_t samplerCount = 1000;
  uint32_t uniformCount = 1000;
  uint32_t storageCount = 1000;
};

template <typename T, typename Data>
bool VulkanDescriptorAllocator::allocate(
    ResourceBindingBase<T, Data> &resourceBinding) {
  assert(hPool != VK_NULL_HANDLE);
  auto layout = resourceBinding.getLayoutHandle();
  if (layout != VK_NULL_HANDLE && !freeSets[layout].empty()) {
    resourceBinding.descriptorSet = freeSets[layout].back();
    freeSets[layout].pop_back();
    usedSets[layout].push_back(resourceBinding.descriptorSet);
    return true;
  }
  if (layout == VK_NULL_HANDLE) {
    auto layoutCreateInfo = resourceBinding.getLayoutCreateInfo();
    if (layoutInfo.find(layoutCreateInfo.index) != layoutInfo.end()) {
      layout = layoutInfo[layoutCreateInfo.index].layout;
    } else {
      if (vkCreateDescriptorSetLayout(device.getHandle(),
                                      &layoutCreateInfo.layoutInfo, nullptr,
                                      &layout) != VK_SUCCESS) {
        return false;
      }
      layoutCreateInfo.layout = layout;
      layoutInfo[layoutCreateInfo.index] = layoutCreateInfo;
    }
    resourceBinding.layout = layout;
  }

  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = hPool;
  allocInfo.pSetLayouts = &layout;
  allocInfo.descriptorSetCount = 1;

  VkResult result = vkAllocateDescriptorSets(device.getHandle(), &allocInfo,
                                             &resourceBinding.descriptorSet);
  usedSets[layout].push_back(resourceBinding.descriptorSet);
  return result == VK_SUCCESS;
}

template <typename T, typename Data>
void VulkanDescriptorAllocator::free(
    ResourceBindingBase<T, Data> &resourceBinding) {
  auto layout = resourceBinding.getLayoutHandle();
  if (layout == VK_NULL_HANDLE) {
    return;
  }
  auto it = std::find(usedSets[layout].begin(), usedSets[layout].end(),
                      resourceBinding.descriptorSet);
  if (it != usedSets[layout].end()) {
    usedSets[layout].erase(it);
    freeSets[layout].push_back(resourceBinding.descriptorSet);
  }
}

// 描述符集绑定的基类。
template <typename Derived, typename Data> class ResourceBindingBase {
protected:
  struct Token {};

public:
  using Ptr = std::unique_ptr<Derived>;
  using Base = ResourceBindingBase<Derived, Data>;

  ResourceBindingBase(Token, VulkanDevice &device,
                      VulkanDescriptorAllocator &allocator);
  virtual ~ResourceBindingBase();

  static Ptr create(VulkanDevice &device, VulkanDescriptorAllocator &allocator,
                    const Data &data) {
    auto p = std::make_unique<Derived>(Token{}, device, allocator, data);
    p->init();
    return p;
  }
  virtual void init();

  virtual DescriptorSetLayoutCreateInfo getLayoutCreateInfo() = 0;

  // 更新描述符集。
  virtual void update(VulkanDevice &device, const Data &data) = 0;

  VkDescriptorSet getDescriptorSetHandle() const { return descriptorSet; }
  VkDescriptorSetLayout getLayoutHandle() const { return layout; }

protected:
  VkDevice hDevice;
  VulkanDescriptorAllocator &allocator;
  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  friend class VulkanDescriptorAllocator;
};

template <typename Derived, typename Data>
ResourceBindingBase<Derived, Data>::ResourceBindingBase(
    Token, VulkanDevice &device, VulkanDescriptorAllocator &allocator)
    : hDevice(device.getHandle()), allocator(allocator) {}
template <typename Derived, typename Data>
void ResourceBindingBase<Derived, Data>::init() {
  allocator.allocate(*this);
}

template <typename Derived, typename Data>
ResourceBindingBase<Derived, Data>::~ResourceBindingBase() {
  if (descriptorSet != VK_NULL_HANDLE) {
    allocator.free(*this);
    descriptorSet = VK_NULL_HANDLE;
    layout = VK_NULL_HANDLE;
  }
}

} // namespace LX_core::graphic_backend