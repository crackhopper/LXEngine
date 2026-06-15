#pragma once
#include "core/platform/types.hpp"

namespace LX_core {

/// Core-layer texel format enum. Backends SHALL provide a translation to their
/// native format type (e.g., `VkFormat toVkFormat(ImageFormat)` in the Vulkan
/// backend). Core code MUST NOT reference backend-specific format types.
enum class ImageFormat : u8 {
  RGBA8,
  RGBA16Float,
  BGRA8,
  R8,
  D32Float,
  D24UnormS8,
  D32FloatS8,
  RGBA8Srgb,
  BGRA8Srgb,
};

[[nodiscard]] constexpr bool isSrgbImageFormat(ImageFormat format) {
  return format == ImageFormat::RGBA8Srgb || format == ImageFormat::BGRA8Srgb;
}

} // namespace LX_core
