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
}

void VulkanRenderer::shutdown() {
  if (!m_device)
    return;

  VkDevice device = m_device->getDevice();
  vkDeviceWaitIdle(device);

  m_drawCommands.clear();
  m_meshMap.clear();
  m_textureMap.clear();

  for (auto &frame : m_frames) {
    if (frame.cameraBuffer != VK_NULL_HANDLE) {
      vkUnmapMemory(device, frame.cameraMemory);
      vkDestroyBuffer(device, frame.cameraBuffer, nullptr);
      vkFreeMemory(device, frame.cameraMemory, nullptr);
      frame.cameraBuffer = VK_NULL_HANDLE;
    }
  }

  m_descriptorAllocator.reset();
  m_graphicsPipeline.reset();
  m_pipelineLayout.reset();
  m_descriptorSetLayout.reset();

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
  m_descriptorSetLayout =
      std::make_shared<VulkanDescriptorSetLayout>(m_device);
  m_descriptorSetLayout->addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                    VK_SHADER_STAGE_VERTEX_BIT);
  m_descriptorSetLayout->build();

  m_pipelineLayout = std::make_shared<VulkanPipelineLayout>(m_device);
  m_pipelineLayout->setDescriptorSetLayout(m_descriptorSetLayout->layout());
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
  VkDeviceSize bufferSize = sizeof(CameraUBO);

  for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    createVkBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   m_frames[i].cameraBuffer, m_frames[i].cameraMemory);

    vkMapMemory(m_device->getDevice(), m_frames[i].cameraMemory, 0,
                bufferSize, 0, &m_frames[i].cameraMapped);

    VkDescriptorSetLayout layout = m_descriptorSetLayout->layout();
    m_frames[i].descriptorSet = m_descriptorAllocator->allocate(&layout, 1);

    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = m_frames[i].cameraBuffer;
    bufInfo.offset = 0;
    bufInfo.range = sizeof(CameraUBO);

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_frames[i].descriptorSet;
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

  if (mesh->subMeshCount() == 0)
    return;

  auto gpuMesh = std::make_shared<VulkanMesh>(m_device);

  std::vector<uint8_t> vertexData;
  std::vector<uint8_t> indexData;
  uint32_t totalIndexCount = 0;

  for (size_t s = 0; s < mesh->subMeshCount(); ++s) {
    const auto *sub = mesh->subMesh(s);

    const auto *vb = sub->vertexBuffer();
    auto vbBegin = static_cast<const uint8_t *>(vb->data());
    vertexData.insert(vertexData.end(), vbBegin, vbBegin + vb->size());

    const auto *ib = sub->indexBuffer();
    auto ibBegin = static_cast<const uint8_t *>(ib->data());
    indexData.insert(indexData.end(), ibBegin, ibBegin + ib->size());

    totalIndexCount += static_cast<uint32_t>(ib->indexCount());
  }

  VkDevice device = m_device->getDevice();

  // Vertex buffer (staging → device-local)
  {
    VkDeviceSize sz = vertexData.size();
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    createVkBuffer(sz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   staging, stagingMem);

    void *mapped;
    vkMapMemory(device, stagingMem, 0, sz, 0, &mapped);
    std::memcpy(mapped, vertexData.data(), sz);
    vkUnmapMemory(device, stagingMem);

    gpuMesh->vertexBuffer = std::make_unique<VulkanBuffer>(m_device);
    createVkBuffer(sz,
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                   gpuMesh->vertexBuffer->buffer,
                   gpuMesh->vertexBuffer->memory);
    gpuMesh->vertexBuffer->size = sz;

    copyBuffer(staging, gpuMesh->vertexBuffer->buffer, sz);

    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMem, nullptr);
  }

  // Index buffer (staging → device-local)
  {
    VkDeviceSize sz = indexData.size();
    VkBuffer staging;
    VkDeviceMemory stagingMem;
    createVkBuffer(sz, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   staging, stagingMem);

    void *mapped;
    vkMapMemory(device, stagingMem, 0, sz, 0, &mapped);
    std::memcpy(mapped, indexData.data(), sz);
    vkUnmapMemory(device, stagingMem);

    gpuMesh->indexBuffer = std::make_unique<VulkanBuffer>(m_device);
    createVkBuffer(sz,
                   VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                       VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                   gpuMesh->indexBuffer->buffer,
                   gpuMesh->indexBuffer->memory);
    gpuMesh->indexBuffer->size = sz;

    copyBuffer(staging, gpuMesh->indexBuffer->buffer, sz);

    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMem, nullptr);
  }

  gpuMesh->indexCount = totalIndexCount;
  m_meshMap[mesh.get()] = gpuMesh;
}

void VulkanRenderer::uploadTexture(LX_core::TexturePtr texture) {
  if (m_textureMap.count(texture.get()))
    return;

  const auto &desc = texture->desc();
  VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
  if (desc.format == LX_core::TextureFormat::RGB8)
    format = VK_FORMAT_R8G8B8_SRGB;
  else if (desc.format == LX_core::TextureFormat::R8)
    format = VK_FORMAT_R8_SRGB;

  VkDevice device = m_device->getDevice();
  auto gpuTex = std::make_shared<VulkanTexture>(m_device);

  // Staging buffer
  VkDeviceSize imageSize = texture->size();
  VkBuffer staging;
  VkDeviceMemory stagingMem;
  createVkBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 staging, stagingMem);

  void *mapped;
  vkMapMemory(device, stagingMem, 0, imageSize, 0, &mapped);
  std::memcpy(mapped, texture->data(), imageSize);
  vkUnmapMemory(device, stagingMem);

  // Image
  gpuTex->image = std::make_unique<VulkanImage>(m_device);
  createVkImage(desc.width, desc.height, format,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                gpuTex->image->image, gpuTex->image->memory);

  transitionImageLayout(gpuTex->image->image, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage(staging, gpuTex->image->image, desc.width, desc.height);
  transitionImageLayout(gpuTex->image->image,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  vkDestroyBuffer(device, staging, nullptr);
  vkFreeMemory(device, stagingMem, nullptr);

  // Image view
  gpuTex->imageView = std::make_unique<VulkanImageView>(m_device);

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = gpuTex->image->image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(device, &viewInfo, nullptr,
                        &gpuTex->imageView->view) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create texture image view");
  }

  // Sampler
  gpuTex->sampler = std::make_unique<VulkanSampler>(m_device);

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_FALSE;
  samplerInfo.maxAnisotropy = 1.0f;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

  if (vkCreateSampler(device, &samplerInfo, nullptr,
                      &gpuTex->sampler->sampler) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create texture sampler");
  }

  m_textureMap[texture.get()] = gpuTex;
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

  std::memcpy(m_frames[m_currentFrame].cameraMapped, &m_cameraData,
              sizeof(CameraUBO));

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
      auto it = m_meshMap.find(dc.mesh.get());
      if (it == m_meshMap.end())
        continue;

      auto &gpuMesh = it->second;
      if (!gpuMesh->vertexBuffer || !gpuMesh->indexBuffer)
        continue;

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

// ============================================================
//  Vulkan helpers
// ============================================================

uint32_t
VulkanRenderer::findMemoryType(uint32_t typeFilter,
                               VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProps;
  vkGetPhysicalDeviceMemoryProperties(m_device->getPhysicalDevice(),
                                      &memProps);

  for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
    if ((typeFilter & (1 << i)) &&
        (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("Failed to find suitable memory type");
}

void VulkanRenderer::createVkBuffer(VkDeviceSize size,
                                    VkBufferUsageFlags usage,
                                    VkMemoryPropertyFlags properties,
                                    VkBuffer &buffer,
                                    VkDeviceMemory &memory) {
  VkDevice device = m_device->getDevice();

  VkBufferCreateInfo bufInfo{};
  bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufInfo.size = size;
  bufInfo.usage = usage;
  bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(device, &bufInfo, nullptr, &buffer) != VK_SUCCESS)
    throw std::runtime_error("Failed to create buffer");

  VkMemoryRequirements memReqs;
  vkGetBufferMemoryRequirements(device, buffer, &memReqs);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(memReqs.memoryTypeBits, properties);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
    throw std::runtime_error("Failed to allocate buffer memory");

  vkBindBufferMemory(device, buffer, memory, 0);
}

void VulkanRenderer::copyBuffer(VkBuffer src, VkBuffer dst,
                                VkDeviceSize size) {
  VkCommandBuffer cmd = beginSingleTimeCommands();

  VkBufferCopy region{};
  region.size = size;
  vkCmdCopyBuffer(cmd, src, dst, 1, &region);

  endSingleTimeCommands(cmd);
}

VkCommandBuffer VulkanRenderer::beginSingleTimeCommands() {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = m_device->getCommandPool();
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer cmd;
  vkAllocateCommandBuffers(m_device->getDevice(), &allocInfo, &cmd);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(cmd, &beginInfo);
  return cmd;
}

void VulkanRenderer::endSingleTimeCommands(VkCommandBuffer cmd) {
  vkEndCommandBuffer(cmd);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &cmd;

  vkQueueSubmit(m_device->getGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(m_device->getGraphicsQueue());

  vkFreeCommandBuffers(m_device->getDevice(), m_device->getCommandPool(), 1,
                       &cmd);
}

void VulkanRenderer::createVkImage(uint32_t w, uint32_t h, VkFormat format,
                                   VkImageUsageFlags usage, VkImage &image,
                                   VkDeviceMemory &memory) {
  VkDevice device = m_device->getDevice();

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent = {w, h, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS)
    throw std::runtime_error("Failed to create image");

  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(device, image, &memReqs);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReqs.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &memory) != VK_SUCCESS)
    throw std::runtime_error("Failed to allocate image memory");

  vkBindImageMemory(device, image, memory, 0);
}

void VulkanRenderer::transitionImageLayout(VkImage image,
                                           VkImageLayout oldLayout,
                                           VkImageLayout newLayout) {
  VkCommandBuffer cmd = beginSingleTimeCommands();

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags srcStage;
  VkPipelineStageFlags dstStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else {
    srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  }

  vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1,
                       &barrier);

  endSingleTimeCommands(cmd);
}

void VulkanRenderer::copyBufferToImage(VkBuffer buffer, VkImage image,
                                       uint32_t w, uint32_t h) {
  VkCommandBuffer cmd = beginSingleTimeCommands();

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {w, h, 1};

  vkCmdCopyBufferToImage(cmd, buffer, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  endSingleTimeCommands(cmd);
}

} // namespace LX_core::graphic_backend
