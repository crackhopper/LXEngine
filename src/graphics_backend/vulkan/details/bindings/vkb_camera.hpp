#pragma once
#include "../vk_resources.hpp"
#include "core/scene/camera.hpp"
#include "vkb_base.hpp"

namespace LX_core::graphic_backend {
class CameraResourceBinding
    : public ResourceBindingBase<CameraResourceBinding, LX_core::Camera> {

public:
  CameraResourceBinding(Base::Token t, VulkanDevice &device,
                        const LX_core::Camera &camera)
      : ResourceBindingBase<CameraResourceBinding, LX_core::Camera>(
            t, device, device.getDescriptorAllocator()),
        m_device(device) {
    m_ubo = VulkanUniformBuffer::create(m_device,
                                        sizeof(LX_core::Camera::CameraUBO));
  }
  ~CameraResourceBinding() = default;
  static Ptr create(VulkanDevice &device, const LX_core::Camera &camera) {
    auto p =
        std::make_unique<CameraResourceBinding>(Base::Token{}, device, camera);
    p->init();
    return p;
  }

  DescriptorSetLayoutCreateInfo getLayoutCreateInfo() override;
  void update(VulkanDevice &device, const LX_core::Camera &data) override;

private:
  VulkanDevice &m_device;
  VulkanUniformBufferPtr m_ubo;
};

} // namespace LX_core::graphic_backend