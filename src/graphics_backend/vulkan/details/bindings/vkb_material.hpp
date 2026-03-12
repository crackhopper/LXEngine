#pragma once
#include "../vk_resources.hpp"
#include "core/resources/material.hpp"
#include "vkb_base.hpp"

namespace LX_core::graphic_backend {

class MaterialResourceBinding
    : public ResourceBindingBase<MaterialResourceBinding, LX_core::MaterialBase> {

public:
  struct MaterialUBO {
    float baseColor[4];     // rgb + alpha/padding
    float specularColor[4]; // rgb + shininess
    float metallic;
    float roughness;
    float _pad[2];
  };

  MaterialResourceBinding(Base::Token t, VulkanDevice &device,
                          const LX_core::MaterialBase &material)
      : ResourceBindingBase<MaterialResourceBinding, LX_core::MaterialBase>(
            t, device, device.getDescriptorAllocator()),
        m_device(device) {
    m_ubo = VulkanUniformBuffer::create(m_device, sizeof(MaterialUBO));
  }
  ~MaterialResourceBinding() = default;

  static Ptr create(VulkanDevice &device,
                    const LX_core::MaterialBase &material) {
    auto p = std::make_unique<MaterialResourceBinding>(Base::Token{}, device,
                                                       material);
    p->init();
    return p;
  }

  DescriptorSetLayoutCreateInfo getLayoutCreateInfo() override;
  void update(VulkanDevice &device, const LX_core::MaterialBase &data) override;

private:
  VulkanDevice &m_device;
  VulkanUniformBufferPtr m_ubo;
};

} // namespace LX_core::graphic_backend
