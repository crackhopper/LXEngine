#pragma once

#include "core/asset/material_instance.hpp"
#include "core/utils/string_table.hpp"
#include "vulkan_renderer_types.hpp"

namespace LX_core::backend {

class VulkanPostProcessBuilder final {
public:
  explicit VulkanPostProcessBuilder(const VulkanPostProcessSettings &settings);

  [[nodiscard]] LX_core::MaterialInstanceSharedPtr
  createStandardPostProcessMaterial() const;
  [[nodiscard]] LX_core::MaterialInstanceSharedPtr
  createBloomThresholdMaterial() const;
  [[nodiscard]] LX_core::MaterialInstanceSharedPtr
  createBloomBlurMaterial(LX_core::StringID pass, const char *shaderName) const;
  [[nodiscard]] LX_core::MaterialInstanceSharedPtr
  createSkyboxBackgroundMaterial() const;

private:
  const VulkanPostProcessSettings &m_settings;
};

} // namespace LX_core::backend
