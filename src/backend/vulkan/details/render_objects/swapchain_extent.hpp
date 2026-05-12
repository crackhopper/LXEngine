#pragma once

#include "core/platform/types.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <vulkan/vulkan.h>

namespace LX_core::backend {

inline std::optional<VkExtent2D>
resolveSwapchainExtent(const VkExtent2D requestedExtent,
                       const VkSurfaceCapabilitiesKHR& capabilities) {
  if (capabilities.currentExtent.width !=
      std::numeric_limits<u32>::max()) {
    if (capabilities.currentExtent.width == 0 ||
        capabilities.currentExtent.height == 0) {
      return std::nullopt;
    }
    return capabilities.currentExtent;
  }

  if (requestedExtent.width == 0 || requestedExtent.height == 0) {
    return std::nullopt;
  }

  VkExtent2D actualExtent = requestedExtent;
  actualExtent.width = std::clamp(actualExtent.width,
                                  capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
  actualExtent.height = std::clamp(actualExtent.height,
                                   capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
  if (actualExtent.width == 0 || actualExtent.height == 0) {
    return std::nullopt;
  }
  return actualExtent;
}

} // namespace LX_core::backend
