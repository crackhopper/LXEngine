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
  m_device = std::make_shared<VulkanDevice>();
  m_device->initialize();

  createSurface();

  m_swapchain.initialize(m_device.get(), m_surface,
                         static_cast<uint32_t>(p_window->getWidth()),
                         static_cast<uint32_t>(p_window->getHeight()));

  createRenderPass();
  createFramebuffers();
  createCommandBuffers();
  createSyncObjects();

  m_descriptorAllocator =
      std::make_shared<VulkanDescriptorAllocator>(m_device);

  createDefaultPipeline();
  createCameraResources();
  createDefaultMaterial();
}

void VulkanRenderer::shutdown() {
  if (!m_device)
    return;

  VkDevice device = m_device->getDevice();
  vkDeviceWaitIdle(device);

  m_drawCommands.clear();
  m_materialMap.clear();
  m_defaultMaterial.reset();
  m_meshMap.clear();
  m_textureMap.clear();
  m_frames.clear();

  m_descriptorAllocator.reset();
  m_graphicsPipeline.reset();
  m_pipelineLayout.reset();
  m_materialSetLayout.reset();
  m_cameraSetLayout.reset();

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
    fb.destroy(m_device);
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
    m_framebuffers[i].create(m_device, m_renderPass, &attachment, 1,
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
  // Set 0: camera UBO (per-frame)
  m_cameraSetLayout = std::make_shared<VulkanDescriptorSetLayout>(m_device);
  m_cameraSetLayout->addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                VK_SHADER_STAGE_VERTEX_BIT);
  m_cameraSetLayout->build();

  // Set 1: material albedo sampler (per-draw)
  m_materialSetLayout = std::make_shared<VulkanDescriptorSetLayout>(m_device);
  m_materialSetLayout->addBinding(0,
                                  VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  VK_SHADER_STAGE_FRAGMENT_BIT);
  m_materialSetLayout->build();

  m_pipelineLayout = std::make_shared<VulkanPipelineLayout>(m_device);
  m_pipelineLayout->addDescriptorSetLayout(m_cameraSetLayout->layout());
  m_pipelineLayout->addDescriptorSetLayout(m_materialSetLayout->layout());
  m_pipelineLayout->build();

  auto vertShader = std::make_shared<VulkanShaderModule>(m_device);
  auto fragShader = std::make_shared<VulkanShaderModule>(m_device);
  vertShader->loadFromFile("shaders/default.vert.spv");
  fragShader->loadFromFile("shaders/default.frag.spv");

  m_graphicsPipeline = std::make_shared<VulkanGraphicsPipeline>(m_device);
  m_graphicsPipeline->build(m_renderPass, m_pipelineLayout->layout(),
                            m_swapchain.getExtent(), vertShader.get(),
                            fragShader.get());
}

void VulkanRenderer::createCameraResources() {
  for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    m_frames.emplace_back(m_device);
    auto &frame = m_frames.back();

    frame.cameraUBO.create(sizeof(CameraUBO));

    VkDescriptorSetLayout layout = m_cameraSetLayout->layout();
    frame.descriptorSet = m_descriptorAllocator->allocate(&layout, 1);

    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = frame.cameraUBO.getBuffer();
    bufInfo.offset = 0;
    bufInfo.range = sizeof(CameraUBO);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = frame.descriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &bufInfo;

    vkUpdateDescriptorSets(m_device->getDevice(), 1, &write, 0, nullptr);
  }
}

// ============================================================
//  Resource upload
// ============================================================

void VulkanRenderer::uploadMesh(LX_core::MeshPtr mesh) {
  if (m_meshMap.count(mesh.get()))
    return;

  auto gpuMesh = std::make_shared<VulkanMesh>(m_device);
  gpuMesh->upload(*mesh);
  m_meshMap[mesh.get()] = gpuMesh;
}

void VulkanRenderer::uploadTexture(LX_core::TexturePtr texture) {
  if (m_textureMap.count(texture.get()))
    return;

  auto gpuTex = std::make_shared<VulkanTexture>(m_device);
  gpuTex->upload(*texture);
  m_textureMap[texture.get()] = gpuTex;
}

void VulkanRenderer::createDefaultMaterial() {
  std::vector<uint8_t> whitePixel = {255, 255, 255, 255};
  LX_core::TextureDesc desc{1, 1, LX_core::TextureFormat::RGBA8};
  LX_core::Texture whiteTex(desc, std::move(whitePixel));

  auto gpuTex = std::make_shared<VulkanTexture>(m_device);
  gpuTex->upload(whiteTex);

  m_defaultMaterial =
      std::make_shared<VulkanMaterial>(m_device, m_descriptorAllocator);
  m_defaultMaterial->setDescriptorSetLayout(m_materialSetLayout->layout());
  m_defaultMaterial->bindTexture(0, gpuTex);
  m_defaultMaterial->buildDescriptorSet();
}

VulkanMaterialPtr
VulkanRenderer::getOrCreateMaterial(LX_core::MaterialPtr material) {
  if (!material || !material->albedoMap.has_value())
    return m_defaultMaterial;

  auto it = m_materialMap.find(material.get());
  if (it != m_materialMap.end())
    return it->second;

  auto &coreTex = material->albedoMap.value();
  uploadTexture(coreTex);

  auto texIt = m_textureMap.find(coreTex.get());
  if (texIt == m_textureMap.end())
    return m_defaultMaterial;

  auto gpuMat =
      std::make_shared<VulkanMaterial>(m_device, m_descriptorAllocator);
  gpuMat->setDescriptorSetLayout(m_materialSetLayout->layout());
  gpuMat->bindTexture(0, texIt->second);
  gpuMat->buildDescriptorSet();

  m_materialMap[material.get()] = gpuMat;
  return gpuMat;
}

// ============================================================
//  Rendering
// ============================================================

void VulkanRenderer::setCamera(const LX_core::Mat4f &view,
                               const LX_core::Mat4f &proj) {
  m_cameraData.view = view;
  m_cameraData.proj = proj;
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

  m_frames[m_currentFrame].cameraUBO.update(&m_cameraData, sizeof(CameraUBO));

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

    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                            &m_frames[m_currentFrame].descriptorSet, 0,
                            nullptr);

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
      if (!gpuMesh->vertexBuffer || !gpuMesh->indexBuffer)
        continue;

      auto gpuMat = getOrCreateMaterial(dc.material);
      VkDescriptorSet matSet = gpuMat->descriptorSet();
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1,
                              1, &matSet, 0, nullptr);

      VkBuffer vertexBuffers[] = {gpuMesh->vertexBuffer->buffer};
      VkDeviceSize offsets[] = {0};
      vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
      vkCmdBindIndexBuffer(cmd, gpuMesh->indexBuffer->buffer, 0,
                           VK_INDEX_TYPE_UINT32);

      vkCmdDrawIndexed(cmd, gpuMesh->indexCount, 1, 0, 0, 0);
    }
  }

  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);
}

} // namespace LX_core::graphic_backend
