#include "backend/vulkan/details/commands/command_buffer_manager.hpp"
#include "backend/vulkan/details/descriptors/descriptor_manager.hpp"
#include "backend/vulkan/details/device.hpp"
#include "backend/vulkan/details/device_resources/texture.hpp"
#include "backend/vulkan/details/render_objects/framebuffer.hpp"
#include "backend/vulkan/details/render_objects/render_pass.hpp"
#include "backend/vulkan/details/resource_manager.hpp"
#include "core/frame_graph/render_queue.hpp"
#include "core/frame_graph/render_upload_plan.hpp"
#include "core/rhi/gpu_resource.hpp"
#include "core/rhi/index_buffer.hpp"
#include "core/rhi/vertex_buffer.hpp"
#include "core/utils/env.hpp"

#include "core/utils/filesystem_tools.hpp"
#include "infra/window/window.hpp"
#include "scene_test_helpers.hpp"

#include <vulkan/vulkan.h>

#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

bool isKnownEnvironmentSetupFailure(const std::string_view message) {
  return message.find("No available video device") != std::string_view::npos ||
         message.find("Failed to find GPUs with Vulkan support") !=
             std::string_view::npos ||
         message.find("Failed to find a suitable GPU") !=
             std::string_view::npos ||
         message.find("Failed to create Vulkan surface handle") !=
             std::string_view::npos;
}

} // namespace

int main() {
  expSetEnvVK();
  bool commandRecordingStarted = false;
  try {
    auto success = initializeRuntimeAssetRoot();
    if (!success) {
      std::cerr << "Failed to find shader files\n";
      return 1;
    }

    LX_infra::Window::Initialize();
    auto window =
        std::make_shared<LX_infra::Window>("Test Vulkan CommandBuffer", 64, 64);

    auto device = LX_core::backend::VulkanDevice::create();
    device->initialize(window, "TestVulkanCommandBuffer");
    const u32 maxFrameInFlight = 2;

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

    // Build a minimal non-material direct helper item.
    auto vertexBufferPtr = LX_core::VertexBuffer<V>::create({
        V({-5.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
        V({5.0f, 5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
        V({5.0f, -5.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f},
          {1.0f, 0.0f, 0.0f, 0.0f}, {0, 0, 0, 0}, {1.0f, 0.0f, 0.0f, 0.0f}),
    });
    auto indexBufferPtr = LX_core::IndexBuffer::create(
        {0u, 1u, 2u, 0u, 1u, 2u, 0u, 2u, 1u});
    auto renderItem = LX_test::makeMinimalDirectRasterHelperItemForVulkanTests(
        *vertexBufferPtr, *indexBufferPtr);

    // Sync all CPU-side resources to GPU.
    LX_core::RenderWorkQueue uploadQueue;
    uploadQueue.addItem(renderItem);
    const LX_core::RenderUploadPlan uploadPlan =
        LX_core::buildRenderUploadPlan(uploadQueue);
    for (const auto &resource : uploadPlan.resources) {
      resourceManager->syncResource(*cmdBufferMgr, resource);
    }
    resourceManager->collectGarbage();

    auto pipeline = resourceManager->getOrCreatePipeline(renderItem);
    const VkPipeline pipelineHandle =
        std::visit([](auto ref) { return ref.get().getHandle(); }, pipeline);
    if (pipelineHandle == VK_NULL_HANDLE) {
      std::cerr << "Pipeline not created correctly\n";
      return 1;
    }

    LX_core::RenderBatchGeometryResources batchGeometry;
    batchGeometry.vertexBuffer = LX_core::GpuResourceRef{*vertexBufferPtr};
    batchGeometry.indexBuffer = LX_core::GpuResourceRef{*indexBufferPtr};
    resourceManager->syncResource(*cmdBufferMgr, batchGeometry.vertexBuffer);
    resourceManager->syncResource(*cmdBufferMgr, batchGeometry.indexBuffer);
    resourceManager->collectGarbage();

    LX_core::RenderBatch batch;
    batch.commandOffset = 7;
    batch.commands = {
        LX_core::IndexedIndirectDrawCommand{
            .indexCount = 3,
            .instanceCount = 1,
            .firstIndex = 0,
            .vertexOffset = 0,
            .firstInstance = 11,
        },
        LX_core::IndexedIndirectDrawCommand{
            .indexCount = 6,
            .instanceCount = 1,
            .firstIndex = 3,
            .vertexOffset = 0,
            .firstInstance = 12,
        },
    };
    batch.commandCount = static_cast<u32>(batch.commands.size());

    cmdBufferMgr->beginFrame(0);
    commandRecordingStarted = true;
    auto unboundBatchCmd = cmdBufferMgr->allocateBuffer();

    VkCommandBufferBeginInfo unboundBeginInfo{};
    unboundBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(unboundBatchCmd->getHandle(), &unboundBeginInfo);
    unboundBatchCmd->beginRenderPass(renderPass.getHandle(),
                                     framebuffer->getHandle(), extent,
                                     renderPass.getClearValues());
    unboundBatchCmd->setViewport(extent.width, extent.height);
    unboundBatchCmd->setScissor(extent.width, extent.height);
    unboundBatchCmd->bindPipeline(pipeline);

    bool rejectedMissingBatchGeometry = false;
    try {
      unboundBatchCmd->executeRenderBatch(batch);
    } catch (const std::runtime_error &) {
      rejectedMissingBatchGeometry = true;
    }
    unboundBatchCmd->endRenderPass();
    vkEndCommandBuffer(unboundBatchCmd->getHandle());

    if (!rejectedMissingBatchGeometry) {
      std::cerr << "executeRenderBatch accepted an indirect draw without "
                   "explicit batch geometry binding\n";
      return 1;
    }

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

    cmd->bindRenderBatchGeometry(*resourceManager, batchGeometry);
    cmd->executeRenderBatch(batch);
    const auto batchStats = cmd->getRenderBatchSubmissionStats();
    if (batchStats.compilerBatchCountConsumed != 1 ||
        batchStats.boundBatchGeometryCount != 1 ||
        batchStats.submittedDirectIndexedDrawCount != 0 ||
        batchStats.submittedIndexedIndirectCommandCount != 2 ||
        batchStats.submittedIndirectBatchCount != 1 ||
        batchStats.submittedIndirectDrawCount != 2 ||
        batchStats.firstCommandOffset != 7 ||
        batchStats.lastCommandOffset != 8 ||
        batchStats.fallbackObservedCount != 0) {
      std::cerr << "RenderBatch submission stats mismatch: consumed="
                << batchStats.compilerBatchCountConsumed
                << " geometryBinds=" << batchStats.boundBatchGeometryCount
                << " directDraws=" << batchStats.submittedDirectIndexedDrawCount
                << " indexedIndirectCommands="
                << batchStats.submittedIndexedIndirectCommandCount
                << " batches="
                << batchStats.submittedIndirectBatchCount
                << " draws=" << batchStats.submittedIndirectDrawCount
                << " first=" << batchStats.firstCommandOffset
                << " last=" << batchStats.lastCommandOffset
                << " fallback=" << batchStats.fallbackObservedCount << "\n";
      return 1;
    }
    cmd->endRenderPass();

    if (vkEndCommandBuffer(cmd->getHandle()) != VK_SUCCESS) {
      std::cerr << "Failed to end batch command buffer\n";
      return 1;
    }

    VkCommandBuffer submittedBatchCommand = cmd->getHandle();
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &submittedBatchCommand;
    if (vkQueueSubmit(device->getGraphicsQueue(), 1, &submitInfo,
                      VK_NULL_HANDLE) != VK_SUCCESS) {
      std::cerr << "Failed to submit batch command buffer\n";
      return 1;
    }
    vkQueueWaitIdle(device->getGraphicsQueue());

    auto &descriptorMgr = device->getDescriptorManager();
    const auto descriptorFootprint = [&descriptorMgr](const u32 frameCount) {
      usize total = 0;
      for (u32 frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        total += descriptorMgr.getFreeSetCount(frameIndex);
        total += descriptorMgr.getPendingReturnCount(frameIndex);
      }
      return total;
    };

    const usize initialDescriptorFootprint =
        descriptorFootprint(maxFrameInFlight);
    for (u32 frame = 0; frame < 600; ++frame) {
      const u32 frameIndex = frame % maxFrameInFlight;
      cmdBufferMgr->beginFrame(frameIndex);
      descriptorMgr.beginFrame(frameIndex);
      auto loopCmd = cmdBufferMgr->allocateBuffer();

      VkCommandBufferBeginInfo loopBeginInfo{};
      loopBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      vkBeginCommandBuffer(loopCmd->getHandle(), &loopBeginInfo);
      loopCmd->beginRenderPass(renderPass.getHandle(), framebuffer->getHandle(),
                               extent, renderPass.getClearValues());
      loopCmd->setViewport(extent.width, extent.height);
      loopCmd->setScissor(extent.width, extent.height);
      loopCmd->bindPipeline(pipeline);
      loopCmd->bindResources(*resourceManager, pipeline, renderItem);
      loopCmd->executeWorkItem(renderItem);
      loopCmd->endRenderPass();
      vkEndCommandBuffer(loopCmd->getHandle());
    }

    const usize finalDescriptorFootprint =
        descriptorFootprint(maxFrameInFlight);
    if (finalDescriptorFootprint > initialDescriptorFootprint + 16) {
      std::cerr << "Descriptor footprint grew unexpectedly: initial="
                << initialDescriptorFootprint
                << " final=" << finalDescriptorFootprint << "\n";
      return 1;
    }

    framebuffer.reset();

    return 0;
  } catch (const std::exception &e) {
    if (!commandRecordingStarted && isKnownEnvironmentSetupFailure(e.what())) {
      std::cerr << "SKIP VulkanCommandBuffer test: " << e.what() << "\n";
      return 0;
    }
    std::cerr << "FAIL VulkanCommandBuffer test: " << e.what() << "\n";
    return 1;
  }
}
