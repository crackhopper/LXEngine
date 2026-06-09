#include "render_pass.hpp"
#include "../device.hpp"
#include <stdexcept>
#include <vector>

namespace LX_core {
namespace backend {

VulkanRenderPass::VulkanRenderPass(Token, VulkanDevice &device,
                                   std::optional<VkFormat> colorFormat,
                                   std::optional<VkFormat> depthFormat,
                                   bool presentColorFinalLayout)
    : VulkanRenderPass(Token{}, device,
                       colorFormat.has_value()
                           ? std::vector<VkFormat>{*colorFormat}
                           : std::vector<VkFormat>{},
                       depthFormat, presentColorFinalLayout) {}

VulkanRenderPass::VulkanRenderPass(Token, VulkanDevice &device,
                                   std::vector<VkFormat> colorFormats,
                                   std::optional<VkFormat> depthFormat,
                                   bool presentColorFinalLayout)
    : m_device(device),
      m_depthFormat(depthFormat.value_or(VK_FORMAT_UNDEFINED)),
      m_hasColorAttachment(!colorFormats.empty()),
      m_colorAttachmentCount(static_cast<u32>(colorFormats.size())),
      m_hasDepthAttachment(depthFormat.has_value()) {
  if (!m_hasColorAttachment && !m_hasDepthAttachment) {
    throw std::runtime_error(
        "VulkanRenderPass requires at least one attachment");
  }

  std::vector<VkAttachmentDescription> attachments;
  std::vector<VkAttachmentReference> colorAttachmentRefs;
  std::vector<VkClearValue> clearValues;
  attachments.reserve(colorFormats.size() +
                      (depthFormat.has_value() ? 1u : 0u));
  colorAttachmentRefs.reserve(colorFormats.size());
  clearValues.reserve(attachments.capacity());

  for (const auto colorFormat : colorFormats) {
    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = static_cast<u32>(attachments.size());
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout =
        presentColorFinalLayout ? VK_IMAGE_LAYOUT_UNDEFINED
                                : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout =
        presentColorFinalLayout ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                                : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments.push_back(colorAttachment);
    colorAttachmentRefs.push_back(colorAttachmentRef);

    VkClearValue clear{};
    clear.color = {0.0f, 0.0f, 0.0f, 1.0f};
    clearValues.push_back(clear);
  }

  VkAttachmentReference depthAttachmentRef{};
  if (depthFormat.has_value()) {
    depthAttachmentRef.attachment = static_cast<u32>(attachments.size());
    depthAttachmentRef.layout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = *depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depthAttachment.finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments.push_back(depthAttachment);

    VkClearValue clear{};
    clear.depthStencil = {1.0f, 0};
    clearValues.push_back(clear);
  }

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = static_cast<u32>(colorAttachmentRefs.size());
  subpass.pColorAttachments =
      colorAttachmentRefs.empty() ? nullptr : colorAttachmentRefs.data();
  subpass.pDepthStencilAttachment =
      depthFormat.has_value() ? &depthAttachmentRef : nullptr;

  std::vector<VkSubpassDependency> dependencies(2);
  VkPipelineStageFlags attachmentStages = 0;
  VkAccessFlags attachmentAccess = 0;
  if (!colorFormats.empty()) {
    attachmentStages |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    attachmentAccess |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  }
  if (depthFormat.has_value()) {
    attachmentStages |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    attachmentAccess |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  }
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = attachmentStages;
  dependencies[0].srcAccessMask = attachmentAccess;
  dependencies[0].dstStageMask = attachmentStages;
  dependencies[0].dstAccessMask = attachmentAccess;

  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[1].srcStageMask = attachmentStages;
  dependencies[1].srcAccessMask = attachmentAccess;
  dependencies[1].dstStageMask = attachmentStages;
  dependencies[1].dstAccessMask = attachmentAccess;

  VkRenderPassCreateInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = static_cast<u32>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = static_cast<u32>(dependencies.size());
  renderPassInfo.pDependencies = dependencies.data();

  if (vkCreateRenderPass(m_device.getLogicalDevice(), &renderPassInfo, nullptr,
                         &m_renderPass) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create render pass!");
  }

  m_clearValues = std::move(clearValues);
}

VulkanRenderPass::~VulkanRenderPass() {
  if (m_renderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(m_device.getLogicalDevice(), m_renderPass, nullptr);
    m_renderPass = VK_NULL_HANDLE;
  }
}

void VulkanRenderPass::setClearColor(float r, float g, float b, float a) {
  if (!m_hasColorAttachment || m_clearValues.empty()) {
    return;
  }
  m_clearValues[0].color = {r, g, b, a};
}

} // namespace backend
} // namespace LX_core
