#pragma once
#include "../vk_resources.hpp"
#include "core/resources/skeleton.hpp"
#include "vkb_base.hpp"

namespace LX_core::graphic_backend {

class SkeletonResourceBinding
    : public ResourceBindingBase<SkeletonResourceBinding, LX_core::Skeleton> {

public:
  SkeletonResourceBinding(Base::Token t, VulkanDevice &device,
                          const LX_core::Skeleton &skeleton)
      : ResourceBindingBase<SkeletonResourceBinding, LX_core::Skeleton>(
            t, device, device.getDescriptorAllocator()),
        m_device(device) {
    m_ubo = VulkanUniformBuffer::create(
        m_device, sizeof(LX_core::Mat4f) * LX_core::MAX_BONE_COUNT);
  }
  ~SkeletonResourceBinding() = default;

  static Ptr create(VulkanDevice &device,
                    const LX_core::Skeleton &skeleton) {
    auto p = std::make_unique<SkeletonResourceBinding>(Base::Token{}, device,
                                                       skeleton);
    p->init();
    return p;
  }

  DescriptorSetLayoutCreateInfo getLayoutCreateInfo() override;
  void update(VulkanDevice &device, const LX_core::Skeleton &data) override;

private:
  VulkanDevice &m_device;
  VulkanUniformBufferPtr m_ubo;
};

} // namespace LX_core::graphic_backend
