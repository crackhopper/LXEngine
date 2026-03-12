#include "vk_renderpass.hpp"
#include <stdexcept>

namespace LX_core::graphic_backend {

VulkanRenderPass::VulkanRenderPass(
    VulkanDevice &device, Type type,
    std::vector<VkFormat> colorFormats,
    VkFormat depthFormat)
    : mDevice(device) {
  init(type, colorFormats, depthFormat);
}

VulkanRenderPass::~VulkanRenderPass() {
  if (mRenderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(mDevice.getHandle(), mRenderPass, nullptr);
    mRenderPass = VK_NULL_HANDLE;
  }
}

VkAttachmentLoadOp VulkanRenderPass::getDefaultLoadOp(Type type) const {
  switch (type) {
  case Type::Swapchain:
  case Type::Offscreen:
  case Type::GBuffer:
  case Type::DepthOnly:
    return VK_ATTACHMENT_LOAD_OP_CLEAR;
  default:
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  }
}

VkAttachmentStoreOp VulkanRenderPass::getDefaultStoreOp(Type type) const {
  switch (type) {
  case Type::Swapchain:
  case Type::Offscreen:
  case Type::GBuffer:
    return VK_ATTACHMENT_STORE_OP_STORE;
  case Type::DepthOnly:
    return VK_ATTACHMENT_STORE_OP_DONT_CARE;
  default:
    return VK_ATTACHMENT_STORE_OP_DONT_CARE;
  }
}

VkImageLayout VulkanRenderPass::getDefaultInitialLayout(Type type) const {
  switch (type) {
  case Type::Swapchain:
  case Type::Offscreen:
  case Type::GBuffer:
  case Type::DepthOnly:
  default:
    return VK_IMAGE_LAYOUT_UNDEFINED;
  }
}

VkImageLayout VulkanRenderPass::getDefaultFinalLayout(Type type) const {
  switch (type) {
  case Type::Swapchain:
    return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  case Type::Offscreen:
  case Type::GBuffer:
    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  case Type::DepthOnly:
    return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  default:
    return VK_IMAGE_LAYOUT_UNDEFINED;
  }
}

void VulkanRenderPass::init(
    Type type,
    std::vector<VkFormat> colorFormats,
    VkFormat depthFormat) {
  std::vector<VkAttachmentDescription> attachments;
  std::vector<VkAttachmentReference> colorRefs;

  for (uint32_t i = 0; i < colorFormats.size(); ++i) {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = colorFormats[i];
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = getDefaultLoadOp(type);
    colorAttachment.storeOp = getDefaultStoreOp(type);
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = getDefaultInitialLayout(type);
    colorAttachment.finalLayout = getDefaultFinalLayout(type);

    attachments.push_back(colorAttachment);

    VkAttachmentReference ref{};
    ref.attachment = static_cast<uint32_t>(attachments.size() - 1);
    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorRefs.push_back(ref);
  }

  VkAttachmentReference depthRef{};
  bool hasDepth = (type == Type::Swapchain || type == Type::Offscreen ||
                   type == Type::GBuffer || type == Type::DepthOnly);
  if (hasDepth) {
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    attachments.push_back(depthAttachment);

    depthRef.attachment = static_cast<uint32_t>(attachments.size() - 1);
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  }

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
  subpass.pColorAttachments = colorRefs.empty() ? nullptr : colorRefs.data();
  subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

  VkRenderPassCreateInfo rpInfo{};
  rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  rpInfo.pAttachments = attachments.data();
  rpInfo.subpassCount = 1;
  rpInfo.pSubpasses = &subpass;
  rpInfo.dependencyCount = 0;
  rpInfo.pDependencies = nullptr;

  if (vkCreateRenderPass(mDevice.getHandle(), &rpInfo, nullptr,
                         &mRenderPass) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create VulkanRenderPass");
  }
}

VulkanFramebuffer::~VulkanFramebuffer() {}

void VulkanFramebuffer::create(VulkanDevice &device, VkRenderPass renderPass,
                               const VkImageView *attachments,
                               uint32_t attachmentCount, VkExtent2D extent) {
  VkFramebufferCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  info.renderPass = renderPass;
  info.attachmentCount = attachmentCount;
  info.pAttachments = attachments;
  info.width = extent.width;
  info.height = extent.height;
  info.layers = 1;

  if (vkCreateFramebuffer(device.getHandle(), &info, nullptr, &framebuffer) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create framebuffer");
  }
}

void VulkanFramebuffer::destroy(VulkanDevice &device) {
  if (framebuffer != VK_NULL_HANDLE) {
    vkDestroyFramebuffer(device.getHandle(), framebuffer, nullptr);
    framebuffer = VK_NULL_HANDLE;
  }
}

} // namespace LX_core::graphic_backend
