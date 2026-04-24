#pragma once

#include "core/rhi/image_format.hpp"
#include "core/platform/types.hpp"
#include <cstddef>
#include <cstdint>

namespace LX_core {

/// Describes a render pass attachment set (color + depth + MSAA). Not part of
/// PipelineKey today; reserved for future multi-target support.
struct RenderTarget {
  ImageFormat colorFormat = ImageFormat::BGRA8;
  ImageFormat depthFormat = ImageFormat::D32Float;
  SampleCount8 sampleCount = 1;

  size_t getHash() const;

  /// Field-by-field equality. REQ-009 uses this for Camera::matchesTarget.
  /// Note: if RenderTarget gains new fields (e.g., attachment handles), this
  /// operator must be updated in lockstep.
  bool operator==(const RenderTarget &other) const {
    return colorFormat == other.colorFormat &&
           depthFormat == other.depthFormat &&
           sampleCount == other.sampleCount;
  }
  bool operator!=(const RenderTarget &other) const { return !(*this == other); }
};

} // namespace LX_core
