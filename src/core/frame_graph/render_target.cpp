#include "core/frame_graph/render_target.hpp"

#include "core/asset/shader.hpp" // for hash_combine

namespace LX_core {

size_t RenderTarget::getHash() const {
  size_t h = 0;
  hash_combine(h, static_cast<uint32_t>(colorFormat));
  hash_combine(h, static_cast<uint32_t>(depthFormat));
  hash_combine(h, static_cast<uint32_t>(sampleCount));
  return h;
}

} // namespace LX_core
