#include "vkr_rendercontext.hpp"

namespace LX_core::backend {
  VulkanRenderContext::VulkanRenderContext(VulkanRenderPass &rp, VulkanFrameBuffer &fb, VkExtent2D extent)
      : renderPass(rp), framebuffer(fb), renderExtent(extent) {}
}
