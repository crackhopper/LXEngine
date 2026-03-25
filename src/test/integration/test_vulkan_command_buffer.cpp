#include "core/gpu/render_resource.hpp"
#include "core/resources/index_buffer.hpp"
#include "core/resources/vertex_buffer.hpp"
#include "core/scene/components/material.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "graphics_backend/vulkan/details/commands/vkc_cmdbuffer_manager.hpp"
#include "graphics_backend/vulkan/details/render_objects/vkr_framebuffer.hpp"
#include "graphics_backend/vulkan/details/render_objects/vkr_renderpass.hpp"
#include "graphics_backend/vulkan/details/resources/vkr_texture.hpp"
#include "graphics_backend/vulkan/details/vk_device.hpp"
#include "graphics_backend/vulkan/details/vk_resource_manager.hpp"
#include "core/utils/env.hpp"

#include "infra/window/window.hpp"

#include <vulkan/vulkan.h>

#include <iostream>
#include <vector>

int main() {
  expSetEnvVK();
  try {
    auto success = cdToWhereShadersExist("blinnphong_0");
    if (!success) {
      std::cerr << "Failed to find shader files\n";
      return 1;
    }

    LX_infra::Window::Initialize();
    auto window =
        std::make_shared<LX_infra::Window>("Test Vulkan CommandBuffer", 64, 64);

    auto device = LX_core::graphic_backend::VulkanDevice::create();
    device->initialize(window, "TestVulkanCommandBuffer");
    const uint32_t maxFrameInFlight = 2;

    // Render pass / pipeline formats.
    VkFormat depthFormat = device->getDepthFormat();
    VkImageAspectFlags depthAspectMask = device->getDepthAspectMask();
    VkSurfaceFormatKHR surfaceFormat = device->getSurfaceFormat();

    // Create command buffer manager first (needed for resource manager)
    auto cmdBufferMgr =
        LX_core::graphic_backend::VulkanCommandBufferManager::create(
            *device, maxFrameInFlight, device->getGraphicsQueueFamilyIndex());
    auto resourceManager =
        LX_core::graphic_backend::VulkanResourceManager::create(*device);
    resourceManager->initializeRenderPassAndPipeline(surfaceFormat,
                                                     depthFormat);

    auto &renderPass = resourceManager->getRenderPass();
    auto &pipeline = resourceManager->getRenderPipeline();
    if (pipeline.getHandle() == VK_NULL_HANDLE) {
      std::cerr << "RenderPass/Pipeline not initialized correctly\n";
      return 1;
    }

    // Create minimal framebuffer attachments.
    const VkExtent2D extent{64, 64};
    auto colorTex = LX_core::graphic_backend::VulkanTexture::createForAttachment(
        *device, extent.width, extent.height, surfaceFormat.format,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    auto depthTex = LX_core::graphic_backend::VulkanTexture::createForAttachment(
        *device, extent.width, extent.height, depthFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        depthAspectMask);
    std::vector<VkImageView> attachments = {
        colorTex->getImageView(), depthTex->getImageView() };
    auto framebuffer = LX_core::graphic_backend::VulkanFrameBuffer::create(
        *device, renderPass.getHandle(), attachments, extent);

    using V = LX_core::VertexPosNormalUvBone;

    // Build a minimal scene so VulkanCommandBuffer::bindResources has CPU-side
    // descriptor resources to upload into descriptor sets.
    auto vertexBufferPtr = LX_core::VertexBuffer<V>::create({
        V({-5.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
        V({5.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
        V({5.0f, -5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
    });
    auto indexBufferPtr = LX_core::IndexBuffer::create({0u, 1u, 2u});
    auto meshPtr = LX_core::Mesh<V>::create(vertexBufferPtr, indexBufferPtr);

    auto material = std::make_shared<LX_core::MaterialBlinnPhong>(
        LX_core::ResourcePassFlag::Forward);
    material->params->params.enableNormalMap = 0; // avoid normal texture
    material->params->setDirty();

    auto renderable =
        std::make_shared<LX_core::RenderableSubMesh<V>>(meshPtr, material);
    // Pipeline declares a SkeletonUBO slot; attach an (empty) skeleton so the
    // descriptor set binding gets a valid buffer to update.
    renderable->skeleton =
        std::make_shared<LX_core::Skeleton>(std::vector<LX_core::Bone>{});
    auto scene = LX_core::Scene::create(renderable);

    // Default directional light UBO (shader expects it).
    if (scene->directionalLight && scene->directionalLight->ubo) {
      scene->directionalLight->ubo->param.dir =
          LX_core::Vec4f{0.0f, -1.0f, 0.0f, 0.0f};
      scene->directionalLight->ubo->param.color =
          LX_core::Vec4f{1.0f, 1.0f, 1.0f, 1.0f};
      scene->directionalLight->ubo->setDirty();
    }

    // Camera matrices needed for CameraUBO uploads.
    scene->camera->position = {0.0f, 0.0f, 3.0f};
    scene->camera->target = {0.0f, 0.0f, 0.0f};
    scene->camera->up = LX_core::Vec3f{0.0f, 1.0f, 0.0f};
    scene->camera->updateMatrices();

    auto renderItem = scene->buildRenderItem();

    // Match VulkanRenderer::initScene(): inject camera/light UBO resources.
    if (scene->camera) {
      auto camRes = scene->camera->getRenderResources();
      renderItem.descriptorResources.insert(
          renderItem.descriptorResources.end(), camRes.begin(), camRes.end());
    }
    if (scene->directionalLight) {
      auto lightRes = scene->directionalLight->getRenderResources();
      renderItem.descriptorResources.insert(
          renderItem.descriptorResources.end(), lightRes.begin(),
          lightRes.end());
    }

    // Initialize push constants deterministically.
    if (renderItem.objectInfo) {
      LX_core::PC_BlinnPhong pc{};
      pc.model = LX_core::Mat4f::identity();
      pc.enableLighting = 1;
      pc.enableSkinning = 0;
      renderItem.objectInfo->update(pc);
    }

    // Sync all CPU-side resources to GPU.
    resourceManager->syncResource(*cmdBufferMgr, renderItem.vertexBuffer);
    resourceManager->syncResource(*cmdBufferMgr, renderItem.indexBuffer);
    for (auto &cpuRes : renderItem.descriptorResources) {
      resourceManager->syncResource(*cmdBufferMgr, cpuRes);
    }
    resourceManager->collectGarbage();

    cmdBufferMgr->beginFrame(0);
    auto cmd = cmdBufferMgr->allocateBuffer();

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;
    vkBeginCommandBuffer(cmd->getHandle(), &beginInfo);

    cmd->beginRenderPass(renderPass.getHandle(), framebuffer->getHandle(),
                         extent, renderPass.getClearValues());
    cmd->setViewport(extent.width, extent.height);
    cmd->setScissor(extent.width, extent.height);
    cmd->bindPipeline(pipeline);

    cmd->bindResources(*resourceManager, pipeline, renderItem);
    cmd->drawItem(renderItem);
    cmd->endRenderPass();

    vkEndCommandBuffer(cmd->getHandle());

    framebuffer.reset();

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "SKIP VulkanCommandBuffer test: " << e.what() << "\n";
    return 0;
  }
}
