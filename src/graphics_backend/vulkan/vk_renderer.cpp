#include "vk_renderer.hpp"
#include "infra/window/window.hpp"

#include <cstring>
#include <stdexcept>

namespace LX_core::graphic_backend {

VulkanRenderer::VulkanRenderer(LX_core::WindowPtr window)
    : p_window(std::move(window)) {}

VulkanRenderer::~VulkanRenderer() { shutdown(); }

// ============================================================
//  Lifecycle
// ============================================================

void VulkanRenderer::initialize() {
  m_device = VulkanDevice::create();

  createSurface();

  m_swapchain.initialize(m_device.get(), m_surface,
                         static_cast<uint32_t>(p_window->getWidth()),
                         static_cast<uint32_t>(p_window->getHeight()));

  createRenderPass();
  createFramebuffers();
  createCommandBuffers();
  createSyncObjects();

  createCameraResources();
  createDefaultPipeline();
}

void VulkanRenderer::shutdown() {
  if (!m_device)
    return;

  VkDevice device = m_device->getDevice();
  vkDeviceWaitIdle(device);

  m_drawCommands.clear();
  m_meshMap.clear();
  m_textureMap.clear();
  m_frames.clear();

  m_graphicsPipeline.reset();
  m_pipelineLayout.reset();

  for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    if (m_imageAvailableSems[i])
      vkDestroySemaphore(device, m_imageAvailableSems[i], nullptr);
    if (m_renderFinishedSems[i])
      vkDestroySemaphore(device, m_renderFinishedSems[i], nullptr);
    if (m_inFlightFences[i])
      vkDestroyFence(device, m_inFlightFences[i], nullptr);
  }
  m_imageAvailableSems = {};
  m_renderFinishedSems = {};
  m_inFlightFences = {};

  for (auto &fb : m_framebuffers)
    fb.destroy(*m_device);
  m_framebuffers.clear();

  if (m_renderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device, m_renderPass, nullptr);
    m_renderPass = VK_NULL_HANDLE;
  }

  m_swapchain.shutdown();

  if (m_surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(m_device->getInstance(), m_surface, nullptr);
    m_surface = VK_NULL_HANDLE;
  }

  m_device->shutdown();
  m_device.reset();
}

// ============================================================
//  Init sub-steps
// ============================================================

void VulkanRenderer::createSurface() {
  auto *impl = dynamic_cast<LX_infra::WindowImpl *>(p_window.get());
  if (!impl)
    throw std::runtime_error(
        "Window does not support Vulkan surface creation");

  m_surface = impl->getVulkanSurface(m_device->getInstance());
}

void VulkanRenderer::createRenderPass() {
  VkAttachmentDescription colorAttachment{};
  colorAttachment.format = m_swapchain.getFormat();
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorRef{};
  colorRef.attachment = 0;
  colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.attachmentCount = 1;
  info.pAttachments = &colorAttachment;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = 1;
  info.pDependencies = &dependency;

  if (vkCreateRenderPass(m_device->getDevice(), &info, nullptr,
                         &m_renderPass) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create render pass");
  }
}

void VulkanRenderer::createFramebuffers() {
  const auto &imageViews = m_swapchain.getImageViews();
  m_framebuffers.resize(imageViews.size());

  for (size_t i = 0; i < imageViews.size(); ++i) {
    VkImageView attachment = imageViews[i];
    m_framebuffers[i].create(*m_device, m_renderPass, &attachment, 1,
                             m_swapchain.getExtent());
  }
}

void VulkanRenderer::createCommandBuffers() {
  m_commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = m_device->getCommandPool();
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = MAX_FRAMES_IN_FLIGHT;

  if (vkAllocateCommandBuffers(m_device->getDevice(), &allocInfo,
                               m_commandBuffers.data()) != VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate command buffers");
  }
}

void VulkanRenderer::createSyncObjects() {
  VkSemaphoreCreateInfo semInfo{};
  semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  VkDevice device = m_device->getDevice();

  for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    if (vkCreateSemaphore(device, &semInfo, nullptr,
                          &m_imageAvailableSems[i]) != VK_SUCCESS ||
        vkCreateSemaphore(device, &semInfo, nullptr,
                          &m_renderFinishedSems[i]) != VK_SUCCESS ||
        vkCreateFence(device, &fenceInfo, nullptr, &m_inFlightFences[i]) !=
            VK_SUCCESS) {
      throw std::runtime_error("Failed to create synchronization objects");
    }
  }
}

void VulkanRenderer::createDefaultPipeline() {
  auto &dev = *m_device;

  VkDescriptorSetLayout cameraLayout =
      m_frames[0].cameraBinding->getLayoutHandle();

  LX_core::MaterialBlinnPhong defaultMat;
  auto tempBinding = MaterialResourceBinding::create(dev, defaultMat);
  VkDescriptorSetLayout materialLayout = tempBinding->getLayoutHandle();

  m_pipelineLayout = std::make_unique<VulkanPipelineLayout>(dev);
  m_pipelineLayout->addDescriptorSetLayout(cameraLayout);
  m_pipelineLayout->addDescriptorSetLayout(materialLayout);
  m_pipelineLayout->build();

  auto vertShader = std::make_unique<VulkanShaderModule>(dev);
  auto fragShader = std::make_unique<VulkanShaderModule>(dev);
  vertShader->loadFromFile("shaders/default.vert.spv");
  fragShader->loadFromFile("shaders/default.frag.spv");

  m_graphicsPipeline = std::make_unique<VulkanGraphicsPipeline>(dev);
  m_graphicsPipeline->build(m_renderPass, m_pipelineLayout->layout(),
                            m_swapchain.getExtent(), vertShader.get(),
                            fragShader.get());
}

void VulkanRenderer::createCameraResources() {
  for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    PerFrameData frame;
    frame.cameraBinding =
        CameraResourceBinding::create(*m_device, m_camera);
    m_frames.push_back(std::move(frame));
  }
}

// ============================================================
//  Resource upload
// ============================================================

void VulkanRenderer::uploadMesh(LX_core::MeshPtr mesh) {
  if (m_meshMap.count(mesh.get()))
    return;

  m_meshMap[mesh.get()] = VulkanMesh::create(*m_device, *mesh);
}

void VulkanRenderer::uploadTexture(LX_core::TexturePtr texture) {
  if (m_textureMap.count(texture.get()))
    return;

  m_textureMap[texture.get()] = VulkanTexture::create(*m_device, *texture);
}

// ============================================================
//  Rendering
// ============================================================

void VulkanRenderer::setCamera(const LX_core::Mat4f &view,
                               const LX_core::Mat4f &proj) {
  m_camera.viewMatrix = view;
  m_camera.projMatrix = proj;
}

void VulkanRenderer::drawMesh(LX_core::MeshPtr mesh,
                              LX_core::MaterialPtr material) {
  m_drawCommands.push_back({std::move(mesh), std::move(material)});
}

void VulkanRenderer::flush() {
  VkDevice device = m_device->getDevice();

  vkWaitForFences(device, 1, &m_inFlightFences[m_currentFrame], VK_TRUE,
                  UINT64_MAX);

  uint32_t imageIndex;
  VkResult result = vkAcquireNextImageKHR(
      device, m_swapchain.getHandle(), UINT64_MAX,
      m_imageAvailableSems[m_currentFrame], VK_NULL_HANDLE, &imageIndex);

  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    return;

  vkResetFences(device, 1, &m_inFlightFences[m_currentFrame]);

  m_frames[m_currentFrame].cameraBinding->update(*m_device, m_camera);

  auto &matBindings = m_frames[m_currentFrame].materialBindings;
  for (auto &dc : m_drawCommands) {
    if (!dc.material)
      continue;
    auto *key = dc.material.get();
    auto it = matBindings.find(key);
    if (it == matBindings.end()) {
      matBindings[key] =
          MaterialResourceBinding::create(*m_device, *dc.material);
    }
    matBindings[key]->update(*m_device, *dc.material);
  }

  VkCommandBuffer cmd = m_commandBuffers[m_currentFrame];
  vkResetCommandBuffer(cmd, 0);
  recordCommandBuffer(cmd, imageIndex);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore waitSems[] = {m_imageAvailableSems[m_currentFrame]};
  VkPipelineStageFlags waitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSems;
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;

  VkSemaphore signalSems[] = {m_renderFinishedSems[m_currentFrame]};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSems;

  if (vkQueueSubmit(m_device->getGraphicsQueue(), 1, &submitInfo,
                    m_inFlightFences[m_currentFrame]) != VK_SUCCESS) {
    throw std::runtime_error("Failed to submit draw command buffer");
  }

  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSems;

  VkSwapchainKHR swapchains[] = {m_swapchain.getHandle()};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapchains;
  presentInfo.pImageIndices = &imageIndex;

  vkQueuePresentKHR(m_device->getGraphicsQueue(), &presentInfo);

  m_drawCommands.clear();
  m_currentFrame = (m_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::recordCommandBuffer(VkCommandBuffer cmd,
                                         uint32_t imageIndex) {
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  vkBeginCommandBuffer(cmd, &beginInfo);

  VkRenderPassBeginInfo rpInfo{};
  rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rpInfo.renderPass = m_renderPass;
  rpInfo.framebuffer = m_framebuffers[imageIndex].getHandle();
  rpInfo.renderArea.offset = {0, 0};
  rpInfo.renderArea.extent = m_swapchain.getExtent();

  VkClearValue clearColor = {{{0.01f, 0.01f, 0.02f, 1.0f}}};
  rpInfo.clearValueCount = 1;
  rpInfo.pClearValues = &clearColor;

  vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

  if (m_graphicsPipeline) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_graphicsPipeline->pipeline());

    VkPipelineLayout layout = m_pipelineLayout->layout();

    VkDescriptorSet cameraSet =
        m_frames[m_currentFrame].cameraBinding->getDescriptorSetHandle();
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                            &cameraSet, 0, nullptr);

    VkExtent2D extent = m_swapchain.getExtent();

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    for (auto &dc : m_drawCommands) {
      auto meshIt = m_meshMap.find(dc.mesh.get());
      if (meshIt == m_meshMap.end())
        continue;

      auto &gpuMesh = meshIt->second;
      if (gpuMesh->getIndexCount() == 0)
        continue;

      auto matIt =
          m_frames[m_currentFrame].materialBindings.find(dc.material.get());
      if (matIt != m_frames[m_currentFrame].materialBindings.end()) {
        VkDescriptorSet materialSet =
            matIt->second->getDescriptorSetHandle();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                                1, 1, &materialSet, 0, nullptr);
      }

      VkBuffer vertexBuffers[] = {gpuMesh->getVertexBuffer().getHandle()};
      VkDeviceSize offsets[] = {0};
      vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
      vkCmdBindIndexBuffer(cmd, gpuMesh->getIndexBuffer().getHandle(), 0,
                           VK_INDEX_TYPE_UINT32);

      vkCmdDrawIndexed(cmd, gpuMesh->getIndexCount(), 1, 0, 0, 0);
    }
  }

  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);
}

} // namespace LX_core::graphic_backend
