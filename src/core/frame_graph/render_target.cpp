#include "core/frame_graph/render_target.hpp"

#include "core/utils/hash.hpp"

namespace LX_core {

size_t RenderTarget::getHash() const {
  size_t h = 0;
  hash_combine(h, static_cast<u32>(colorFormat));
  hash_combine(h, static_cast<u32>(depthFormat));
  hash_combine(h, static_cast<u32>(sampleCount));
  return h;
}

} // namespace LX_core
