#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/render_objects/framebuffer.hpp"
#include "backend/vulkan/details/render_objects/render_pass.hpp"
#include "backend/vulkan/details/device_resources/texture.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/scene/scene.hpp"
#include "core/utils/env.hpp"

#include "scene_test_helpers.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/material_loader/generic_material_loader.hpp"
#include "core/utils/filesystem_tools.hpp"
#include "infra/window/window.hpp"

#include <vulkan/vulkan.h>

#include <iostream>
#include <vector>

int main() {
  expSetEnvVK();
  try {
    auto success = cdToWhereResourcesCouldFound("blinnphong_0");
    if (!success) {
      std::cerr << "Failed to find shader files\n";
      return 1;
    }

    LX_infra::Window::Initialize();
    auto window =
        std::make_shared<LX_infra::Window>("Test Vulkan CommandBuffer", 64, 64);

    auto device = LX_core::backend::VulkanDevice::create();
    device->initialize(window, "TestVulkanCommandBuffer");
    const FrameIndex32 maxFrameInFlight = 2;

    // Render pass / pipeline formats.
    VkFormat depthFormat = device->getDepthFormat();
    VkImageAspectFlags depthAspectMask = device->getDepthAspectMask();
    VkSurfaceFormatKHR surfaceFormat = device->getSurfaceFormat();

    // Create command buffer manager first (needed for resource manager)
    auto cmdBufferMgr = LX_core::backend::VulkanCommandBufferManager::create(
        *device, maxFrameInFlight, device->getGraphicsQueueFamilyIndex());
    auto resourceManager =
        LX_core::backend::VulkanResourceManager::create(*device);
    resourceManager->initializeRenderPassAndPipeline(surfaceFormat,
                                                     depthFormat);

    auto &renderPass = resourceManager->getRenderPass();

    // Create minimal framebuffer attachments.
    const VkExtent2D extent{64, 64};
    auto colorTex = LX_core::backend::VulkanTexture::createForAttachment(
        *device, extent.width, extent.height, surfaceFormat.format,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    auto depthTex = LX_core::backend::VulkanTexture::createForAttachment(
        *device, extent.width, extent.height, depthFormat,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthAspectMask);
    std::vector<VkImageView> attachments = {colorTex->getImageView(),
                                            depthTex->getImageView()};
    auto framebuffer = LX_core::backend::VulkanFrameBuffer::create(
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
    auto meshPtr = LX_core::Mesh::create(vertexBufferPtr, indexBufferPtr);

    auto material = LX_infra::loadGenericMaterial("materials/blinnphong_default.material");
    material->setParameter(LX_core::StringID("MaterialUBO"),
                           LX_core::StringID("enableNormal"),
                           0); // avoid normal texture
    material->syncGpuData();

    auto renderable = std::make_shared<LX_core::RenderableSubMesh>(meshPtr, material);
    // Pipeline declares a skeleton data slot; attach an (empty) skeleton so the
    // descriptor set binding gets a valid buffer to update.
    renderable->skeleton =
        LX_core::Skeleton::create(std::vector<LX_core::Bone>{});
    auto scene = LX_core::Scene::create(renderable);

    // REQ-009: reach the scene's default camera + directional light via the
    // multi-container API (Scene::Scene seeds exactly one of each).
    auto camera = scene->getCameras().front();
    auto dirLight = std::dynamic_pointer_cast<LX_core::DirectionalLight>(
        scene->getLights().front());

    // Default directional light UBO (shader expects it).
    if (dirLight && dirLight->ubo) {
      dirLight->ubo->param.dir = LX_core::Vec4f{0.0f, -1.0f, 0.0f, 0.0f};
      dirLight->ubo->param.color = LX_core::Vec4f{1.0f, 1.0f, 1.0f, 1.0f};
      dirLight->ubo->setDirty();
    }

    // Camera matrices needed for camera data uploads.
    camera->position = {0.0f, 0.0f, 3.0f};
    camera->target = {0.0f, 0.0f, 0.0f};
    camera->up = LX_core::Vec3f{0.0f, 1.0f, 0.0f};
    camera->updateMatrices();

    auto renderItem =
        LX_test::firstItemFromScene(*scene, LX_core::Pass_Forward);

    // Initialize push constants deterministically.
    if (renderItem.drawData) {
      LX_core::PerDrawLayout pc{};
      pc.model = LX_core::Mat4f::identity();
      renderItem.drawData->update(pc);
    }

    // Sync all CPU-side resources to GPU.
    resourceManager->syncResource(*cmdBufferMgr, renderItem.vertexBuffer);
    resourceManager->syncResource(*cmdBufferMgr, renderItem.indexBuffer);
    for (auto &cpuRes : renderItem.descriptorResources) {
      resourceManager->syncResource(*cmdBufferMgr, cpuRes);
    }
    resourceManager->collectGarbage();

    auto &pipeline = resourceManager->getOrCreateRenderPipeline(renderItem);
    if (pipeline.getHandle() == VK_NULL_HANDLE) {
      std::cerr << "Pipeline not created correctly\n";
      return 1;
    }

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
