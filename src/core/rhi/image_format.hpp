#pragma once
#include "core/platform/types.hpp"

namespace LX_core {

/// Core-layer texel format enum. Backends SHALL provide a translation to their
/// native format type (e.g., `VkFormat toVkFormat(ImageFormat)` in the Vulkan
/// backend). Core code MUST NOT reference backend-specific format types.
enum class ImageFormat : u8 {
  RGBA8,
  BGRA8,
  R8,
  D32Float,
  D24UnormS8,
  D32FloatS8,
};

} // namespace LX_core
